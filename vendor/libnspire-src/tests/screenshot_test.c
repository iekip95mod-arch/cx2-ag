#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/data.h"
#include "src/service.h"
#include "src/api/error.h"
#include "src/api/screenshot.h"

static unsigned char header[15], compressed[1024];
static size_t header_length, compressed_length, chunk_size, consumed;
static unsigned reads, disconnects;
static int read_error, write_error, extra_byte, empty_chunk, oversized_header;

uint32_t packet_max_datasize(nspire_handle_t *handle)
{
	(void)handle;
	return 1440;
}

int service_connect(nspire_handle_t *handle, uint16_t sid)
{
	(void)handle;
	assert(sid == 0x4024);
	return 0;
}

int service_disconnect(nspire_handle_t *handle)
{
	(void)handle;
	disconnects++;
	return 0;
}

int data_write(nspire_handle_t *handle, void *bytes, size_t length)
{
	(void)handle;
	assert(length == 1 && *(unsigned char *)bytes == 0);
	return write_error;
}

int data_read(nspire_handle_t *handle, void *bytes, size_t capacity, size_t *actual)
{
	(void)handle;
	memset(bytes, 0xA5, capacity);
	if (!reads++) {
		memcpy(bytes, header, header_length);
		if (actual) *actual = oversized_header ? capacity + 1 : header_length;
		return 0;
	}
	if (read_error)
		return read_error;
	if (consumed == compressed_length)
		return -NSPIRE_ERR_TIMEOUT;
	size_t length = compressed_length - consumed;
	if (length > chunk_size) length = chunk_size;
	if (empty_chunk) length = 0;
	assert(length + 1 < capacity);
	*(unsigned char *)bytes = 1;
	memcpy((unsigned char *)bytes + 1, compressed + consumed, length);
	consumed += length;
	if (actual) *actual = length + 1 + extra_byte;
	return 0;
}

static void prepare(uint16_t width, uint16_t height, uint8_t bpp, const unsigned char *rle, size_t length)
{
	assert(length <= sizeof compressed);
	memset(header, 0, sizeof header);
	header[1] = length >> 24;
	header[2] = length >> 16;
	header[3] = length >> 8;
	header[4] = length;
	header[9] = width >> 8;
	header[10] = width;
	header[11] = height >> 8;
	header[12] = height;
	header[13] = bpp;
	memcpy(compressed, rle, length);
	header_length = sizeof header;
	compressed_length = length;
	chunk_size = sizeof compressed;
	consumed = reads = disconnects = 0;
	read_error = write_error = extra_byte = empty_chunk = oversized_header = 0;
}

static void rejected(void)
{
	struct nspire_image *image = NULL;
	int status = nspire_screenshot(NULL, &image);
	if (!status) {
		free(image);
		fputs("malformed screenshot was reported as a successful image\n", stderr);
		abort();
	}
	assert(image == NULL && disconnects == 1);
}

static void valid_controls(void)
{
	for (unsigned bpp = 4; bpp <= 16; bpp *= 2) {
		unsigned unit = bpp / 4;
		unsigned char literal[17] = { 0xFD }, expected[16];
		for (unsigned j = 0; j < unit * 4; j++)
			expected[j] = literal[j + 1] = j + 1;
		for (unsigned fragment = 1; fragment <= 3; fragment++) {
			prepare(4, 2, bpp, literal, 1 + unit * 4);
			chunk_size = fragment;
			struct nspire_image *image = NULL;
			assert(nspire_screenshot(NULL, &image) == 0);
			assert(image && image->width == 4 && image->height == 2 && image->bpp == bpp);
			assert(!memcmp(image->data, expected, unit * 4));
			assert(consumed == compressed_length && disconnects == 1);
			free(image);
		}
		literal[0] = 3;
		prepare(4, 2, bpp, literal, 1 + unit);
		struct nspire_image *image = NULL;
		assert(nspire_screenshot(NULL, &image) == 0);
		for (unsigned j = 0; j < 4; j++)
			assert(!memcmp(image->data + j * unit, literal + 1, unit));
		free(image);
		unsigned char longest[517] = { 0x80 };
		for (unsigned j = 0; j < 129 * unit; j++) longest[j + 1] = j;
		prepare(258, 1, bpp, longest, 1 + 129 * unit);
		assert(nspire_screenshot(NULL, &image) == 0);
		assert(!memcmp(image->data, longest + 1, 129 * unit));
		free(image);
		longest[0] = 127;
		prepare(256, 1, bpp, longest, 1 + unit);
		assert(nspire_screenshot(NULL, &image) == 0);
		for (unsigned j = 0; j < 128; j++)
			assert(!memcmp(image->data + j * unit, longest + 1, unit));
		free(image);
	}
}

int main(void)
{
	const uint64_t boundaries[] = { 0, INT32_MAX, UINT64_C(1) << 31, UINT32_MAX,
		UINT64_C(1) << 63, UINT64_MAX };
	for (unsigned j = 0; j < sizeof boundaries / sizeof boundaries[0]; j++) {
		unsigned char bytes[8];
		for (unsigned k = 0; k < sizeof bytes; k++)
			bytes[k] = boundaries[j] >> (56 - k * 8);
		uint32_t word = 0;
		uint64_t wide = 0;
		assert(data_scan("w", bytes + 4, 4, &word) == 0 && word == (uint32_t)boundaries[j]);
		assert(data_scan("l", bytes, 8, &wide) == 0 && wide == boundaries[j]);
	}
	const unsigned char short_run[] = { 0, 0x11 };
	prepare(4, 1, 8, short_run, sizeof short_run);
	rejected();
	valid_controls();
	const unsigned char correct[] = { 1, 0x12, 0x34 };
	for (unsigned bytes = 0; bytes < sizeof header; bytes++) {
		prepare(4, 1, 8, correct, sizeof correct);
		header_length = bytes;
		rejected();
	}
	const unsigned char malformed[][8] = {
		{ 0 }, { 0, 1, 2 }, { 2, 1, 2 }, { 0xFF, 1, 2, 3 },
		{ 0xFE, 1, 2, 3, 4, 5, 6 }, { 1, 1, 2, 0 }, { 1, 1, 2, 0, 3, 4 }
	};
	const size_t sizes[] = { 1, 3, 3, 4, 7, 4, 6 };
	for (unsigned j = 0; j < sizeof sizes / sizeof sizes[0]; j++) {
		prepare(4, 1, 8, malformed[j], sizes[j]);
		rejected();
	}
	prepare(4, 1, 8, correct, sizeof correct);
	oversized_header = 1;
	rejected();
	prepare(4, 1, 8, correct, sizeof correct);
	extra_byte = 1;
	rejected();
	prepare(4, 1, 8, correct, sizeof correct);
	empty_chunk = 1;
	rejected();
	prepare(4, 1, 8, correct, sizeof correct);
	read_error = -NSPIRE_ERR_TIMEOUT;
	rejected();
	prepare(4, 1, 8, correct, sizeof correct);
	write_error = -NSPIRE_ERR_TIMEOUT;
	rejected();
	prepare(0, 1, 8, correct, sizeof correct);
	rejected();
	prepare(1, 0, 8, correct, sizeof correct);
	rejected();
	prepare(1, 1, 4, correct, sizeof correct);
	rejected();
	prepare(65535, 65535, 16, correct, sizeof correct);
	rejected();
	prepare(65534, 65534, 16, correct, sizeof correct);
	rejected();
	prepare(4, 1, 8, correct, sizeof correct);
	memset(header + 1, 0xFF, 4);
	rejected();
	for (unsigned bpp = 0; bpp < 256; bpp++) {
		if (bpp == 4 || bpp == 8 || bpp == 16) continue;
		prepare(4, 1, bpp, correct, sizeof correct);
		rejected();
	}
	prepare(4, 1, 8, correct, 0);
	rejected();
	puts("screenshot: malformed headers/chunks/RLE rejected, fragmented grayscale/color controls passed");
	return 0;
}
