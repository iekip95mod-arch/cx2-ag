#include "nps/ui/retained_menu.h"
#include "nps/ui/bitmap.h"
#include "nps/platform/nspire/integrity.h"

#include <array>
#include <optional>
#include <os.h>
#include <unistd.h>

namespace {
// Ndl unloads the image without running C++ finalizers.
alignas(nps::ui::RetainedMenu::pixel_alignment) std::array<uint16_t, 320 * 240> pixels;
std::optional<nps::ui::RetainedMenu> menu;
std::array<char, 20 + 320 * 240 * 2> encoded;
constexpr std::array<std::string_view, 6> titles = {"First derivative", "Definite integral", "Finite limit",
    "Right-sided limit", "Limit at infinity", "Full answer and domain conditions"};
size_t selected = 0;
int viewport_width = 0, viewport_height = 0;

int frame(lua_State *L) {
    const int width = luaL_checkinteger(L, 1);
    const int height = luaL_checkinteger(L, 2);
    if (width < 96 || width > 320 || height < 192 || height > 240)
        return luaL_error(L, "Unsupported qualification viewport");
    bool created = false;
    if (!menu || viewport_width != width || viewport_height != height) {
        menu.reset();
        menu.emplace(width, height, pixels, "Calculus templates", titles);
        viewport_width = width;
        viewport_height = height;
        selected = 0;
        created = true;
    }
    if (menu->status() != NPS_RETAINED_READY) return luaL_error(L, "Retained menu unavailable");
    const bool changed = menu->render();
    if (menu->status() != NPS_RETAINED_READY) return luaL_error(L, "Retained rendering failed safely");
    if (changed || created) {
        const size_t bytes = nps::ui::ti_image_size(width, height);
        if (!nps::ui::encode_ti_image(width, height, menu->pixels(), {encoded.data(), bytes}))
            return luaL_error(L, "Retained surface encoding failed");
        lua_pushlstring(L, encoded.data(), bytes);
    } else lua_pushnil(L);
    lua_pushinteger(L, menu->pool_used());
    lua_pushinteger(L, menu->revision());
    return 3;
}

int move(lua_State *L) {
    const int direction = luaL_checkinteger(L, 1);
    if (direction != -1 && direction != 1) return luaL_error(L, "Expected one navigation step");
    if (!menu) return 0;
    const size_t next = direction > 0 ? (selected + 1) % titles.size()
                                    : (selected + titles.size() - 1) % titles.size();
    if (!menu->select(next)) return luaL_error(L, "Retained navigation failed safely");
    selected = next;
    return 0;
}

int choose(lua_State *L) {
    if (menu && menu->status() == NPS_RETAINED_READY) lua_pushlstring(L, titles[selected].data(), titles[selected].size());
    else lua_pushnil(L);
    return 1;
}

const luaL_Reg functions[] = {{"frame", frame}, {"move", move}, {"choose", choose}, {nullptr, nullptr}};
}

int main(int argc, char **argv) {
    lua_State *L = nl_lua_getstate();
    if (!L) return 0;
    if (argc < 1 || !argv || !argv[0] ||
        nps::verify_package_integrity(argv[0], "nps_retained_probe.luax.tns") != nps::IntegrityStatus::Verified)
        return luaL_error(L, "Retained UI probe integrity check failed");
    luaL_register(L, "nps_retained_probe", functions);
    _exit(0);
}
