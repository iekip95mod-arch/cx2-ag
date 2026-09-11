// Selftest fixture for lint-resident-exit. A resident Lua module main that ends with _exit. The
// check must pass this. The early return on the not-a-module path is correct and must not be flagged.
typedef struct lua_State lua_State;
lua_State *nl_lua_getstate(void);
void register_module(lua_State *L);
void _exit(int);

int main(void) {
    lua_State *L = nl_lua_getstate();
    if (!L)
        return 0;
    register_module(L);
    _exit(0);
}
