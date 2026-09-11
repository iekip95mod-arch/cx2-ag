#ifndef NPS_PLATFORM_NSPIRE_ALLOCATION_PROBE_H
#define NPS_PLATFORM_NSPIRE_ALLOCATION_PROBE_H

#include <cstddef>
#include <cstdint>

#include "nps/platform/nspire/measurement.h"

#ifndef NPS_RESOURCE_PROFILE
#define NPS_RESOURCE_PROFILE 0
#endif

#ifndef NPS_RESOURCE_PROFILE_TESTING
#define NPS_RESOURCE_PROFILE_TESTING 0
#endif

#if NPS_RESOURCE_PROFILE

namespace nps {

inline constexpr std::size_t kResourceProfileTrackerCapacity = 8192;
inline constexpr std::size_t kResourceProfileTrackerStorageBytes =
    sizeof(AllocationTracker<kResourceProfileTrackerCapacity>);
inline constexpr char kResourceProfileReportPath[] =
    "/documents/ndl/nps_resource.txt.tns";

struct ResourceProfileMetrics {
    const char *operation = nullptr;
    bool request_failed = false;
    bool solver_metrics_available = false;
    uint64_t render_ready_ms = 0;
    std::size_t lua_live_bytes = 0;
    std::size_t arena_nodes = 0;
    std::size_t arena_child_slots = 0;
    std::size_t derivation_steps = 0;
    std::size_t rewrites = 0;
    std::size_t backend_calls = 0;
};

struct ResourceProfileResult {
    AllocationSnapshot allocation;
    AllocatorHeadroom headroom_before;
    AllocatorHeadroom headroom_render_ready;
    std::size_t tracker_capacity = kResourceProfileTrackerCapacity;
    bool interval_active = false;
    bool report_written = false;
};

bool begin_resource_profile(const char *operation) noexcept;
ResourceProfileResult finish_resource_profile(const ResourceProfileMetrics &metrics) noexcept;

#if NPS_RESOURCE_PROFILE_TESTING
struct ResourceProfileIo {
    int (*open)(const char *path, int flags, int mode) noexcept;
    std::ptrdiff_t (*write)(int descriptor, const void *bytes, std::size_t length) noexcept;
    int (*close)(int descriptor) noexcept;
};

void set_resource_profile_io_for_testing(const ResourceProfileIo &io) noexcept;
void reset_resource_profile_io_for_testing() noexcept;
#endif

}

#endif

#endif
