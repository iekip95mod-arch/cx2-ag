// usbprobe: which libusb step actually fails, and whether the earlier steps are what break it.
//
// libnspire's usb_get_device funnels open, set_configuration, reset_device and claim_interface into
// one NSPIRE_ERR_NODEVICE, so "No device found" names four possible causes and distinguishes none.
//
// The first version of this probe ran the steps in libnspire's order and reported EACCES on the
// claim. That reading was not safe: set_configuration had already failed with NO_DEVICE, which can
// invalidate the handle, so the claim may have been made against a dead handle and its error means
// nothing. Each sequence below therefore opens its own handle, so every result stands alone.

#include <stdio.h>
#include <libusb-1.0/libusb.h>

#define VID 0x0451
#define PID_CX2 0xe022

static libusb_device_handle *fresh(libusb_context *ctx, const char *label)
{
	libusb_device_handle *dev = libusb_open_device_with_vid_pid(ctx, VID, PID_CX2);
	printf("\n[%s]\n", label);
	if (!dev)
		printf("  open: FAILED\n");
	else
		printf("  open: ok\n");
	return dev;
}

static void claim_only(libusb_context *ctx)
{
	libusb_device_handle *dev = fresh(ctx, "claim straight after open, nothing else");
	if (!dev)
		return;
	int rc = libusb_claim_interface(dev, 0);
	printf("  claim_interface(0): %s\n", rc ? libusb_strerror(rc) : "ok");
	if (!rc)
		libusb_release_interface(dev, 0);
	libusb_close(dev);
}

static int kernel_driver(libusb_context *ctx)
{
	libusb_device_handle *dev = fresh(ctx, "is a kernel driver holding interface 0");
	if (!dev)
		return 0;
	int active = libusb_kernel_driver_active(dev, 0);
	if (active < 0)
		printf("  kernel_driver_active: %s\n", libusb_strerror(active));
	else
		printf("  kernel_driver_active: %d\n", active);

	int rc = libusb_detach_kernel_driver(dev, 0);
	printf("  detach_kernel_driver: %s\n", rc ? libusb_strerror(rc) : "ok");
	int restore_error = 0;
	if (!rc) {
		rc = libusb_claim_interface(dev, 0);
		printf("  claim after detach: %s\n", rc ? libusb_strerror(rc) : "ok");
		if (!rc)
			libusb_release_interface(dev, 0);
		restore_error = libusb_attach_kernel_driver(dev, 0);
		printf("  attach_kernel_driver: %s\n", restore_error ? libusb_strerror(restore_error) : "ok");
	}
	libusb_close(dev);
	return restore_error;
}

static void libnspire_order(libusb_context *ctx)
{
	libusb_device_handle *dev = fresh(ctx, "libnspire's own order");
	if (!dev)
		return;
	int rc = libusb_set_configuration(dev, 1);
	printf("  set_configuration(1): %s\n", rc ? libusb_strerror(rc) : "ok");
	rc = libusb_reset_device(dev);
	printf("  reset_device: %s\n", rc ? libusb_strerror(rc) : "ok");
	rc = libusb_claim_interface(dev, 0);
	printf("  claim_interface(0): %s\n", rc ? libusb_strerror(rc) : "ok");
	if (!rc)
		libusb_release_interface(dev, 0);
	libusb_close(dev);
}

static void describe(libusb_context *ctx)
{
	libusb_device **list;
	ssize_t count = libusb_get_device_list(ctx, &list);
	if (count < 0) {
		printf("libusb_get_device_list: %s\n", libusb_strerror(count));
		return;
	}
	printf("libusb sees %zd device(s)\n", count);

	for (ssize_t i = 0; i < count; i++) {
		struct libusb_device_descriptor desc;
		if (libusb_get_device_descriptor(list[i], &desc) || desc.idVendor != VID)
			continue;
		printf("TI device vid 0x%04x pid 0x%04x, %u configuration(s)\n",
		       desc.idVendor, desc.idProduct, desc.bNumConfigurations);

		int current = -1;
		libusb_device_handle *dev;
		if (!libusb_open(list[i], &dev)) {
			if (libusb_get_configuration(dev, &current))
				current = -1;
			libusb_close(dev);
		}
		printf("  active configuration: %d\n", current);

		struct libusb_config_descriptor *config;
		if (!libusb_get_active_config_descriptor(list[i], &config)) {
			printf("  bConfigurationValue: %u, interfaces: %u\n",
			       config->bConfigurationValue, config->bNumInterfaces);
			for (int n = 0; n < config->bNumInterfaces; n++) {
				const struct libusb_interface_descriptor *iface =
					config->interface[n].altsetting;
				printf("    iface %d: class 0x%02x subclass 0x%02x proto 0x%02x, "
				       "%u endpoint(s)\n", n, iface->bInterfaceClass,
				       iface->bInterfaceSubClass, iface->bInterfaceProtocol,
				       iface->bNumEndpoints);
				for (int e = 0; e < iface->bNumEndpoints; e++)
					printf("      ep 0x%02x\n",
					       iface->endpoint[e].bEndpointAddress);
			}
			libusb_free_config_descriptor(config);
		} else {
			printf("  no active config descriptor\n");
		}
	}
	libusb_free_device_list(list, 1);
}

int main(void)
{
	libusb_context *ctx = NULL;
	int rc = libusb_init(&ctx);
	if (rc) {
		printf("libusb_init: %s\n", libusb_strerror(rc));
		return 1;
	}

	describe(ctx);
	claim_only(ctx);
	rc = kernel_driver(ctx);
	if (!rc)
		libnspire_order(ctx);

	libusb_exit(ctx);
	return rc ? 1 : 0;
}
