// The Lua bridge as a host shared library, so its stack handling and its re-entry into Giac can run
// under ASan and UBSan with a scripted Giac. On the calculator the same defects are a reset with
// nothing to read. The module source is included whole rather than linked, so its registration
// table is reachable and nothing in src/ has to know about the host.

bool nps_test_escape_pressed = false;
unsigned nps_test_msgbox_reply = 1;

#define main stepcas_device_main
#include "../../src/platform/nspire/lua_module.cc"
#undef main

lua_State *nl_lua_getstate() { return nullptr; }

// Declared in the module itself rather than in os.h, so the stub belongs here to match it.
extern "C" unsigned nl_osid() { return 0; }

int set_escape_pressed(lua_State *L) {
    nps_test_escape_pressed = lua_toboolean(L, 1) != 0;
    return 0;
}

int test_lua_number_observation_comparator(lua_State *L) {
    const bool matches = lua_number_abi_matches(luaL_checknumber(L, 1), luaL_checknumber(L, 2),
                                                luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    lua_pushboolean(L, matches);
    return 1;
}

int test_integrity_failure_surface(lua_State *L) {
    register_integrity_failure_surface(L, "nps_nspire", IntegrityStatus::Mismatch);
    return 1;
}

extern "C" int luaopen_nps_split(lua_State *L) {
    if (!lua_number_abi_works(L))
        return luaL_error(L, "StepCAS module rejected an incompatible Ndl Lua number ABI");
    luaL_register(L, "nps_split", lib);
    lua_pushcfunction(L, set_escape_pressed);
    lua_setfield(L, -2, "test_escape_pressed");
    lua_pushcfunction(L, test_lua_number_observation_comparator);
    lua_setfield(L, -2, "test_lua_number_observation_comparator");
    lua_pushcfunction(L, test_integrity_failure_surface);
    lua_setfield(L, -2, "test_integrity_failure_surface");
    return 1;
}
