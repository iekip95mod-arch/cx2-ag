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
#include "dir.h"

static int dir_enum(nspire_handle_t *handle, struct nspire_dir_info **d) {
	int ret;
	char *name;
	uint32_t size, date;
	uint8_t is_dir;
	size_t len;
	uint16_t status;
	struct nspire_dir_info *new_dir;
	struct nspire_dir_item *current;

	unsigned char buffer[254];

	if ( (ret = data_write8(handle, 0x0E)) )
		return ret;
	if ( (ret = data_read(handle, buffer, sizeof(buffer), &len)) )
		return ret;

	if ( (ret = data_scan("h", buffer, len, &status)) )
		return ret;
	if (buffer[0] == 0xFF) {
		if (status == 0xFF11)
			return 1;
		if (status == 0xFF00)
			return NSPIRE_ERR_SUCCESS;
		return status == 0xFF0A ? -NSPIRE_ERR_NONEXIST : -NSPIRE_ERR_INVALID;
	}
	if (buffer[0] != 0x10)
		return -NSPIRE_ERR_INVALID;

	if ( (ret = data_scan("hbswwb0", buffer, len,
			NULL, NULL, &name, &size, &date, &is_dir)) )
		return ret;

	if ((*d)->num >= (SIZE_MAX - sizeof(struct nspire_dir_info)) / sizeof(struct nspire_dir_item))
		return -NSPIRE_ERR_NOMEM;
	new_dir = realloc(*d, sizeof(struct nspire_dir_info) +
			(((*d)->num + 1) * sizeof(struct nspire_dir_item)));
	if (!new_dir)
		return -NSPIRE_ERR_NOMEM;
	current = new_dir->items + new_dir->num;
	new_dir->num++;
	*d = new_dir;

	strncpy(current->name, name, sizeof(current->name));
	current->name[sizeof(current->name)-1] = '\0';
	current->size = size;
	current->date = date;
	current->type = is_dir;

	return NSPIRE_ERR_SUCCESS;
}

int nspire_dirlist(nspire_handle_t *handle, const char *path,
		struct nspire_dir_info **info_ptr) {
	int ret;
	size_t len;
	uint8_t buffer[254];
	uint16_t result;
	struct nspire_dir_info *d;
	*info_ptr = NULL;

	if ( (ret = service_connect(handle, 0x4060)) )
		return ret;

	/* Begin dir enum */
	if ( (ret = data_build("bs0", buffer, sizeof(buffer), &len,
			0x0D, path)) )
		goto end;

	if ( (ret = data_write(handle, buffer, len)) )
		goto end;

	if ( (ret = data_read(handle, buffer, 2, &len)) )
		goto end;

	if ( (ret = data_scan("h", buffer, len, &result)) )
		goto end;

	if (result != 0xFF00) {
		ret = result == 0xFF0A ? -NSPIRE_ERR_NONEXIST : -NSPIRE_ERR_INVALID;
		goto end;
	}

	d = malloc(sizeof(struct nspire_dir_info));
	if (!d) {
		ret = -NSPIRE_ERR_NOMEM;
		goto end;
	}
	d->num = 0;

	*info_ptr = d;
	/* Start enumerating */
	while (1) {
		ret = dir_enum(handle, info_ptr);
		if (ret < 0)
			goto end;

		if (ret)
			break;
	}

	/* End dir enum */
	if ( (ret = data_build("b", buffer, sizeof(buffer), &len,
			0x0F)) )
		goto end;

	if ( (ret = data_write(handle, buffer, len)) )
		goto end;

	if ( (ret = data_read(handle, buffer, 2, &len)) )
		goto end;

	if ( (ret = data_scan("h", buffer, len, &result)) )
		goto end;

	if (result != 0xFF00 && result != 0xFF11) {
		ret = result == 0xFF0A ? -NSPIRE_ERR_NONEXIST : -NSPIRE_ERR_INVALID;
		goto end;
	}

	ret = NSPIRE_ERR_SUCCESS;
end:
	ret = service_finish(handle, ret);
	if (ret) {
		free(*info_ptr);
		*info_ptr = NULL;
	}
	return ret;
}

void nspire_dirlist_free(struct nspire_dir_info *d) {
	free(d);
}

int nspire_dir_create(nspire_handle_t *handle, const char *path) {
	int ret;
	size_t len;
	uint16_t result;
	uint8_t buffer[254];

	if ( (ret = service_connect(handle, 0x4060)) )
		return ret;

	if ( (ret = data_build("hs", buffer, sizeof(buffer), &len,
			0x0A03, path)) )
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

int nspire_dir_delete(nspire_handle_t *handle, const char *path) {
	int ret;
	size_t len;
	uint16_t result;
	uint8_t buffer[254];


	if ( (ret = service_connect(handle, 0x4060)) )
		return ret;

	if ( (ret = data_build("hs", buffer, sizeof(buffer), &len,
			0x0B03, path)) )
		goto end;

	if ( (ret = data_write(handle, &buffer, len)) )
		goto end;

	if ( (ret = data_read(handle, &buffer, 2, &len)) )
		goto end;

	if ( (ret = data_scan("h", buffer, len, &result)) )
		goto end;

	ret = (result == 0xFF00) ? NSPIRE_ERR_SUCCESS : -NSPIRE_ERR_NONEXIST;
end:
	return service_finish(handle, ret);
}

int nspire_attr(nspire_handle_t *handle, const char *path,
		struct nspire_dir_item *info) {
	int ret;
	const char *phase = "build";
	size_t len;
	uint8_t is_dir, buffer[254];
	uint32_t size, date;

	if ( (ret = service_connect(handle, 0x4060)) )
		return ret;

	if ( (ret = data_build("hs0", buffer, sizeof(buffer), &len,
			0x2001, path)) )
		goto end;

	phase = "request";
	nspire_trace("file operation=stat phase=%s offset=0 requested=%zu", phase, len);
	if ( (ret = data_write(handle, buffer, len)) )
		goto end;

	phase = "response";
	nspire_trace("file operation=stat phase=%s offset=0", phase);
	if ( (ret = data_read(handle, buffer, sizeof(buffer), &len)) )
		goto end;

	if (!len) {
		ret = -NSPIRE_ERR_INVALID;
		goto end;
	}
	if (buffer[0] != 0x20) {
		ret = -NSPIRE_ERR_NONEXIST;
		goto end;
	}

	if ( (ret = data_scan("bwwb0", buffer, len,
			NULL, &size, &date, &is_dir)) )
		goto end;

	strncpy(info->name, path, sizeof(info->name));
	info->name[sizeof(info->name)-1] = '\0';
	info->size = size;
	info->date = date;
	info->type = is_dir;

	ret = NSPIRE_ERR_SUCCESS;
end:
	nspire_trace("file operation=stat phase=%s event=end offset=0 status=%d", phase, ret);
	return service_finish(handle, ret);
}
