/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NSP_KEY_H
#define NSP_KEY_H

#include <stdint.h>
#include "handle.h"

// Sends a packed 24-bit TiLP key and waits for transport ACKs, not a visible UI effect.
int nspire_send_key(nspire_handle_t *handle, uint32_t key);

#endif
