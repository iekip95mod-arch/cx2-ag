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

#include <stddef.h>

#include "error.h"

static const char *error_msg[NSPIRE_ERR_MAX] = {
	[NSPIRE_ERR_SUCCESS]	= "No error",
	[NSPIRE_ERR_TIMEOUT]	= "Timeout",
	[NSPIRE_ERR_NOMEM]	= "Out of memory",
	[NSPIRE_ERR_LIBUSB]	= "Libusb error",
	[NSPIRE_ERR_NODEVICE]	= "No device found",
	[NSPIRE_ERR_INVALPKT]	= "Invalid packet received",
	[NSPIRE_ERR_NACK]	= "NACK received",
	[NSPIRE_ERR_BUSY]	= "Busy",
	[NSPIRE_ERR_INVALID]	= "Invalid input",
	[NSPIRE_ERR_EXISTS]	= "Already exists",
	[NSPIRE_ERR_NONEXIST]	= "Path does not exist",
	[NSPIRE_ERR_OSFAILED]	= "OS installation failed",
	[NSPIRE_ERR_ACCESS]	= "USB access denied",
	[NSPIRE_ERR_USB_BUSY]	= "USB interface busy",
	[NSPIRE_ERR_DISCONNECTED]	= "USB device disconnected",
	[NSPIRE_ERR_USB_CONFIG]	= "USB configuration unavailable",

};

static const char* unknown_err = "Unknown error";

const char *nspire_strerror(int error) {
	if (error > 0 || error <= -NSPIRE_ERR_MAX)
		return unknown_err;

	return error_msg[-error];
}
