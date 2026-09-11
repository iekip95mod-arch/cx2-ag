#define main container_device_main
#include "container_probe.cc"
#undef main

lua_State *nl_lua_getstate() { return nullptr; }
extern "C" int luaopen_nps_container_probe(lua_State *L) {
    luaL_register(L, "nps_container_probe", functions);
    return 1;
}
