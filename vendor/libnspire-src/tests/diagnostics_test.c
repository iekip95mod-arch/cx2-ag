#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/data.h"
#include "src/service.h"
#include "src/api/dir.h"
#include "src/api/error.h"

static unsigned calls, fail_at, closes, writes, failures;
static int closing, stat_request;

int packet_prepare_cx2(nspire_handle_t *handle) {
	assert(handle->is_cx2);
	return 0;
}

uint32_t packet_max_datasize(nspire_handle_t *handle) {
	(void)handle;
	return 1440;
}

int data_write_special(nspire_handle_t *handle, void *bytes, size_t length,
		void (*callback)(struct packet *)) {
	struct packet packet = { 0 };
	(void)handle;
	(void)bytes;
	assert(length == 2 && callback);
	callback(&packet);
	assert(packet.src_sid == 0x40DE);
	closes++;
	return closing;
}

int data_write(nspire_handle_t *handle, void *bytes, size_t length) {
	(void)bytes;
	assert(handle->connected && length);
	writes++;
	return ++calls == fail_at ? -NSPIRE_ERR_TIMEOUT : 0;
}

int data_read(nspire_handle_t *handle, void *bytes, size_t capacity, size_t *actual) {
	assert(handle->connected);
	if (++calls == fail_at) return -NSPIRE_ERR_TIMEOUT;
	if (stat_request == 1)
		return data_build("bwwb0", bytes, capacity, actual, 0x20, UINT32_C(8461), UINT32_C(9), 0);
	if (stat_request == 2) {
		if (calls == 2)
			return data_build("h000000000w", bytes, capacity, actual, 0x0301, UINT32_C(1500));
		*actual = calls == 4 ? 1440 : 62;
		assert(*actual <= capacity);
		memset(bytes, 'Q', *actual);
		((unsigned char *)bytes)[0] = 5;
		return 0;
	}
	if (calls == 2) {
		assert(capacity);
		((unsigned char *)bytes)[0] = 4;
		*actual = 1;
		return 0;
	}
	return data_build("h", bytes, capacity, actual, 0xFF00);
}

static void check(int condition, const char *name, const char *field) {
	if (!condition) {
		fprintf(stderr, "%s: missing or incorrect %s\n", name, field);
		failures++;
	}
}

static void run(unsigned fault, int close_error, int is_stat, const char *phase,
		unsigned expected_writes, const char *offset, int trace) {
	calls = closes = writes = 0;
	fail_at = fault;
	closing = close_error;
	stat_request = is_stat;
	nspire_handle_t handle = { 0 };
	handle.host_sid = 0x8000;
	unsigned char document[8461];
	memset(document, 'Q', sizeof document);
	memcpy(document, "PRIVATE-DOCUMENT-CONTENT", 24);
	struct nspire_dir_item info;
	FILE *capture = tmpfile();
	assert(capture);
	fflush(stderr);
	int saved = dup(STDERR_FILENO);
	assert(saved >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0);
	if (trace) setenv("NSPIRE_TRACE", "1", 1);
	else unsetenv("NSPIRE_TRACE");
	size_t count = 0;
	int status = is_stat == 1 ? nspire_attr(&handle, "/PRIVATE-PATH", &info)
		: is_stat == 2 ? nspire_file_read(&handle, "/PRIVATE-PATH", document, sizeof document, &count)
		: nspire_file_write(&handle, "/PRIVATE-PATH", document, sizeof document);
	fflush(stderr);
	assert(dup2(saved, STDERR_FILENO) >= 0);
	close(saved);
	rewind(capture);
	char log[32768];
	size_t length = fread(log, 1, sizeof log - 1, capture);
	assert(!ferror(capture) && feof(capture));
	log[length] = 0;
	fclose(capture);
	int expected = fault ? -NSPIRE_ERR_TIMEOUT : close_error;
	check(status == expected, phase, "primary error preservation");
	check(closes == 1 && writes == expected_writes, phase, "single close and no replay");
	check(handle.connected == !!close_error, phase, "connection state");
	check(handle.host_sid == (close_error ? 0x8000 : 0x8001), phase, "service ID");
	check(!strstr(log, "PRIVATE") && !strstr(log, "51 51 51"), phase, "metadata-only output");
	if (!trace) {
		check(length == 0, phase, "disabled trace silence");
		return;
	}
	check(strstr(log, phase) != NULL, phase, "file phase");
	check(strstr(log, offset) != NULL, phase, "confirmed offset");
	const char *operation = is_stat == 1 ? "stat" : is_stat == 2 ? "read" : "write";
	char ending[160];
	snprintf(ending, sizeof ending, "file operation=%s %s event=end %s", operation, phase, offset);
	if (strcmp(phase, "phase=close")) check(strstr(log, ending) != NULL, phase, "exact ending phase and offset");
	check(strstr(log, operation) != NULL, phase, "operation");
	char completion[96];
	snprintf(completion, sizeof completion, "primary_status=%d close_status=%d", fault ? -NSPIRE_ERR_TIMEOUT : 0, close_error);
	check(strstr(log, completion) != NULL, phase, "separate primary and close status");
}

int main(void) {
	run(1, 0, 0, "phase=request", 1, "offset=0", 1);
	run(2, 0, 0, "phase=ready", 1, "offset=0", 1);
	run(4, 0, 0, "phase=chunk", 3, "offset=1439", 1);
	run(9, 0, 0, "phase=final-status", 7, "offset=8461", 1);
	run(0, -NSPIRE_ERR_TIMEOUT, 0, "phase=close", 7, "offset=8461", 1);
	run(1, -NSPIRE_ERR_DISCONNECTED, 0, "phase=request", 1, "offset=0", 1);
	run(1, 0, 1, "phase=request", 1, "offset=0", 1);
	run(2, 0, 1, "phase=response", 1, "offset=0", 1);
	run(0, -NSPIRE_ERR_TIMEOUT, 1, "phase=close", 1, "offset=0", 1);
	run(0, 0, 0, "phase=final-status", 7, "offset=8461", 1);
	run(1, 0, 0, "phase=request", 1, "offset=0", 0);
	run(1, 0, 2, "phase=request", 1, "offset=0", 1);
	run(2, 0, 2, "phase=header", 1, "offset=0", 1);
	run(3, 0, 2, "phase=ready", 2, "offset=0", 1);
	run(5, 0, 2, "phase=chunk", 2, "offset=1439", 1);
	run(6, 0, 2, "phase=final-status", 3, "offset=1500", 1);
	run(0, -NSPIRE_ERR_TIMEOUT, 2, "phase=close", 3, "offset=1500", 1);
	run(0, 0, 2, "phase=final-status", 3, "offset=1500", 1);
	printf("18 file/stat diagnostic cases: %u failures\n", failures);
	return failures ? 1 : 0;
}
