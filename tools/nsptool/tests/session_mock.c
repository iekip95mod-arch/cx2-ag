#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>

static int test_open(const char *path, int flags, ...);
#define open test_open
#include "../nsptool.c"
#undef open

static int test_open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	if (flags & O_CREAT) {
		va_list arguments;
		va_start(arguments, flags);
		mode = va_arg(arguments, int);
		va_end(arguments);
	}
	if (!strncmp(path, "/tmp/nsptool-", 13)) {
		path = getenv("NSPTOOL_TEST_LOCK");
		assert(path);
	}
	return flags & O_CREAT ? open(path, flags, mode) : open(path, flags);
}

struct nspire_handle {
	unsigned generation;
};

static unsigned initializations;

static void record_call(const char *operation, unsigned generation)
{
	const char *path = getenv("NSPTOOL_TEST_LOG");
	assert(path);
	FILE *log = fopen(path, "a");
	assert(log);
	fprintf(log, "%s %u\n", operation, generation);
	assert(!fclose(log));
}

int nspire_init(nspire_handle_t **handle)
{
	record_call("init", ++initializations);
	fprintf(stdout, "native init stdout\n");
	fprintf(stderr, "native init stderr\n");
	if (getenv("NSPTOOL_TEST_INIT_FAIL"))
		return NSPIRE_ERR_TIMEOUT;
	*handle = malloc(sizeof **handle);
	assert(*handle);
	(*handle)->generation = initializations;
	return 0;
}

void nspire_free(nspire_handle_t *handle)
{
	record_call("free", handle->generation);
	fprintf(stdout, "native free stdout\n");
	fprintf(stderr, "native free stderr\n");
	free(handle);
}

const char *nspire_strerror(int error)
{
	(void)error;
	return "mock transport failure";
}

int nspire_device_info(nspire_handle_t *handle, struct nspire_devinfo *info)
{
	record_call("info", handle->generation);
	memset(info, 0, sizeof *info);
	snprintf(info->device_name, sizeof info->device_name, "generation %u", handle->generation);
	return 0;
}

int nspire_dir_create(nspire_handle_t *handle, const char *path)
{
	record_call("mkdir", handle->generation);
	if (!strcmp(path, "fail"))
		return NSPIRE_ERR_TIMEOUT;
	if (!strcmp(path, "flood")) {
		char chunk[8192];
		memset(chunk, 'x', sizeof chunk);
		for (unsigned i = 0; i < SESSION_MAX_OUTPUT / sizeof chunk + 2; i++)
			assert(fwrite(chunk, 1, sizeof chunk, stdout) == sizeof chunk);
	}
	return 0;
}

int nspire_dir_delete(nspire_handle_t *handle, const char *path)
{
	const char *expected = getenv("NSPTOOL_TEST_RMDIR_PATH");
	assert(expected && !strcmp(path, expected));
	record_call("rmdir", handle->generation);
	return getenv("NSPTOOL_TEST_RMDIR_FAIL") ? -NSPIRE_ERR_TIMEOUT : 0;
}

int nspire_dirlist(nspire_handle_t *handle, const char *path, struct nspire_dir_info **listing)
{
	const char *expected = getenv("NSPTOOL_TEST_RMDIR_PATH");
	if (expected)
		assert(!strcmp(path, expected));
	record_call("ls", handle->generation);
	if (getenv("NSPTOOL_TEST_DIRLIST_FAIL"))
		return -NSPIRE_ERR_TIMEOUT;
	*listing = calloc(1, sizeof **listing + sizeof(struct nspire_dir_item));
	assert(*listing);
	if (getenv("NSPTOOL_TEST_RMDIR_NONEMPTY")) {
		(*listing)->num = 1;
		strcpy((*listing)->items[0].name, "retained.tns");
	}
	return 0;
}

void nspire_dirlist_free(struct nspire_dir_info *listing)
{
	free(listing);
}

int nspire_attr(nspire_handle_t *handle, const char *path, struct nspire_dir_item *item)
{
	(void)handle;
	(void)path;
	memset(item, 0, sizeof *item);
	item->size = 3;
	return 0;
}

int nspire_file_read(nspire_handle_t *handle, const char *path, void *bytes, size_t length, size_t *got)
{
	(void)path;
	assert(length == 3);
	record_call("get", handle->generation);
	memcpy(bytes, "abc", 3);
	*got = 3;
	return 0;
}

int nspire_file_write(nspire_handle_t *handle, const char *path, void *bytes, size_t length)
{
	(void)path;
	assert(length == 3 && !memcmp(bytes, "abc", 3));
	record_call("put", handle->generation);
	return 0;
}

int nspire_file_delete(nspire_handle_t *handle, const char *path)
{
	(void)path;
	record_call("rm", handle->generation);
	return 0;
}

int nspire_screenshot(nspire_handle_t *handle, struct nspire_image **image)
{
	record_call("screenshot", handle->generation);
	*image = calloc(1, sizeof **image + 1);
	assert(*image);
	(*image)->width = 1;
	(*image)->height = 1;
	(*image)->bpp = 8;
	(*image)->data[0] = 127;
	return 0;
}

int nspire_service_exchange(nspire_handle_t *handle, uint16_t sid, const void *request,
			    size_t length, void *reply, size_t capacity, size_t *got)
{
	assert(sid == KEYSVC_SID && capacity >= 4);
	const struct keyrec *records = request;
	unsigned char *answer = reply;
	record_call(records[0].action == ACT_QUERY ? "query" : "key", handle->generation);
	answer[0] = 'k';
	answer[1] = length / sizeof *records;
	answer[2] = KEYSVC_VERSION;
	answer[3] = KEYSVC_MAX_RECORDS;
	*got = records[0].action == ACT_QUERY ? 4 : 2;
	return 0;
}

int nspire_send_key(nspire_handle_t *handle, uint32_t key)
{
	static unsigned sent;
	char operation[32];
	snprintf(operation, sizeof operation, "key-os %06" PRIx32, key);
	record_call(operation, handle->generation);
	const char *fail_at = getenv("NSPTOOL_TEST_KEY_OS_FAIL");
	return fail_at && ++sent == strtoul(fail_at, NULL, 10) ? -NSPIRE_ERR_TIMEOUT : 0;
}
