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

#ifndef NSP_RAW_H
#define NSP_RAW_H

#include <stddef.h>
#include <stdint.h>

#include "handle.h"

/* One request to any service on the calculator, answered with that service's first reply.
   Meant for services a program registered on the calculator itself. */
int nspire_service_exchange(nspire_handle_t *handle, uint16_t service_id,
		const void *request, size_t request_len,
		void *reply, size_t reply_max, size_t *reply_len);

#endif
