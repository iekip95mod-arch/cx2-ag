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

#include <string.h>

#include "handle.h"
#include "error.h"
#include "data.h"
#include "service.h"

int nspire_file_write(nspire_handle_t *handle, const char *path,
		void* data, size_t size) {
	int ret;
	size_t len;
	uint8_t buffer[sizeof(struct packet)], *ptr = data;
	uint16_t result;
	const size_t total = size;
	const char *phase = "build";
	if (size > UINT32_MAX)
		return -NSPIRE_ERR_INVALID;

	if ( (ret = service_connect(handle, 0x4060)) )
		return ret;

	if ( (ret = data_build("hsw", buffer, sizeof(buffer), &len,
			0x0301, path, (uint32_t)size)) )
		goto end;

	phase = "request";
	nspire_trace("file operation=write phase=%s offset=0 total=%zu requested=%zu", phase, total, len);
	if ( (ret = data_write(handle, buffer, len)) )
		goto end;

	phase = "ready";
	nspire_trace("file operation=write phase=%s offset=0 total=%zu", phase, total);
	if ( (ret = data_read(handle, buffer, sizeof(buffer), &len)) )
		goto end;

	if (!len || buffer[0] != 0x04) {
		ret = -NSPIRE_ERR_INVALID;
		goto end;
	}

	size_t datasize = packet_max_datasize(handle) - 1;

	buffer[0] = 0x05;
	while (size) {
		len = (datasize < size) ? datasize : size;

		phase = "chunk";
		nspire_trace("file operation=write phase=%s offset=%zu total=%zu requested=%zu", phase, total-size, total, len);
		memcpy(buffer + 1, ptr, len);
		if ( (ret = data_write(handle, buffer, len+1)) )
			goto end;

		size -= len;
		ptr += len;
	}

	phase = "final-status";
	nspire_trace("file operation=write phase=%s offset=%zu total=%zu", phase, total-size, total);
	if ( (ret = data_read(handle, buffer, sizeof(buffer), &len)) )
		goto end;

	if ( (ret = data_scan("h", buffer, len, &result)) )
		goto end;

	nspire_trace("file operation=write phase=%s remote_status=%u", phase, (unsigned)result);
	ret = (result == 0xFF00) ? NSPIRE_ERR_SUCCESS : -NSPIRE_ERR_NONEXIST;
end:
	nspire_trace("file operation=write phase=%s event=end offset=%zu total=%zu status=%d", phase, total-size, total, ret);
	return service_finish(handle, ret);
}

int nspire_file_read(nspire_handle_t *handle, const char *path,
		void* data, size_t size, size_t *total_bytes) {
	int ret;
	size_t len;
	uint8_t buffer[packet_max_datasize(handle)], *ptr = data;
	uint16_t result;
	uint32_t data_len;
	size_t offset = 0;
	const char *phase = "build";
	if (total_bytes) *total_bytes = 0;

	if ( (ret = service_connect(handle, 0x4060)) )
		return ret;

	if ( (ret = data_build("hs", buffer, sizeof(buffer), &len,
			0x0701, path)) )
		goto end;

	phase = "request";
	nspire_trace("file operation=read phase=%s offset=%zu requested=%zu", phase, offset, len);
	if ( (ret = data_write(handle, buffer, len)) )
		goto end;

	phase = "header";
	nspire_trace("file operation=read phase=%s offset=%zu", phase, offset);
	if ( (ret = data_read(handle, buffer, sizeof(buffer), &len)) )
		goto end;

	if ( (ret = data_scan("h", buffer, len, &result)) )
		goto end;

	if (result != 0x0301) {
		ret = -NSPIRE_ERR_NONEXIST;
		goto end;
	}
	if ( (ret = data_scan("h000000000w", buffer, len, NULL, &data_len)) )
		goto end;

	phase = "ready";
	nspire_trace("file operation=read phase=%s offset=%zu total=%u", phase, offset, data_len);
	if ( (ret = data_write8(handle, 0x04)) )
		goto end;

	while (data_len) {
		phase = "chunk";
		nspire_trace("file operation=read phase=%s offset=%zu remaining=%u", phase, offset, data_len);
		if ( (ret = data_read(handle, buffer, sizeof(buffer), &len)) )
			goto end;

		if (len <= 1 || buffer[0] != 0x05 || len - 1 > data_len) {
			ret = -NSPIRE_ERR_INVALPKT;
			goto end;
		}
		len--;

		/* Bytes past the caller's buffer are counted, not stored. */
		if (size) {
			size_t to_copy = (len < size) ? len : size;
			memcpy(ptr, buffer + 1, to_copy);
			size -= to_copy;

			ptr += to_copy;
		}

		if (total_bytes) *total_bytes += len;
		offset += len;
		data_len -= len;
	}

	phase = "final-status";
	nspire_trace("file operation=read phase=%s offset=%zu", phase, offset);
	if ( (ret = data_write16(handle, 0xFF00)) )
		goto end;

	ret = NSPIRE_ERR_SUCCESS;
end:
	nspire_trace("file operation=read phase=%s event=end offset=%zu status=%d", phase, offset, ret);
	return service_finish(handle, ret);
}

int nspire_file_move(nspire_handle_t *handle,
		const char *src, const char *dst) {
	int ret;
	size_t len;
	uint16_t result;
	uint8_t buffer[254];


	if ( (ret = service_connect(handle, 0x4060)) )
		return ret;

	if ( (ret = data_build("hss0", buffer, sizeof(buffer), &len,
			0x2101, src, dst)) )
		goto end;

	if ( (ret = data_write(handle, &buffer, len)) )
		goto end;

	if ( (ret = data_read(handle, &buffer, 2, &len)) )
		goto end;

	if ( (ret = data_scan("h", buffer, len, &result)) )
		goto end;

	ret = (result == 0xFF00) ? NSPIRE_ERR_SUCCESS : -NSPIRE_ERR_INVALID;
end:
	return service_finish(handle, ret);
}

int nspire_file_copy(nspire_handle_t *handle,
		const char *src, const char *dst) {
	int ret;
	size_t len;
	uint16_t result;
	uint8_t buffer[254];


	if ( (ret = service_connect(handle, 0x4060)) )
		return ret;

	if ( (ret = data_build("hss0", buffer, sizeof(buffer), &len,
			0x0C01, src, dst)) )
		goto end;

	if ( (ret = data_write(handle, &buffer, len)) )
		goto end;

	if ( (ret = data_read(handle, &buffer, 2, &len)) )
		goto end;

	if ( (ret = data_scan("h", buffer, len, &result)) )
		goto end;

	ret = (result == 0xFF00) ? NSPIRE_ERR_SUCCESS : -NSPIRE_ERR_INVALID;
end:
	return service_finish(handle, ret);
}

int nspire_file_delete(nspire_handle_t *handle, const char *path) {
	int ret;
	size_t len;
	uint16_t result;
	uint8_t buffer[254];


	if ( (ret = service_connect(handle, 0x4060)) )
		return ret;

	if ( (ret = data_build("hs0", buffer, sizeof(buffer), &len,
			0x0901, path)) )
		goto end;

	if ( (ret = data_write(handle, &buffer, len)) )
		goto end;

	if ( (ret = data_read(handle, &buffer, 2, &len)) )
		goto end;

	if ( (ret = data_scan("h", buffer, len, &result)) )
		goto end;

	ret = (result == 0xFF00) ? NSPIRE_ERR_SUCCESS : -NSPIRE_ERR_EXISTS;
end:
	return service_finish(handle, ret);
}
