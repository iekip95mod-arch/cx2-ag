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

#include <stdlib.h>
#include <string.h>

#include "packet.h"
#include "error.h"
#include "usb.h"

static int connect_handle(nspire_handle_t *h, int reconnect) {
	int ret;
	struct packet p;

	if ( (ret = usb_init()) )
		goto error;

	if (reconnect) {
		if ( (ret = usb_reopen_device(&h->device)) )
			goto error;
	} else if ( (ret = usb_get_device(&h->device, NSP_VID, h->is_cx2 ? NSP_PID_CX2 : NSP_PID)) ) {
		if (h->is_cx2 || ret != -NSPIRE_ERR_NODEVICE)
			goto error;
		h->is_cx2 = true;
		if ( (ret = usb_get_device(&h->device, NSP_VID, NSP_PID_CX2)) )
			goto error;
	}

	h->host_addr = 0x6400;
	h->device_addr = 0x6401;
	h->host_sid = 0x4003;
	h->device_sid = 0x4003;
	h->connected = 0;
	h->seq = 1;
	h->cx2_handshake_complete = false;

	if (!h->is_cx2) {
		// Wait for an address request
		if ( (ret = packet_recv(h, NULL)) )
			goto error_free_usb;
	}

	p = packet_new(h);
	packet_set_data(p, 0x64, 0x01, 0xFF, 0x00);
	if ( (ret = packet_send(h, p)) )
		goto error_free_usb;

	h->host_sid = 0x8000;

	return NSPIRE_ERR_SUCCESS;

error_free_usb:
	usb_free_device(&h->device);
	h->device.dev = NULL;
error:
	return ret;
}

int nspire_init(nspire_handle_t **ptr) {
	nspire_handle_t *h = calloc(1, sizeof(*h));
	if (!h)
		return -NSPIRE_ERR_NOMEM;
	int ret = connect_handle(h, 0);
	if (ret) {
		free(h);
		return ret;
	}
	*ptr = h;
	return NSPIRE_ERR_SUCCESS;
}

int nspire_reconnect(nspire_handle_t *h) {
	return connect_handle(h, 1);
}

void nspire_free(nspire_handle_t *ptr) {
	if (ptr->device.dev)
		usb_free_device(&ptr->device);
	free(ptr);
}
