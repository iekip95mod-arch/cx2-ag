#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/data.h"
#include "src/service.h"
#include "src/api/error.h"
#include "src/api/dir.h"

struct frame {
	unsigned char bytes[512];
	size_t length;
};

static struct frame incoming[32], outgoing[32];
static unsigned received, queued, written, connected, disconnected;

uint32_t packet_max_datasize(nspire_handle_t *handle)
{
	(void)handle;
	return 254;
}

int service_connect(nspire_handle_t *handle, uint16_t sid)
{
	(void)handle;
	assert(sid == 0x4060);
	connected++;
	return 0;
}

int service_disconnect(nspire_handle_t *handle)
{
	(void)handle;
	disconnected++;
	return 0;
}

int data_read(nspire_handle_t *handle, void *bytes, size_t capacity, size_t *actual)
{
	(void)handle;
	if (received == queued)
		return -NSPIRE_ERR_TIMEOUT;
	struct frame *frame = &incoming[received++];
	size_t count = frame->length < capacity ? frame->length : capacity;
	memset(bytes, 0, capacity);
	memcpy(bytes, frame->bytes, count);
	if (actual) *actual = count;
	return 0;
}

int data_write(nspire_handle_t *handle, void *bytes, size_t length)
{
	(void)handle;
	assert(written < 32 && length <= sizeof outgoing[0].bytes);
	outgoing[written].length = length;
	memcpy(outgoing[written++].bytes, bytes, length);
	return 0;
}

static void reset(void)
{
	received = queued = written = connected = disconnected = 0;
}

static void push(const void *bytes, size_t length)
{
	assert(queued < 32 && length <= sizeof incoming[0].bytes);
	memcpy(incoming[queued].bytes, bytes, length);
	incoming[queued++].length = length;
}

static void status(unsigned code)
{
	unsigned char bytes[] = { code >> 8, code };
	push(bytes, sizeof bytes);
}

static void file_header(uint32_t size)
{
	unsigned char header[15];
	size_t length;
	assert(!data_build("h000000000w", header, sizeof header, &length, 0x0301, size));
	assert(length == sizeof header);
	push(header, length);
}

static void check_read_tag(void)
{
	reset();
	file_header(3);
	const unsigned char wrong[] = { 0x04, 'a', 'b', 'c' };
	push(wrong, sizeof wrong);
	unsigned char bytes[3] = { 0 };
	size_t count = 99;
	int code = nspire_file_read(NULL, "/a", bytes, sizeof bytes, &count);
	if (!code) {
		fputs("wrong-tag file chunk was reported as successful file content\n", stderr);
		abort();
	}
	assert(count == 0 && bytes[0] == 0 && disconnected == 1);
}

static void check_dir_final(void)
{
	reset();
	status(0xFF00);
	status(0xFF11);
	status(0xFF0F);
	struct nspire_dir_info *listing = NULL;
	int code = nspire_dirlist(NULL, "/", &listing);
	if (!code) {
		nspire_dirlist_free(listing);
		fputs("failed directory close was reported as successful enumeration\n", stderr);
		abort();
	}
	assert(listing == NULL && disconnected == 1);
}

static void check_short_status(void)
{
	reset();
	const unsigned char partial[] = { 0xFF };
	push(partial, sizeof partial);
	if (!nspire_file_move(NULL, "/a", "/b")) {
		fputs("one-byte status was extended with unread stack bytes and reported as success\n", stderr);
		abort();
	}
	assert(disconnected == 1);
}

static void check_read(void)
{
	const unsigned char first[] = { 5, 'a' }, rest[] = { 5, 'b', 'c' };
	for (unsigned capacity = 0; capacity <= 5; capacity++) {
		reset();
		file_header(3);
		push(first, sizeof first);
		push(rest, sizeof rest);
		unsigned char bytes[5] = { 0 };
		size_t count = 99;
		assert(!nspire_file_read(NULL, "/a", bytes, capacity, &count));
		assert(count == 3 && !memcmp(bytes, "abc", capacity < 3 ? capacity : 3));
		assert(written == 3 && outgoing[2].bytes[0] == 0xFF && outgoing[2].bytes[1] == 0);
		assert(disconnected == 1);
	}
	reset();
	file_header(0);
	size_t count = 99;
	assert(!nspire_file_read(NULL, "/empty", NULL, 0, &count));
	assert(count == 0 && received == 1 && written == 3 && disconnected == 1);
	for (unsigned length = 0; length < 15; length++) {
		reset();
		file_header(0);
		incoming[0].length = length;
		assert(nspire_file_read(NULL, "/a", NULL, 0, &count) != 0);
		assert(count == 0 && disconnected == 1);
	}
	reset();
	status(0xFF0A);
	assert(nspire_file_read(NULL, "/missing", NULL, 0, &count) == -NSPIRE_ERR_NONEXIST);
	const unsigned char bad[] = { 5, 'a', 'b', 'c', 'd' };
	for (unsigned length = 0; length <= 5; length++) {
		if (length == 4) continue;
		reset();
		file_header(3);
		push(bad, length);
		assert(nspire_file_read(NULL, "/a", NULL, 0, &count) != 0);
		assert(disconnected == 1);
	}
}

static void check_write(void)
{
	unsigned char bytes[600];
	for (unsigned i = 0; i < sizeof bytes; i++) bytes[i] = i;
	const unsigned char ready[] = { 4 };
	for (unsigned size = 0; size <= sizeof bytes; size += sizeof bytes) {
		reset();
		push(ready, sizeof ready);
		status(0xFF00);
		assert(!nspire_file_write(NULL, "/a", bytes, size));
		assert(disconnected == 1);
		size_t copied = 0;
		for (unsigned i = 1; i < written; i++) {
			assert(outgoing[i].bytes[0] == 5);
			assert(!memcmp(outgoing[i].bytes + 1, bytes + copied, outgoing[i].length - 1));
			copied += outgoing[i].length - 1;
		}
		assert(copied == size);
	}
	for (unsigned length = 0; length < 2; length++) {
		reset();
		push(ready, sizeof ready);
		status(0xFF00);
		incoming[1].length = length;
		assert(nspire_file_write(NULL, "/a", NULL, 0) != 0 && disconnected == 1);
	}
	reset();
	push(ready, 0);
	assert(nspire_file_write(NULL, "/a", NULL, 0) != 0 && disconnected == 1);
	reset();
	push(ready, 1);
	status(0xFF0A);
	assert(nspire_file_write(NULL, "/a", NULL, 0) == -NSPIRE_ERR_NONEXIST);
	if (SIZE_MAX > UINT32_MAX) {
		reset();
		assert(nspire_file_write(NULL, "/a", NULL, (size_t)UINT32_MAX + 1) == -NSPIRE_ERR_INVALID);
		assert(!connected && !written);
	}
}

static int mutation(unsigned which)
{
	switch (which) {
	case 0: return nspire_file_move(NULL, "/a", "/b");
	case 1: return nspire_file_copy(NULL, "/a", "/b");
	case 2: return nspire_file_delete(NULL, "/a");
	case 3: return nspire_dir_create(NULL, "/a");
	default: return nspire_dir_delete(NULL, "/a");
	}
}

static void check_mutations(void)
{
	for (unsigned which = 0; which < 5; which++) {
		for (unsigned length = 0; length <= 3; length++) {
			reset();
			const unsigned char reply[] = { 0xFF, 0, 0xA5 };
			push(reply, length);
			int code = mutation(which);
			assert(length < 2 ? code != 0 : code == 0);
			assert(disconnected == 1);
		}
		reset();
		status(0xFF0A);
		const int errors[] = { NSPIRE_ERR_INVALID, NSPIRE_ERR_INVALID, NSPIRE_ERR_EXISTS,
			NSPIRE_ERR_EXISTS, NSPIRE_ERR_NONEXIST };
		assert(mutation(which) == -errors[which]);
	}
}

static void check_directories(void)
{
	unsigned char entry[254];
	size_t length;
	assert(!data_build("hbswwb0", entry, sizeof entry, &length, 0x1000, 0, "a", UINT32_C(37), UINT32_MAX, 1));
	for (unsigned ending = 0; ending < 2; ending++) {
		reset();
		status(0xFF00);
		status(0xFF11);
		status(ending ? 0xFF11 : 0xFF00);
		struct nspire_dir_info *listing = NULL;
		assert(!nspire_dirlist(NULL, "/", &listing));
		assert(listing->num == 0 && disconnected == 1);
		nspire_dirlist_free(listing);
		reset();
		status(0xFF00);
		status(0xFF00);
		push(entry, length);
		status(0xFF11);
		status(ending ? 0xFF11 : 0xFF00);
		listing = NULL;
		assert(!nspire_dirlist(NULL, "/", &listing));
		assert(listing->num == 1 && !strcmp(listing->items[0].name, "a"));
		assert(listing->items[0].size == 37 && listing->items[0].date == UINT32_MAX);
		assert(listing->items[0].type == NSPIRE_DIR && disconnected == 1);
		nspire_dirlist_free(listing);
	}
	for (unsigned truncated = 0; truncated < length; truncated++) {
		reset();
		status(0xFF00);
		push(entry, truncated);
		struct nspire_dir_info *listing = NULL;
		assert(nspire_dirlist(NULL, "/", &listing) != 0 && listing == NULL && disconnected == 1);
	}
	for (unsigned phase = 0; phase < 3; phase++) {
		reset();
		status(0xFF00);
		status(0xFF11);
		status(0xFF00);
		incoming[phase].length = 1;
		struct nspire_dir_info *listing = NULL;
		assert(nspire_dirlist(NULL, "/", &listing) != 0 && listing == NULL && disconnected == 1);
	}
	reset();
	status(0xFF0A);
	struct nspire_dir_info *listing = NULL;
	assert(nspire_dirlist(NULL, "/missing", &listing) == -NSPIRE_ERR_NONEXIST && listing == NULL);
	reset();
	status(0xFF00);
	status(0xFF0F);
	assert(nspire_dirlist(NULL, "/", &listing) == -NSPIRE_ERR_INVALID && listing == NULL);
	for (unsigned truncated = 0; truncated <= 11; truncated++) {
		reset();
		unsigned char attr[11] = { 0x20, 0, 0, 0, 37, 0, 0, 0, 9, 0, 0 };
		push(attr, truncated);
		struct nspire_dir_item info;
		int code = nspire_attr(NULL, "/a", &info);
		assert(truncated < 11 ? code != 0 : code == 0);
		if (!code) assert(info.size == 37 && info.date == 9 && info.type == NSPIRE_FILE);
		assert(disconnected == 1);
	}
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "short-status")) {
		check_short_status();
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "dir-final")) {
		check_dir_final();
		return 0;
	}
	check_read_tag();
	if (argc == 2) return 0;
	check_dir_final();
	check_short_status();
	check_read();
	check_write();
	check_mutations();
	check_directories();
	puts("file/dir: actual-length, tagged chunks, empty/fragmented/truncated transfers, and enumeration checks passed");
	return 0;
}
