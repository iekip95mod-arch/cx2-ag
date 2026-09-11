#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

static FILE *output_fopen(const char *, const char *);
static int output_printf(const char *, ...);
static int output_fprintf(FILE *, const char *, ...);

#define fopen output_fopen
#define printf output_printf
#define fprintf output_fprintf
#define main nsptool_main
#include "../nsptool.c"
#undef main
#undef fprintf
#undef printf
#undef fopen

enum output_fault { NO_FAULT, OPEN_FAULT, HEADER_FAULT, PIXEL_FAULT, WRITE_FAULT, CLOSE_FAULT };

static enum output_fault fault;
static char destination[] = "/tmp/nsptool-output-XXXXXX";
static char success[256], diagnostic[256], stdio_buffer[1024];
static unsigned open_calls, read_calls, capture_calls, close_calls;
static size_t attr_size, file_size, accepted_bytes;
static unsigned screenshot_bpp;
static const unsigned char file_bytes[] = { 1, 3, 5, 7, 9, 11, 13, 15 };

static int output_printf(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	int length = vsnprintf(success, sizeof success, format, arguments);
	va_end(arguments);
	return length;
}

static int output_fprintf(FILE *stream, const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	int length = stream == stderr ? vsnprintf(diagnostic, sizeof diagnostic, format, arguments)
				     : vfprintf(stream, format, arguments);
	va_end(arguments);
	return length;
}

static int sink_write(void *cookie, const char *bytes, int length)
{
	(void)cookie;
	(void)bytes;
	if (fault != PIXEL_FAULT || accepted_bytes >= 11) {
		errno = ENOSPC;
		return -1;
	}
	accepted_bytes += (size_t)length;
	return length;
}

static int sink_close(void *cookie)
{
	(void)cookie;
	close_calls++;
	return 0;
}

#ifndef __APPLE__
static ssize_t cookie_write(void *cookie, const char *bytes, size_t length)
{
	assert(length <= INT_MAX);
	return sink_write(cookie, bytes, (int)length);
}
#endif

static FILE *output_fopen(const char *path, const char *mode)
{
	assert(!strcmp(path, destination) && !strcmp(mode, "wb"));
	open_calls++;
	if (fault == NO_FAULT)
		return fopen(path, mode);
	if (fault == OPEN_FAULT) {
		errno = EACCES;
		return NULL;
	}
#ifdef __APPLE__
	FILE *stream = funopen(NULL, NULL, sink_write, NULL, sink_close);
#else
	cookie_io_functions_t callbacks = { .write = cookie_write, .close = sink_close };
	FILE *stream = fopencookie(NULL, "w", callbacks);
#endif
	assert(stream);
	assert(setvbuf(stream, stdio_buffer, fault == CLOSE_FAULT ? _IOFBF : _IONBF,
		       sizeof stdio_buffer) == 0);
	return stream;
}

const char *nspire_strerror(int error)
{
	(void)error;
	return "output fixture transport failure";
}

int nspire_attr(nspire_handle_t *handle, const char *remote, struct nspire_dir_item *item)
{
	assert(!handle && !strcmp(remote, "/output-test.tns"));
	memset(item, 0, sizeof *item);
	item->size = attr_size;
	return 0;
}

int nspire_file_read(nspire_handle_t *handle, const char *remote, void *buffer,
		     size_t capacity, size_t *received)
{
	assert(!handle && !strcmp(remote, "/output-test.tns"));
	assert(capacity == attr_size && read_calls++ == 0);
	assert(file_size <= sizeof file_bytes);
	memcpy(buffer, file_bytes, file_size < capacity ? file_size : capacity);
	*received = file_size;
	return 0;
}

int nspire_screenshot(nspire_handle_t *handle, struct nspire_image **image)
{
	assert(!handle && capture_calls++ == 0);
	*image = calloc(1, sizeof **image + 2);
	assert(*image);
	(*image)->width = 1;
	(*image)->height = 1;
	(*image)->bpp = screenshot_bpp;
	(*image)->data[0] = screenshot_bpp == 8 ? 127 : 0xE0;
	(*image)->data[1] = 0x07;
	return 0;
}

static void reset(enum output_fault selected)
{
	fault = selected;
	open_calls = read_calls = capture_calls = close_calls = 0;
	accepted_bytes = 0;
	success[0] = diagnostic[0] = '\0';
	attr_size = file_size = sizeof file_bytes;
	screenshot_bpp = 16;
	FILE *existing = fopen(destination, "wb");
	assert(existing);
	assert(fwrite("retain", 1, 6, existing) == 6);
	assert(fclose(existing) == 0);
}

static void expect_file(const void *expected, size_t length)
{
	unsigned char bytes[32];
	FILE *existing = fopen(destination, "rb");
	assert(existing);
	assert(fread(bytes, 1, sizeof bytes, existing) == length);
	assert(!memcmp(bytes, expected, length));
	assert(fclose(existing) == 0);
}

static void check_growth(size_t initial_size)
{
	reset(NO_FAULT);
	attr_size = initial_size;
	assert(cmd_get(NULL, "/output-test.tns", destination) == 1);
	assert(read_calls == 1 && open_calls == 0 && !success[0] && diagnostic[0]);
	expect_file("retain", 6);
}

static void check_get(enum output_fault selected)
{
	reset(selected);
	assert(cmd_get(NULL, "/output-test.tns", destination) == (selected != NO_FAULT));
	assert(read_calls == 1 && open_calls == 1);
	if (selected == NO_FAULT) {
		assert(success[0] && !diagnostic[0]);
		expect_file(file_bytes, sizeof file_bytes);
	} else {
		assert(!success[0] && diagnostic[0]);
		assert(close_calls == (selected != OPEN_FAULT));
	}
}

static void check_screenshot(enum output_fault selected, unsigned bpp)
{
	reset(selected);
	screenshot_bpp = bpp;
	assert(cmd_screenshot(NULL, destination) == (selected != NO_FAULT));
	assert(capture_calls == 1 && open_calls == 1);
	if (selected == NO_FAULT) {
		assert(success[0] && !diagnostic[0]);
		const unsigned char rgb[] = "P6\n1 1\n255\n\0\377\0";
		const unsigned char grey[] = "P6\n1 1\n255\n\177\177\177";
		expect_file(bpp == 16 ? rgb : grey, sizeof rgb - 1);
	} else {
		assert(!success[0] && diagnostic[0]);
		assert(close_calls == (selected != OPEN_FAULT));
	}
}

int main(int argc, char **argv)
{
	int descriptor = mkstemp(destination);
	assert(descriptor >= 0 && close(descriptor) == 0);
	if (argc == 2) {
		if (!strcmp(argv[1], "growth"))
			check_growth(4);
		else if (!strcmp(argv[1], "get-close"))
			check_get(CLOSE_FAULT);
		else if (!strcmp(argv[1], "screen-header"))
			check_screenshot(HEADER_FAULT, 16);
		else
			assert(!"unknown reproduction");
		return 0;
	}
	check_growth(4);
	check_growth(0);
	check_get(NO_FAULT);
	check_get(OPEN_FAULT);
	check_get(WRITE_FAULT);
	check_get(CLOSE_FAULT);
	for (unsigned bpp = 8; bpp <= 16; bpp += 8) {
		check_screenshot(NO_FAULT, bpp);
		check_screenshot(OPEN_FAULT, bpp);
		check_screenshot(HEADER_FAULT, bpp);
		check_screenshot(PIXEL_FAULT, bpp);
		check_screenshot(CLOSE_FAULT, bpp);
	}
	puts("nsptool output: growth refusal, output bytes, write faults and buffered close failures passed");
	return 0;
}
