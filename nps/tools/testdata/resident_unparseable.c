// Selftest fixture for lint-resident-exit. A resident Lua module main behind an include that cannot
// resolve, which is how lua_module.cc looked to this lint from 12621ad until the toolchain include
// was named: clang-query reports a fatal error, exits 0 and prints no match. Flagging nothing here
// would mean the file was never judged, so the check has to refuse it rather than call it clean.
#include "nps-no-such-header.h"

typedef struct lua_State lua_State;
lua_State *nl_lua_getstate(void);
void register_module(lua_State *L);

int main(void) {
    lua_State *L = nl_lua_getstate();
    register_module(L);
    return 0;
}
