/* Declarations for armsnippets */

#ifndef _H_ARMSNIPPETS
#define _H_ARMSNIPPETS

#include <stdbool.h>
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

enum SNIPPETS {
    SNIPPET_ndls_debug_alloc, SNIPPET_ndls_debug_free, SNIPPET_ndls_exec
};
enum ARMLOADER_PARAM_TYPE {ARMLOADER_PARAM_VAL, ARMLOADER_PARAM_PTR};
struct armloader_load_params {
    enum ARMLOADER_PARAM_TYPE t;
    union {
        struct {
            void *ptr;
            uint32_t size;
        } p;
        uint32_t v; // simple value
    };
};
bool armloader_cb(void);
void armloader_reset(void);
bool armloader_load_snippet(enum SNIPPETS snippet, struct armloader_load_params params[], unsigned params_num, void (*callback)(struct arm_state *));

/* True when the guest is on the task Ndl runs programs on, so a snippet ending in an swi can be
 * run here. See armsnippets_loader.c for what the test is and why. */
bool armloader_ndl_context(void);
/* Run a path under Ndl the next time armloader_ndl_context() holds. */
void armloader_defer_exec(const char *path);
/* Run an ARM code blob on that task instead, entered at its first byte and returning through lr.
 * Replaces anything already queued. */
bool armloader_defer_blob(const uint8_t *blob, uint32_t size);
void armloader_cancel_deferred(void);
bool armloader_deferred_pending(void);
/* Called from the emulation loop between instructions. */
void armloader_poll_deferred(void);

#ifdef __cplusplus
}
#endif

#endif
