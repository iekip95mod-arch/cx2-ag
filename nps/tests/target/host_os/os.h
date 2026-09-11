// Stands in for Ndl's os.h when src/lua_module.cc is built for the host. LuaJIT provides the
// Lua 5.1 API, and the harness supplies the Ndl calls this module makes.
//
// A missing stub here stops the harness building, and this harness is the only thing that exercises
// the real Lua entry points rather than the engines beneath them. Nothing reports that it stopped,
// so the entry points quietly lose their only cover.
#ifndef NPS_TEST_HOST_OS_H
#define NPS_TEST_HOST_OS_H

#include <lua.hpp>

lua_State *nl_lua_getstate();

extern bool nps_test_escape_pressed;
static const int KEY_NSPIRE_ESC = 0;
inline bool isKeyPressed(int) { return nps_test_escape_pressed; }

// The dialogs answer without drawing. A host run has nobody to press a button, so the message boxes
// report whichever button the harness has armed, and the number input reports a cancel, which is the
// branch a caller is likelier to get wrong.
extern unsigned nps_test_msgbox_reply;
inline unsigned show_msgbox(const char *, const char *) { return nps_test_msgbox_reply; }
inline unsigned show_msgbox_2b(const char *, const char *, const char *, const char *) {
    return nps_test_msgbox_reply;
}
inline unsigned show_msgbox_3b(const char *, const char *, const char *, const char *,
                               const char *) {
    return nps_test_msgbox_reply;
}
inline int show_1numeric_input(const char *, const char *, const char *, int *, int, int) {
    return 0;
}

// A CX II CAS on a plausible Ndl, so l_device_identity has something a caller can assert against.
static const int FALSE = 0;
inline int nl_hwtype() { return 1; }
inline int nl_hwsubtype() { return 2; }
inline int nl_ndl_rev() { return 2001; }
inline int nl_loaded_by_3rd_party_loader() { return FALSE; }

#endif
