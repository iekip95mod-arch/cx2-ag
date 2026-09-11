/*
    This file is part of libnspire.

    libnspire is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libnspire is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libnspire.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <libusb.h>
#include <string.h>

#include "usb.h"
#include "error.h"

#define NSP_DEFAULT_CONFIG 1
#define NSP_DEFAULT_IFACE 0
#define NSP_TIMEOUT 10000

static libusb_context * usb_ctx = NULL;

int usb_init() {
	if (usb_ctx)
		return NSPIRE_ERR_SUCCESS;

	if (libusb_init(&usb_ctx))
		return -NSPIRE_ERR_LIBUSB;

	//libusb_set_debug(usb_ctx, 3);

	return NSPIRE_ERR_SUCCESS;
}

void usb_finish() {
	libusb_exit(usb_ctx);
	usb_ctx = NULL;
}

int usb_error(int error) {
	switch (error) {
	case 0:				return NSPIRE_ERR_SUCCESS;
	case LIBUSB_ERROR_ACCESS:	return -NSPIRE_ERR_ACCESS;
	case LIBUSB_ERROR_BUSY:		return -NSPIRE_ERR_USB_BUSY;
	case LIBUSB_ERROR_NO_DEVICE:	return -NSPIRE_ERR_DISCONNECTED;
	case LIBUSB_ERROR_TIMEOUT:	return -NSPIRE_ERR_TIMEOUT;
	case LIBUSB_ERROR_NO_MEM:	return -NSPIRE_ERR_NOMEM;
	default:			return -NSPIRE_ERR_LIBUSB;
	}
}

struct usb_location {
	uint8_t bus;
	uint8_t ports[7];
	int length;
};

static int usb_open_device(libusb_device_handle **dev, uint16_t vid, uint16_t pid,
		const struct usb_location *location) {
	libusb_device **devices;
	ssize_t count = libusb_get_device_list(usb_ctx, &devices);
	int ret = -NSPIRE_ERR_NODEVICE;
	if (count < 0)
		return usb_error(count);
	for (ssize_t i = 0; i < count; i++) {
		struct libusb_device_descriptor descriptor;
		int status = libusb_get_device_descriptor(devices[i], &descriptor);
		if (status) {
			ret = usb_error(status);
			continue;
		}
		if (descriptor.idVendor != vid || descriptor.idProduct != pid)
			continue;
		if (location) {
			uint8_t ports[7];
			int length = libusb_get_port_numbers(devices[i], ports, sizeof(ports));
			if (libusb_get_bus_number(devices[i]) != location->bus
					|| length != location->length || length <= 0
					|| memcmp(ports, location->ports, length))
				continue;
		}
		ret = usb_error(libusb_open(devices[i], dev));
		break;
	}
	libusb_free_device_list(devices, 1);
	return ret;
}

static int usb_configure(libusb_device_handle *dev, int interface_only) {
	int ret = libusb_kernel_driver_active(dev, NSP_DEFAULT_IFACE);
	if (interface_only && ret != 0)
		return ret < 0 ? usb_error(ret) : -NSPIRE_ERR_USB_BUSY;
	/* IOKit attaches a driver to every interface, so nothing can be claimed until it is
	 * detached. On macOS that path needs root, or the com.apple.vm.device-access entitlement.
	 * A no-op where libusb does not implement detaching. */
	if (ret == 1) {
		ret = libusb_detach_kernel_driver(dev, NSP_DEFAULT_IFACE);
		if (ret && ret != LIBUSB_ERROR_NOT_FOUND)
			return usb_error(ret);
	} else if (ret < 0 && ret != LIBUSB_ERROR_NOT_SUPPORTED)
		return usb_error(ret);

	/* Setting a configuration the device is already in fails with NO_DEVICE on macOS and can
	 * invalidate the handle, so only set it when it actually differs. */
	int current = 0;
	ret = libusb_get_configuration(dev, &current);
	if (ret)
		return usb_error(ret);
	if (current != NSP_DEFAULT_CONFIG)
		return interface_only ? -NSPIRE_ERR_USB_CONFIG :
			usb_error(libusb_set_configuration(dev, NSP_DEFAULT_CONFIG));
	return NSPIRE_ERR_SUCCESS;
}

static int usb_acquire_device(usb_device_t *handle, uint16_t vid, uint16_t pid,
		libusb_device *original) {
	struct libusb_config_descriptor *config;
	const struct libusb_interface_descriptor *iface;
	libusb_device_handle *dev = NULL;
	struct usb_location location;
	int interface_only = 0;
#ifdef __APPLE__
	/* Reinitialize CX II endpoints without USB reenumeration. */
	interface_only = pid == NSP_PID_CX2;
#endif
	uint8_t alternate = 0;
	int claimed = 0;
	int ret = original ? usb_error(libusb_open(original, &dev)) :
		usb_open_device(&dev, vid, pid, NULL);
	memset(handle, 0, sizeof(*handle));
	if (ret)
		return ret;
	location.bus = libusb_get_bus_number(libusb_get_device(dev));
	location.length = libusb_get_port_numbers(libusb_get_device(dev),
		location.ports, sizeof(location.ports));
	ret = usb_configure(dev, interface_only);
	if (ret)
		goto error_close;

	if (!interface_only) {
		ret = libusb_reset_device(dev);
		if (ret == LIBUSB_ERROR_NOT_FOUND || ret == LIBUSB_ERROR_NO_DEVICE) {
			libusb_close(dev);
			dev = NULL;
			if (original || location.length <= 0)
				return -NSPIRE_ERR_DISCONNECTED;
			for (int attempt = 0; attempt < 20; attempt++) {
				ret = usb_open_device(&dev, vid, pid, &location);
				if (ret != -NSPIRE_ERR_NODEVICE && ret != -NSPIRE_ERR_DISCONNECTED)
					break;
				struct timeval delay = {0, 100000};
				int status = libusb_handle_events_timeout(usb_ctx, &delay);
				if (status && status != LIBUSB_ERROR_INTERRUPTED)
					return usb_error(status);
			}
			if (ret)
				return ret == -NSPIRE_ERR_NODEVICE ? -NSPIRE_ERR_DISCONNECTED : ret;
			ret = usb_configure(dev, 0);
			if (ret)
				goto error_close;
		} else if (ret) {
			ret = usb_error(ret);
			goto error_close;
		}
	}

	ret = usb_error(libusb_claim_interface(dev, NSP_DEFAULT_IFACE));
	if (ret)
		goto error_close;
	claimed = 1;
	if (interface_only) {
		ret = libusb_control_transfer(dev,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_INTERFACE,
			LIBUSB_REQUEST_GET_INTERFACE, 0, NSP_DEFAULT_IFACE, &alternate, 1, 1000);
		if (ret != 1) {
			ret = ret < 0 ? usb_error(ret) : -NSPIRE_ERR_INVALPKT;
			goto error_close;
		}
		ret = usb_error(libusb_set_interface_alt_setting(dev, NSP_DEFAULT_IFACE, alternate));
		if (ret)
			goto error_close;
	}

	/* Find and use first found input and output endpoints */
	/* We can't hardcode or else it won't work in recovery mode */
	ret = usb_error(libusb_get_active_config_descriptor(libusb_get_device(dev), &config));
	if (ret)
		goto error_close;
	ret = -NSPIRE_ERR_LIBUSB;
	if (config->bNumInterfaces <= NSP_DEFAULT_IFACE
			|| config->interface[NSP_DEFAULT_IFACE].num_altsetting < 1)
		goto error_free_desc;
	iface = config->interface[NSP_DEFAULT_IFACE].altsetting;
	if (interface_only) {
		iface = NULL;
		for (int setting = 0; setting < config->interface[NSP_DEFAULT_IFACE].num_altsetting; setting++) {
			const struct libusb_interface_descriptor *candidate =
				&config->interface[NSP_DEFAULT_IFACE].altsetting[setting];
			if (candidate->bAlternateSetting == alternate) {
				iface = candidate;
				break;
			}
		}
		if (!iface)
			goto error_free_desc;
	}

	handle->ep_in = 0;
	handle->ep_out = 0;
	for (int i=0; i<iface->bNumEndpoints; i++) {
		if ((iface->endpoint[i].bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)
				!= LIBUSB_TRANSFER_TYPE_BULK)
			continue;
		unsigned char ep = iface->endpoint[i].bEndpointAddress;
		if (ep & LIBUSB_ENDPOINT_IN) {
			if (!handle->ep_in) handle->ep_in = ep;
		} else {
			if (!handle->ep_out) handle->ep_out = ep;
		}
	}
	libusb_free_config_descriptor(config);

	if (!handle->ep_in || !handle->ep_out)
		goto error_close;

	handle->dev = dev;
	return NSPIRE_ERR_SUCCESS;
error_free_desc:
	libusb_free_config_descriptor(config);
error_close:
	if (claimed)
		libusb_release_interface(dev, NSP_DEFAULT_IFACE);
	libusb_close(dev);
	return ret;
}

int usb_get_device(usb_device_t *handle, uint16_t vid, uint16_t pid) {
	return usb_acquire_device(handle, vid, pid, NULL);
}

int usb_reopen_device(usb_device_t *handle) {
	if (!handle->dev)
		return -NSPIRE_ERR_DISCONNECTED;
	libusb_device *original = libusb_ref_device(libusb_get_device(handle->dev));
	struct libusb_device_descriptor descriptor;
	int ret = usb_error(libusb_get_device_descriptor(original, &descriptor));
	usb_free_device(handle);
	memset(handle, 0, sizeof(*handle));
	if (!ret)
		ret = usb_acquire_device(handle, descriptor.idVendor, descriptor.idProduct, original);
	libusb_unref_device(original);
	return ret;
}

void usb_free_device(usb_device_t *handle) {
	libusb_release_interface(handle->dev, NSP_DEFAULT_IFACE);
	libusb_close(handle->dev);
}

static inline int usb_xfer(libusb_device_handle *handle, unsigned char ep,
		void *ptr, int len) {
	int ret, transferred = 0;
	ret = libusb_bulk_transfer(handle,
		ep,
		ptr,
		len,
		&transferred,
		NSP_TIMEOUT);

	if (ret)
		return usb_error(ret);
	if (transferred <= 0 && len > 0)
		return -NSPIRE_ERR_TIMEOUT;
	return len - transferred;
}

int usb_write(usb_device_t *handle, void *ptr, int len) {
	return usb_xfer(handle->dev, handle->ep_out, ptr, len);
}

int usb_read(usb_device_t *handle, void *ptr, int len) {
	return usb_xfer(handle->dev, handle->ep_in, ptr, len);
}
