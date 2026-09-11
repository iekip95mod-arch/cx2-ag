#include <assert.h>
#include <stdio.h>
#include <string.h>

static int probe_printf(const char *format, ...);

#define printf probe_printf
#define main usbprobe_main
#include "../usbprobe.c"
#undef main
#undef printf

struct libusb_context { int live; };
struct libusb_device_handle { int open; };

static struct libusb_context context;
static struct libusb_device_handle handle;
static libusb_device *empty_devices[1];
static ssize_t enumeration;
static int init_error, open_error, detach_error, claim_error, attach_error;
static unsigned free_calls, exit_calls, configuration_calls, reset_calls;
static char operations[64];
static size_t operation_count;
static unsigned cases, failures;

static int probe_printf(const char *format, ...)
{
	(void)format;
	return 0;
}

static void record(char operation)
{
	assert(operation_count + 1 < sizeof operations);
	operations[operation_count++] = operation;
	operations[operation_count] = '\0';
}

int libusb_init(libusb_context **ctx)
{
	if (init_error)
		return init_error;
	*ctx = &context;
	return 0;
}

void libusb_exit(libusb_context *ctx)
{
	assert(ctx == &context);
	exit_calls++;
}

const char *libusb_strerror(int error)
{
	(void)error;
	return "mock USB failure";
}

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list)
{
	assert(ctx == &context);
	if (enumeration >= 0)
		*list = empty_devices;
	return enumeration;
}

void libusb_free_device_list(libusb_device **list, int unref_devices)
{
	(void)list;
	assert(unref_devices == 1);
	free_calls++;
}

libusb_device_handle *libusb_open_device_with_vid_pid(libusb_context *ctx,
						    uint16_t vendor, uint16_t product)
{
	assert(ctx == &context && vendor == VID && product == PID_CX2);
	record('o');
	return open_error ? NULL : &handle;
}

void libusb_close(libusb_device_handle *dev)
{
	assert(dev == &handle);
	record('x');
}

int libusb_kernel_driver_active(libusb_device_handle *dev, int interface_number)
{
	assert(dev == &handle && interface_number == 0);
	return 1;
}

int libusb_detach_kernel_driver(libusb_device_handle *dev, int interface_number)
{
	assert(dev == &handle && interface_number == 0);
	record('d');
	return detach_error;
}

int libusb_attach_kernel_driver(libusb_device_handle *dev, int interface_number)
{
	assert(dev == &handle && interface_number == 0);
	record('a');
	return attach_error;
}

int libusb_claim_interface(libusb_device_handle *dev, int interface_number)
{
	assert(dev == &handle && interface_number == 0);
	record('c');
	return claim_error;
}

int libusb_release_interface(libusb_device_handle *dev, int interface_number)
{
	assert(dev == &handle && interface_number == 0);
	record('r');
	return 0;
}

int libusb_set_configuration(libusb_device_handle *dev, int configuration)
{
	assert(dev == &handle && configuration == 1);
	configuration_calls++;
	return 0;
}

int libusb_reset_device(libusb_device_handle *dev)
{
	assert(dev == &handle);
	reset_calls++;
	return 0;
}

int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc)
{
	(void)dev;
	(void)desc;
	assert(0);
	return LIBUSB_ERROR_IO;
}

int libusb_open(libusb_device *dev, libusb_device_handle **opened)
{
	(void)dev;
	(void)opened;
	assert(0);
	return LIBUSB_ERROR_IO;
}

int libusb_get_configuration(libusb_device_handle *dev, int *configuration)
{
	(void)dev;
	(void)configuration;
	assert(0);
	return LIBUSB_ERROR_IO;
}

int libusb_get_active_config_descriptor(libusb_device *dev,
					struct libusb_config_descriptor **config)
{
	(void)dev;
	(void)config;
	assert(0);
	return LIBUSB_ERROR_IO;
}

void libusb_free_config_descriptor(struct libusb_config_descriptor *config)
{
	(void)config;
	assert(0);
}

static void reset_probe(void)
{
	enumeration = 0;
	init_error = open_error = detach_error = claim_error = attach_error = 0;
	free_calls = exit_calls = configuration_calls = reset_calls = 0;
	operation_count = 0;
	operations[0] = '\0';
}

static void check(const char *name, int passed)
{
	cases++;
	if (!passed) {
		failures++;
		fprintf(stderr, "FAIL: %s\n", name);
	}
}

int main(void)
{
	reset_probe();
	enumeration = LIBUSB_ERROR_IO;
	describe(&context);
	check("failed enumeration does not free an unassigned list", free_calls == 0);

	reset_probe();
	describe(&context);
	check("empty successful enumeration releases its list", free_calls == 1);

	reset_probe();
	kernel_driver(&context);
	check("successful claim releases before restoring the driver", !strcmp(operations, "odcrax"));

	reset_probe();
	claim_error = LIBUSB_ERROR_BUSY;
	kernel_driver(&context);
	check("failed claim still restores the detached driver", !strcmp(operations, "odcax"));

	reset_probe();
	detach_error = LIBUSB_ERROR_NOT_SUPPORTED;
	kernel_driver(&context);
	check("failed detach does not attach a driver", !strcmp(operations, "odx"));

	reset_probe();
	attach_error = LIBUSB_ERROR_IO;
	int status = usbprobe_main();
	check("failed restoration after a successful claim stops mutations and cleans up",
	      status == 1 && exit_calls == 1 && configuration_calls == 0 && reset_calls == 0 &&
	      !strcmp(operations, "ocrxodcrax"));

	reset_probe();
	claim_error = LIBUSB_ERROR_BUSY;
	attach_error = LIBUSB_ERROR_IO;
	status = usbprobe_main();
	check("failed restoration after a failed claim stops mutations and cleans up",
	      status == 1 && exit_calls == 1 && configuration_calls == 0 && reset_calls == 0 &&
	      !strcmp(operations, "ocxodcax"));

	reset_probe();
	claim_error = LIBUSB_ERROR_BUSY;
	status = usbprobe_main();
	check("ordinary claim failures retain exit zero and continue after restoration",
	      status == 0 && exit_calls == 1 && configuration_calls == 1 && reset_calls == 1);

	reset_probe();
	init_error = LIBUSB_ERROR_IO;
	status = usbprobe_main();
	check("failed initialization exits before probing", status == 1 && exit_calls == 0 && operation_count == 0);

	printf("usbprobe: %u/%u cases passed\n", cases - failures, cases);
	return failures ? 1 : 0;
}
