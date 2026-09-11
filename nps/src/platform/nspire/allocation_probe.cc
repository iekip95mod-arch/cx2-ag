#include "nps/platform/nspire/allocation_probe.h"

#if NPS_RESOURCE_PROFILE

#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <system_error>
#include <type_traits>
#include <unistd.h>

struct _reent;

namespace nps {
namespace {

constexpr std::size_t kOperationCapacity = 32;
using Tracker = AllocationTracker<kResourceProfileTrackerCapacity>;

struct ReportIo {
    int (*open)(const char *path, int flags, int mode) noexcept;
    std::ptrdiff_t (*write)(int descriptor, const void *bytes, std::size_t length) noexcept;
    int (*close)(int descriptor) noexcept;
};

static_assert(std::is_trivially_destructible_v<Tracker>);

alignas(Tracker) constinit std::byte allocation_tracker_storage[sizeof(Tracker)] = {};
constinit Tracker *allocation_tracker = nullptr;
constinit char operation_name[kOperationCapacity] = {};
constinit AllocatorHeadroom headroom_before;
constinit bool interval_active = false;
constinit bool wrapper_active = false;

int system_open(const char *path, int flags, int mode) noexcept {
    return ::open(path, flags, mode);
}

std::ptrdiff_t system_write(int descriptor, const void *bytes, std::size_t length) noexcept {
    return static_cast<std::ptrdiff_t>(::write(descriptor, bytes, length));
}

int system_close(int descriptor) noexcept { return ::close(descriptor); }

constinit ReportIo report_io{system_open, system_write, system_close};

Tracker &tracker() noexcept {
    if (!allocation_tracker)
        allocation_tracker =
            std::construct_at(reinterpret_cast<Tracker *>(allocation_tracker_storage));
    return *allocation_tracker;
}

bool operation_character(char character) noexcept {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' || character == '-' ||
           character == '.';
}

bool copy_operation(const char *operation) noexcept {
    if (!operation || operation[0] == '\0')
        return false;

    std::size_t index = 0;
    while (operation[index] != '\0') {
        if (index + 1 == kOperationCapacity || !operation_character(operation[index]))
            return false;
        operation_name[index] = operation[index];
        ++index;
    }
    operation_name[index] = '\0';
    return true;
}

bool same_operation(const char *operation) noexcept {
    if (!operation)
        return false;
    return std::strncmp(operation_name, operation, kOperationCapacity) == 0;
}

class WrapperScope {
  public:
    WrapperScope() noexcept : outermost_(!wrapper_active) {
        if (outermost_)
            wrapper_active = true;
    }

    ~WrapperScope() {
        if (outermost_)
            wrapper_active = false;
    }

    bool outermost() const noexcept { return outermost_; }

  private:
    bool outermost_;
};

bool write_all(int descriptor, const char *bytes, std::size_t length) noexcept {
    while (length != 0) {
        std::ptrdiff_t written = -1;
        do {
            written = report_io.write(descriptor, bytes, length);
        } while (written < 0 && errno == EINTR);
        if (written <= 0)
            return false;
        const std::size_t count = static_cast<std::size_t>(written);
        bytes += count;
        length -= count;
    }
    return true;
}

class ReportWriter {
  public:
    explicit ReportWriter(int descriptor) noexcept : descriptor_(descriptor) {}

    void text(const char *key, const char *value) noexcept {
        literal(key);
        literal(" = ");
        literal(value);
        literal("\n");
    }

    void number(const char *key, uint64_t value) noexcept {
        char digits[32];
        const auto conversion = std::to_chars(digits, digits + sizeof(digits), value);
        if (conversion.ec != std::errc()) {
            ok_ = false;
            return;
        }
        literal(key);
        literal(" = ");
        bytes(digits, static_cast<std::size_t>(conversion.ptr - digits));
        literal("\n");
    }

    bool ok() const noexcept { return ok_; }

  private:
    void literal(const char *text) noexcept { bytes(text, std::strlen(text)); }

    void bytes(const char *text, std::size_t length) noexcept {
        if (ok_ && !write_all(descriptor_, text, length))
            ok_ = false;
    }

    int descriptor_;
    bool ok_ = true;
};

int open_report() noexcept {
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#if defined(O_BINARY)
    flags |= O_BINARY;
#endif
    int descriptor = -1;
    do {
        descriptor = report_io.open(kResourceProfileReportPath, flags, 0666);
    } while (descriptor < 0 && errno == EINTR);
    return descriptor;
}

void write_headroom(ReportWriter &writer, const char *prefix,
                    const AllocatorHeadroom &headroom) noexcept {
    char key[64];
    const std::size_t prefix_length = std::strlen(prefix);
    auto field = [&](const char *suffix) noexcept -> const char * {
        const std::size_t suffix_length = std::strlen(suffix);
        if (prefix_length + suffix_length + 1 > sizeof(key))
            return nullptr;
        std::memcpy(key, prefix, prefix_length);
        std::memcpy(key + prefix_length, suffix, suffix_length + 1);
        return key;
    };

    if (const char *name = field("_scope"))
        writer.text(name, "contiguous allocation lower bound, not free memory");
    if (const char *name = field("_contiguous_lower_bound_bytes"))
        writer.number(name, static_cast<uint64_t>(headroom.contiguous_lower_bound_bytes));
    if (const char *name = field("_resolution_bytes"))
        writer.number(name, static_cast<uint64_t>(headroom.resolution_bytes));
    if (const char *name = field("_search_ceiling_bytes"))
        writer.number(name, static_cast<uint64_t>(headroom.search_ceiling_bytes));
    if (const char *name = field("_ceiling_reached"))
        writer.text(name, headroom.ceiling_reached ? "yes" : "no");
}

bool write_report(const ResourceProfileMetrics &metrics, const ResourceProfileResult &profile,
                  bool operation_matches) noexcept {
    const int descriptor = open_report();
    if (descriptor < 0)
        return false;

    ReportWriter writer(descriptor);
    writer.text("schema", "nps-resource-profile-v2");
    writer.text("profile", "diagnostic-only");
    writer.text("operation", operation_name);
    writer.text("operation_argument_match", operation_matches ? "yes" : "no");
    writer.text("request_status", metrics.request_failed ? "failed" : "completed");
    const bool solver_metrics_available = operation_matches && metrics.solver_metrics_available;
    writer.text("solver_metrics_available", solver_metrics_available ? "yes" : "no");
    writer.text("native_requested_scope",
                "module-image requests observed through wrapped allocation APIs");
    writer.text("native_requested_wrappers",
                "malloc,free,realloc,calloc,_malloc_r,_free_r,_realloc_r,_calloc_r");
    writer.text("native_requested_semantics",
                "tracked live requested bytes, with an interval-local absolute peak");
    writer.text("native_requested_excludes",
                "allocator metadata, fragmentation, raw syscalls, Lua, stack, whole process");
    writer.number("tracker_capacity_entries", static_cast<uint64_t>(profile.tracker_capacity));
    writer.number("tracker_instrumentation_storage_bytes",
                  static_cast<uint64_t>(kResourceProfileTrackerStorageBytes));
    writer.text("tracker_accounting_valid", profile.allocation.valid ? "yes" : "no");
    writer.text("tracker_overflowed", profile.allocation.overflowed ? "yes" : "no");
    writer.number("native_requested_baseline_bytes",
                  static_cast<uint64_t>(profile.allocation.baseline_requested_bytes));
    writer.number("native_requested_current_bytes",
                  static_cast<uint64_t>(profile.allocation.current_requested_bytes));
    writer.number("native_requested_interval_peak_bytes",
                  static_cast<uint64_t>(profile.allocation.peak_requested_bytes));
    writer.number("native_allocation_count",
                  static_cast<uint64_t>(profile.allocation.allocation_count));
    writer.number("native_free_count", static_cast<uint64_t>(profile.allocation.free_count));
    writer.number("native_reallocation_count",
                  static_cast<uint64_t>(profile.allocation.reallocation_count));
    writer.number("native_allocation_failure_count",
                  static_cast<uint64_t>(profile.allocation.failure_count));
    writer.number("native_unknown_free_count",
                  static_cast<uint64_t>(profile.allocation.unknown_free_count));
    writer.number("native_unknown_reallocation_count",
                  static_cast<uint64_t>(profile.allocation.unknown_reallocation_count));
    write_headroom(writer, "headroom_before", profile.headroom_before);
    write_headroom(writer, "headroom_render_ready", profile.headroom_render_ready);
    writer.text("lua_live_scope", "render-ready live heap snapshot, not a high-water mark");
    if (operation_matches)
        writer.number("lua_live_bytes", static_cast<uint64_t>(metrics.lua_live_bytes));
    else
        writer.text("lua_live_bytes", "unavailable");
    writer.text("arena_scope", "operation-local monotonic occupancy, not process memory");
    auto solver_counter = [&](const char *key, std::size_t value) noexcept {
        if (solver_metrics_available)
            writer.number(key, static_cast<uint64_t>(value));
        else
            writer.text(key, "unavailable");
    };
    solver_counter("arena_nodes", metrics.arena_nodes);
    solver_counter("arena_child_slots", metrics.arena_child_slots);
    if (operation_matches)
        writer.number("render_ready_ms", metrics.render_ready_ms);
    else
        writer.text("render_ready_ms", "unavailable");
    solver_counter("derivation_steps", metrics.derivation_steps);
    solver_counter("rewrites", metrics.rewrites);
    solver_counter("backend_calls", metrics.backend_calls);

    const bool written = writer.ok();
    const bool closed = report_io.close(descriptor) == 0;
    return written && closed;
}

void record_allocation(void *pointer, std::size_t requested_bytes) noexcept {
    tracker().record_allocation(pointer, requested_bytes);
}

void record_calloc(void *pointer, std::size_t count, std::size_t element_size) noexcept {
    tracker().record_calloc(pointer, count, element_size);
}

void record_free(void *pointer) noexcept { tracker().record_free(pointer); }

void record_reallocation(void *old_pointer, void *new_pointer,
                         std::size_t requested_bytes) noexcept {
    tracker().record_reallocation(old_pointer, new_pointer, requested_bytes);
}

}

bool begin_resource_profile(const char *operation) noexcept {
    interval_active = false;
    operation_name[0] = '\0';
    headroom_before = {};
    if (!copy_operation(operation))
        return false;

    headroom_before = measure_contiguous_headroom();
    tracker().begin_interval();
    interval_active = true;
    return true;
}

ResourceProfileResult finish_resource_profile(const ResourceProfileMetrics &metrics) noexcept {
    ResourceProfileResult profile;
    profile.allocation = tracker().snapshot();
    if (!interval_active)
        return profile;

    profile.interval_active = true;
    profile.headroom_before = headroom_before;
    interval_active = false;
    profile.headroom_render_ready = measure_contiguous_headroom();
    profile.report_written = write_report(metrics, profile, same_operation(metrics.operation));
    return profile;
}

#if NPS_RESOURCE_PROFILE_TESTING
void set_resource_profile_io_for_testing(const ResourceProfileIo &io) noexcept {
    report_io = {io.open, io.write, io.close};
}

void reset_resource_profile_io_for_testing() noexcept {
    report_io = {system_open, system_write, system_close};
}
#endif

extern "C" void *__real_malloc(std::size_t) noexcept;
extern "C" void __real_free(void *) noexcept;
extern "C" void *__real_realloc(void *, std::size_t) noexcept;
extern "C" void *__real_calloc(std::size_t, std::size_t) noexcept;
extern "C" void *__real__malloc_r(::_reent *, std::size_t) noexcept;
extern "C" void __real__free_r(::_reent *, void *) noexcept;
extern "C" void *__real__realloc_r(::_reent *, void *, std::size_t) noexcept;
extern "C" void *__real__calloc_r(::_reent *, std::size_t, std::size_t) noexcept;

extern "C" void *__wrap_malloc(std::size_t requested_bytes) noexcept {
    WrapperScope scope;
    void *pointer = __real_malloc(requested_bytes);
    if (scope.outermost())
        record_allocation(pointer, requested_bytes);
    return pointer;
}

extern "C" void __wrap_free(void *pointer) noexcept {
    WrapperScope scope;
    __real_free(pointer);
    if (scope.outermost())
        record_free(pointer);
}

extern "C" void *__wrap_realloc(void *old_pointer, std::size_t requested_bytes) noexcept {
    WrapperScope scope;
    void *new_pointer = __real_realloc(old_pointer, requested_bytes);
    if (scope.outermost())
        record_reallocation(old_pointer, new_pointer, requested_bytes);
    return new_pointer;
}

extern "C" void *__wrap_calloc(std::size_t count, std::size_t element_size) noexcept {
    WrapperScope scope;
    void *pointer = __real_calloc(count, element_size);
    if (scope.outermost())
        record_calloc(pointer, count, element_size);
    return pointer;
}

extern "C" void *__wrap__malloc_r(::_reent *reentrancy, std::size_t requested_bytes) noexcept {
    WrapperScope scope;
    void *pointer = __real__malloc_r(reentrancy, requested_bytes);
    if (scope.outermost())
        record_allocation(pointer, requested_bytes);
    return pointer;
}

extern "C" void __wrap__free_r(::_reent *reentrancy, void *pointer) noexcept {
    WrapperScope scope;
    __real__free_r(reentrancy, pointer);
    if (scope.outermost())
        record_free(pointer);
}

extern "C" void *__wrap__realloc_r(::_reent *reentrancy, void *old_pointer,
                                    std::size_t requested_bytes) noexcept {
    WrapperScope scope;
    void *new_pointer = __real__realloc_r(reentrancy, old_pointer, requested_bytes);
    if (scope.outermost())
        record_reallocation(old_pointer, new_pointer, requested_bytes);
    return new_pointer;
}

extern "C" void *__wrap__calloc_r(::_reent *reentrancy, std::size_t count,
                                   std::size_t element_size) noexcept {
    WrapperScope scope;
    void *pointer = __real__calloc_r(reentrancy, count, element_size);
    if (scope.outermost())
        record_calloc(pointer, count, element_size);
    return pointer;
}

}

#endif
