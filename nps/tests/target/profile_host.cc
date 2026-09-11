#define luaopen_nps_split luaopen_base_nps_split
#include "luax_host.cc"
#undef luaopen_nps_split

namespace nps {

static bool profile_active = false;
static ResourceProfileMetrics observed_metrics;
static std::string observed_operation;

bool begin_resource_profile(const char *) noexcept {
    if (profile_active)
        return false;
    profile_active = true;
    return true;
}

ResourceProfileResult finish_resource_profile(const ResourceProfileMetrics &metrics) noexcept {
    observed_metrics = metrics;
    observed_operation = metrics.operation ? metrics.operation : "";
    ResourceProfileResult profile;
    profile.interval_active = profile_active;
    profile.report_written = profile_active;
    profile_active = false;
    return profile;
}

int test_profile_metrics(lua_State *L) {
    lua_newtable(L);
    set_field(L, "request_failed", observed_metrics.request_failed);
    set_field(L, "solver_metrics_available", observed_metrics.solver_metrics_available);
    set_field(L, "operation", observed_operation);
    set_profile_size_field(L, "render_ready_ms", observed_metrics.render_ready_ms);
    set_profile_size_field(L, "lua_live_bytes", observed_metrics.lua_live_bytes);
    set_profile_size_field(L, "arena_nodes", observed_metrics.arena_nodes);
    set_profile_size_field(L, "arena_child_slots", observed_metrics.arena_child_slots);
    set_profile_size_field(L, "derivation_steps", observed_metrics.derivation_steps);
    set_profile_size_field(L, "rewrites", observed_metrics.rewrites);
    set_profile_size_field(L, "backend_calls", observed_metrics.backend_calls);
    return 1;
}

}

extern "C" int luaopen_nps_split(lua_State *L) {
    luaopen_base_nps_split(L);
    lua_pushcfunction(L, nps::test_profile_metrics);
    lua_setfield(L, -2, "test_profile_metrics");
    return 1;
}
