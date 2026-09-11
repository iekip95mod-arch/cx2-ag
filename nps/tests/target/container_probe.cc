#include "ui/container_workload.h"
#include "nps/platform/nspire/integrity.h"

#include <cmath>
#include <os.h>
#include <unistd.h>

namespace {
unsigned argument(lua_State *L, int index, unsigned minimum, unsigned maximum) {
    const lua_Number value = luaL_checknumber(L, index);
    if (!(value >= minimum && value <= maximum) || value != std::floor(value))
        luaL_argerror(L, index, "Expected a bounded integer");
    return static_cast<unsigned>(value);
}
int run(lua_State *L) {
    const unsigned kind = argument(L, 1, 1, 6);
    const unsigned frames = argument(L, 2, 1, 20000);
    const unsigned seed = argument(L, 3, 0, 199);
    const auto batch = nps::test::container_batch(kind, frames, seed);
    if (!batch.valid) return luaL_error(L, "Container workload refused");
    lua_pushnumber(L, batch.checksum);
    lua_pushinteger(L, batch.object_bytes);
    lua_pushinteger(L, batch.heap_capacity_bytes);
    return 3;
}
const luaL_Reg functions[] = {{"run", run}, {nullptr, nullptr}};
}

int main(int argc, char **argv) {
    lua_State *L = nl_lua_getstate();
    if (!L) return 0;
    if (argc < 1 || !argv || !argv[0] ||
        nps::verify_package_integrity(argv[0], "nps_container_probe.luax.tns") != nps::IntegrityStatus::Verified)
        return luaL_error(L, "Container probe integrity check failed");
    luaL_register(L, "nps_container_probe", functions);
    _exit(0);
}
