/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "error.h"
#include "data.h"
#include "service.h"
#include "api/key.h"

// Wire format: https://github.com/debrouxl/tilibs/blob/6dba390e7390c4b98ae96287b39a3971c331fbef/libticalcs/trunk/src/nsp_cmd.cc#L1044-L1098
int nspire_send_key(nspire_handle_t *handle, uint32_t key) {
	int ret;
	uint8_t initialize[] = { 1, 0, 0, 0x80 };
	uint8_t command[26] = { 0 };

	if (key > 0xffffff)
		return -NSPIRE_ERR_INVALID;
	if ( (ret = service_connect(handle, 0x4042)) )
		return ret;
	if ( (ret = data_write(handle, initialize, sizeof initialize)) )
		goto end;

	command[4] = 8;
	command[5] = 2;
	command[6] = key >> 16;
	command[8] = key >> 8;
	command[24] = key;
	ret = data_write(handle, command, sizeof command);

end:
	return service_finish(handle, ret);
}
