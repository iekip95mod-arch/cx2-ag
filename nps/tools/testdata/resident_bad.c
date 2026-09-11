// Selftest fixture for lint-resident-exit. A resident Lua module main that returns instead of
// calling _exit. The check must flag this. Standalone so the selftest needs no SDK headers.
typedef struct lua_State lua_State;
lua_State *nl_lua_getstate(void);
void register_module(lua_State *L);

int main(void) {
    lua_State *L = nl_lua_getstate();
    if (!L)
        return 0;
    register_module(L);
    return 0;
}
