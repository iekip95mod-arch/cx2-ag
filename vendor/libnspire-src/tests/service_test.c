#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/data.h"
#include "src/service.h"
#include "src/api/error.h"
#include "src/api/dir.h"
#include "src/api/screenshot.h"
#include "src/api/devinfo.h"
#include "src/api/raw.h"
#include "src/api/os.h"

struct frame {
	unsigned char bytes[254];
	size_t length;
};

static struct frame incoming[8];
static unsigned queued, received, written, closed, failures;
static int close_error, write_error;

int packet_prepare_cx2(nspire_handle_t *handle)
{
	assert(handle->is_cx2);
	return 0;
}

uint32_t packet_max_datasize(nspire_handle_t *handle)
{
	(void)handle;
	return 254;
}

int data_write_special(nspire_handle_t *handle, void *bytes, size_t length,
		void (*callback)(struct packet *))
{
	struct packet packet = { 0 };
	assert(callback && length == 2);
	callback(&packet);
	assert(packet.src_sid == 0x40DE);
	assert(((unsigned char *)bytes)[0] == (handle->host_sid >> 8));
	assert(((unsigned char *)bytes)[1] == (handle->host_sid & 0xFF));
	closed++;
	return close_error;
}

int data_write(nspire_handle_t *handle, void *bytes, size_t length)
{
	(void)bytes;
	assert(handle->connected && length);
	written++;
	return write_error;
}

int data_read(nspire_handle_t *handle, void *bytes, size_t capacity, size_t *actual)
{
	assert(handle->connected && received < queued);
	struct frame *frame = &incoming[received++];
	assert(frame->length <= capacity);
	memset(bytes, 0, capacity);
	memcpy(bytes, frame->bytes, frame->length);
	if (actual) *actual = frame->length;
	return 0;
}

static void push(const void *bytes, size_t length)
{
	assert(queued < 8 && length <= sizeof incoming[0].bytes);
	memcpy(incoming[queued].bytes, bytes, length);
	incoming[queued++].length = length;
}

static void status(unsigned code)
{
	unsigned char bytes[] = { code >> 8, code };
	push(bytes, sizeof bytes);
}

static const char *names[] = {
	"file write", "file read", "file move", "file copy", "file delete",
	"directory list", "directory create", "directory delete", "attribute",
	"screenshot", "device info", "raw exchange", "OS send"
};

static void check_api(unsigned which, int closing, int primary)
{
	nspire_handle_t handle = { 0 };
	handle.host_sid = 0x8000;
	queued = received = written = closed = 0;
	close_error = closing;
	write_error = primary;
	unsigned char bytes[254] = { 0 };
	size_t length = 0, count = 99;
	struct nspire_dir_info *listing = NULL;
	struct nspire_image *image = NULL;
	struct nspire_dir_item attribute;
	struct nspire_devinfo info;
	int code;
	switch (which) {
	case 0:
		bytes[0] = 4;
		push(bytes, 1);
		status(0xFF00);
		code = nspire_file_write(&handle, "/a", bytes, 1);
		break;
	case 1:
		assert(!data_build("h000000000w", bytes, sizeof bytes, &length, 0x0301, UINT32_C(1)));
		push(bytes, length);
		status(0x0561);
		code = nspire_file_read(&handle, "/a", bytes, sizeof bytes, &count);
		if (!primary) assert(count == 1 && bytes[0] == 'a');
		break;
	case 2:
		status(0xFF00);
		code = nspire_file_move(&handle, "/a", "/b");
		break;
	case 3:
		status(0xFF00);
		code = nspire_file_copy(&handle, "/a", "/b");
		break;
	case 4:
		status(0xFF00);
		code = nspire_file_delete(&handle, "/a");
		break;
	case 5:
		status(0xFF00);
		assert(!data_build("hbswwb0", bytes, sizeof bytes, &length, 0x1000, 0, "a", UINT32_C(37), UINT32_C(9), 0));
		push(bytes, length);
		status(0xFF11);
		status(0xFF00);
		code = nspire_dirlist(&handle, "/", &listing);
		if (!code) assert(listing && listing->num == 1 && listing->items[0].size == 37);
		break;
	case 6:
		status(0xFF00);
		code = nspire_dir_create(&handle, "/a");
		break;
	case 7:
		status(0xFF00);
		code = nspire_dir_delete(&handle, "/a");
		break;
	case 8:
		assert(!data_build("bwwb0", bytes, sizeof bytes, &length, 0x20, UINT32_C(37), UINT32_C(9), 0));
		push(bytes, length);
		code = nspire_attr(&handle, "/a", &attribute);
		if (!primary) assert(attribute.size == 37 && attribute.date == 9);
		break;
	case 9:
		assert(!data_build("bwhhhhbb", bytes, sizeof bytes, &length, 0, UINT32_C(3), 0, 0, 4, 1, 8, 0));
		push(bytes, length);
		bytes[0] = 1;
		bytes[1] = 1;
		bytes[2] = 0x12;
		bytes[3] = 0x34;
		push(bytes, 4);
		code = nspire_screenshot(&handle, &image);
		if (!code) assert(image && image->width == 4 && image->data[0] == 0x12);
		break;
	case 10:
		push(bytes, 253);
		push("\2CX II", 7);
		push("\3tns\0tcc", 9);
		code = nspire_device_info(&handle, &info);
		if (!primary) assert(!strcmp(info.device_name, "CX II"));
		break;
	case 11:
		status(0x6B01);
		code = nspire_service_exchange(&handle, 0x4044, "restart", 7, bytes, sizeof bytes, &count);
		if (!primary) assert(count == 2 && bytes[0] == 'k' && bytes[1] == 1);
		break;
	default:
		status(0x0400);
		status(0xFF00);
		status(0x0664);
		code = nspire_os_send(&handle, bytes, 1);
		break;
	}
	int expected = primary ? primary : closing;
	if (code != expected || (code && (listing || image))) {
		fprintf(stderr, "%s close=%d primary=%d returned=%d expected=%d owned_output=%d\n",
			names[which], closing, primary, code, expected, listing != NULL || image != NULL);
		failures++;
	}
	free(listing);
	free(image);
	assert(closed == 1 && written > 0);
	if (primary) assert(received == 0 && written == 1);
	else assert(received == queued);
	assert(handle.connected == (closing != 0));
	assert(handle.host_sid == (closing ? 0x8000 : 0x8001));
	assert(service_connect(&handle, 0x4020) == (closing ? -NSPIRE_ERR_BUSY : 0));
}

int main(void)
{
	const int closing[] = { 0, -NSPIRE_ERR_TIMEOUT, -NSPIRE_ERR_DISCONNECTED, -NSPIRE_ERR_INVALPKT };
	for (unsigned which = 0; which < sizeof names / sizeof names[0]; which++) {
		for (unsigned close = 0; close < sizeof closing / sizeof closing[0]; close++) {
			check_api(which, closing[close], 0);
			check_api(which, closing[close], -NSPIRE_ERR_NONEXIST);
		}
	}
	if (failures) {
		fprintf(stderr, "service completion: %u failures\n", failures);
		return 1;
	}
	puts("service completion: 13 APIs, 104 success/close/primary-error cases passed");
	return 0;
}
