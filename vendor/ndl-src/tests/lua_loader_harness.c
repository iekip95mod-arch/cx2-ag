#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>

DIAGNOSTIC_SOURCE

static int scenario, calls, frees;
static unsigned char module_memory;
static const char *get_documents_dir(void) { return "/documents"; }
static void ld_free(void *base) { if (base) { assert(base == &module_memory); ++frees; } }
static int ld_exec_with_args_diagnostic(const char *path, int argc, char *argv[], void **base, struct ld_diagnostic *diagnostic) {
    assert(argc == 0 && !argv && strstr(path, "probe.luax.tns"));
    ++calls;
    if (scenario == 1 || (scenario == 2 && calls == 1))
        return ld_diagnostic_set(diagnostic, "zehn.executable.allocate", 0xDEAD, UINT32_MAX);
    if (scenario == 3) {
        *base = &module_memory;
        return ld_diagnostic_set(diagnostic, "program.entry", -17, 0);
    }
    *base = &module_memory;
    return ld_diagnostic_set(diagnostic, "none", 0, 0);
}
static int file_each(const char *directory, int (*callback)(const char *, void *), void *context) {
    assert(!strcmp(directory, "/documents"));
    assert(callback("/documents/unrelated.tns", context) == 0);
    if (scenario == 0) return 0;
    if (callback("/documents/a/probe.luax.tns", context)) return 1;
    assert(callback("/documents/other.luax.tns", context) == 0);
    return scenario == 2 ? callback("/documents/b/probe.luax.tns", context) : 0;
}

REQUIRE_SOURCE

static int cases;
static void check(lua_State *L, int which, const char *name, const char *error) {
    ++cases;
    scenario = which;
    calls = frees = 0;
    lua_pushcfunction(L, require);
    lua_pushstring(L, name);
    int status = lua_pcall(L, 1, 1, 0);
    if (error) {
        assert(status != 0);
        assert(!strcmp(lua_tostring(L, -1), error));
    } else assert(status == 0);
    lua_pop(L, 1);
}

int main(void) {
    lua_State *L = luaL_newstate();
    assert(L);
    check(L, 1, "probe", "module 'probe' was found but would not load: zehn.executable.allocate (code 57005, bytes 4294967295)");
    assert(loaded_next_index == 0 && calls == 1 && frees == 0);
    check(L, 0, "probe", "module 'probe' not found");
    assert(loaded_next_index == 0 && calls == 0);
    check(L, 3, "probe", "module 'probe' was found but would not load: program.entry (code -17, bytes 0)");
    assert(loaded_next_index == 0 && calls == 1 && frees == 1);
    check(L, 2, "probe", NULL);
    assert(loaded_next_index == 1 && calls == 2 && frees == 0 && loaded[0] == &module_memory);
    check(L, 4, "probe", NULL);
    assert(loaded_next_index == 2 && calls == 1);
    check(L, 0, "abcdefghijklmnopqrstuvwxyz1234", "module 'abcdefghijklmnopqrstuvwxyz1234' not found");
    assert(calls == 0);
    loaded_next_index = LUAEXT_MAX_MODULES;
    check(L, 4, "probe", "cannot load module 'probe': too many modules loaded");
    assert(calls == 0);
    lua_close(L);
    printf("Lua loader diagnostic cases: %d\n", cases);
}
