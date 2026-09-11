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

#include "handle.h"
#include "error.h"
#include "data.h"
#include "service.h"
#include "api/raw.h"

int nspire_service_exchange(nspire_handle_t *handle, uint16_t service_id,
		const void *request, size_t request_len,
		void *reply, size_t reply_max, size_t *reply_len) {
	int ret;

	if ( (ret = service_connect(handle, service_id)) )
		return ret;

	if ( (ret = data_write(handle, (void *)request, request_len)) )
		goto end;

	if ( (ret = data_read(handle, reply, reply_max, reply_len)) )
		goto end;

	ret = NSPIRE_ERR_SUCCESS;

end:
	return service_finish(handle, ret);
}
