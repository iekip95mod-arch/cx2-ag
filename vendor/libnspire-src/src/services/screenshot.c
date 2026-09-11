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

#include "handle.h"
#include "error.h"
#include "data.h"
#include "service.h"
#include "screenshot.h"

static int rle_decode(const uint8_t bpp, const uint8_t *in, uint8_t *out,
		size_t in_size, size_t out_size) {
	const size_t rle_size = bpp / 4;

	while (in_size && out_size) {
		int len = (int8_t)*in++;
		in_size--;
		size_t bytes = (len < 0 ? -len + 1 : len + 1) * rle_size;
		if (bytes > out_size)
			return -NSPIRE_ERR_INVALID;
		if (len < 0) {
			if (bytes > in_size)
				return -NSPIRE_ERR_INVALID;
			memcpy(out, in, bytes);
			in_size -= bytes;
			in += bytes;
			out += bytes;
		} else {
			if (in_size < rle_size)
				return -NSPIRE_ERR_INVALID;

			for (int i = 0; i < len + 1; ++i) {
				memcpy(out, in, rle_size);
				out += rle_size;
			}
			in_size -= rle_size;
			in += rle_size;
		}
		out_size -= bytes;
	}
	return in_size || out_size ? -NSPIRE_ERR_INVALID : NSPIRE_ERR_SUCCESS;
}

int nspire_screenshot(nspire_handle_t *handle, struct nspire_image **ptr) {
	int ret;
	size_t len, in_len, out_len;
	uint8_t buffer[packet_max_datasize(handle)], bpp, *tmp = NULL, *tmp_ptr = NULL;
	uint16_t width, height;
	uint32_t size;
	struct nspire_image *i = NULL;
	*ptr = NULL;

	if ( (ret = service_connect(handle, 0x4024)) )
		return ret;

	if ( (ret = data_write8(handle, 0x00)) )
		goto end;

	if ( (ret = data_read(handle, buffer, sizeof(buffer), &len)) )
		goto end;
	if (len > sizeof buffer) {
		ret = -NSPIRE_ERR_INVALID;
		goto end;
	}

	if ( (ret = data_scan("bwhhhhbb", buffer, len,
			NULL, &size, NULL, NULL, &width, &height, &bpp, NULL)) )
		goto end;

	if (!width || !height || !size || (bpp != 4 && bpp != 8 && bpp != 16) ||
			(size_t)width > SIZE_MAX / height || (size_t)width * height > SIZE_MAX / bpp) {
		ret = -NSPIRE_ERR_INVALID;
		goto end;
	}
	out_len = (size_t)width * height * bpp;
	if (out_len % 8) {
		ret = -NSPIRE_ERR_INVALID;
		goto end;
	}
	out_len /= 8;
	const size_t rle_size = bpp / 4;
	if (out_len % rle_size || out_len > SIZE_MAX - sizeof(*i) ||
			size > out_len + out_len / rle_size ||
			size / (rle_size + 1) < (out_len / rle_size + 127) / 128) {
		ret = -NSPIRE_ERR_INVALID;
		goto end;
	}

	tmp_ptr = tmp = malloc(size);
	if (!tmp) {
		ret = -NSPIRE_ERR_NOMEM;
		goto end;
	}

	in_len = size;

	i = malloc(sizeof(*i) + out_len);
	if (!i) {
		ret = -NSPIRE_ERR_NOMEM;
		goto end;
	}

	i->width = width;
	i->height = height;
	i->bpp = bpp;

	while (size) {
		if ( (ret = data_read(handle, buffer, sizeof(buffer), &len)) )
			goto end;

		if (len <= 1 || len > sizeof buffer || len - 1 > size) {
			ret = -NSPIRE_ERR_INVALID;
			goto end;
		}
		len--;
		memcpy(tmp_ptr, buffer + 1, len);
		tmp_ptr += len;
		size -= len;
	}

	if ( (ret = rle_decode(bpp, tmp, i->data, in_len, out_len)) )
		goto end;
	ret = NSPIRE_ERR_SUCCESS;
end:
	if (tmp) free(tmp);
	ret = service_finish(handle, ret);
	if (ret) free(i);
	else *ptr = i;
	return ret;
}
