// Selftest fixture for lint-resident-exit. A resident Lua module main that calls _exit on its
// refusal branch and returns on the path that registers the module, which is the shape lua_module.cc
// has. The check must flag the return. Standalone so the selftest needs no SDK headers.
typedef struct lua_State lua_State;
lua_State *nl_lua_getstate(void);
void register_module(lua_State *L);
void register_refusal(lua_State *L);
int module_verified(void);
void _exit(int);

int main(void) {
    lua_State *L = nl_lua_getstate();
    if (!L)
        return 0;
    if (!module_verified()) {
        register_refusal(L);
        _exit(0);
    }
    register_module(L);
    return 0;
}
