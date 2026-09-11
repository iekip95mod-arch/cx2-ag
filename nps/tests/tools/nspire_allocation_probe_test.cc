#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "nps/platform/nspire/allocation_probe.h"

struct _reent;

namespace nps {

extern "C" void *__wrap_malloc(std::size_t) noexcept;
extern "C" void __wrap_free(void *) noexcept;
extern "C" void *__wrap_realloc(void *, std::size_t) noexcept;
extern "C" void *__wrap_calloc(std::size_t, std::size_t) noexcept;
extern "C" void *__wrap__malloc_r(::_reent *, std::size_t) noexcept;
extern "C" void __wrap__free_r(::_reent *, void *) noexcept;
extern "C" void *__wrap__realloc_r(::_reent *, void *, std::size_t) noexcept;
extern "C" void *__wrap__calloc_r(::_reent *, std::size_t, std::size_t) noexcept;

}

namespace {

void *next_pointer = nullptr;
enum class IoMode { Success, OpenFailure, WriteFailure, CloseFailure };
IoMode io_mode = IoMode::Success;
bool inject_open_interrupt = false;
bool inject_write_interrupt = false;
bool inject_partial_write = false;
unsigned open_calls = 0;
unsigned write_calls = 0;
unsigned close_calls = 0;
char report_text[8192] = {};
std::size_t report_length = 0;

bool check(bool condition, const char *message) {
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

void configure_io(IoMode mode, bool exercise_retries = false) {
    io_mode = mode;
    inject_open_interrupt = exercise_retries;
    inject_write_interrupt = exercise_retries;
    inject_partial_write = exercise_retries;
    open_calls = 0;
    write_calls = 0;
    close_calls = 0;
    report_length = 0;
    report_text[0] = '\0';
}

int fake_open(const char *path, int, int) noexcept {
    ++open_calls;
    if (std::strcmp(path, nps::kResourceProfileReportPath) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (inject_open_interrupt) {
        inject_open_interrupt = false;
        errno = EINTR;
        return -1;
    }
    if (io_mode == IoMode::OpenFailure) {
        errno = EACCES;
        return -1;
    }
    return 7;
}

std::ptrdiff_t fake_write(int descriptor, const void *bytes, std::size_t length) noexcept {
    ++write_calls;
    if (descriptor != 7) {
        errno = EBADF;
        return -1;
    }
    if (inject_write_interrupt) {
        inject_write_interrupt = false;
        errno = EINTR;
        return -1;
    }
    if (io_mode == IoMode::WriteFailure) {
        errno = EIO;
        return -1;
    }

    std::size_t count = length;
    if (inject_partial_write && length > 1) {
        inject_partial_write = false;
        count = length / 2;
    }
    if (count >= sizeof(report_text) - report_length) {
        errno = EFBIG;
        return -1;
    }
    std::memcpy(report_text + report_length, bytes, count);
    report_length += count;
    report_text[report_length] = '\0';
    return static_cast<std::ptrdiff_t>(count);
}

int fake_close(int descriptor) noexcept {
    ++close_calls;
    if (descriptor != 7 || io_mode == IoMode::CloseFailure) {
        errno = EIO;
        return -1;
    }
    return 0;
}

bool report_contains(const char *expected) {
    return std::strstr(report_text, expected) != nullptr;
}

}

extern "C" void *__real_malloc(std::size_t requested_bytes) noexcept {
    return requested_bytes == 0 ? nullptr : next_pointer;
}

extern "C" void __real_free(void *) noexcept {}

extern "C" void *__real_realloc(void *, std::size_t requested_bytes) noexcept {
    return requested_bytes == 0 ? nullptr : next_pointer;
}

extern "C" void *__real_calloc(std::size_t count, std::size_t element_size) noexcept {
    return count == 0 || element_size == 0 ? nullptr : next_pointer;
}

extern "C" void *__real__malloc_r(::_reent *, std::size_t requested_bytes) noexcept {
    return nps::__wrap_malloc(requested_bytes);
}

extern "C" void __real__free_r(::_reent *, void *pointer) noexcept {
    nps::__wrap_free(pointer);
}

extern "C" void *__real__realloc_r(::_reent *, void *pointer,
                                    std::size_t requested_bytes) noexcept {
    return nps::__wrap_realloc(pointer, requested_bytes);
}

extern "C" void *__real__calloc_r(::_reent *, std::size_t count,
                                   std::size_t element_size) noexcept {
    return nps::__wrap_calloc(count, element_size);
}

int main() {
    int failures = 0;
    const nps::ResourceProfileIo io{fake_open, fake_write, fake_close};
    nps::set_resource_profile_io_for_testing(io);
    nps::ResourceProfileMetrics metrics;
    metrics.operation = "differentiate";
    metrics.solver_metrics_available = true;
    metrics.render_ready_ms = 17;
    metrics.lua_live_bytes = 2048;
    metrics.arena_nodes = 12;
    metrics.arena_child_slots = 19;
    metrics.derivation_steps = 4;
    metrics.rewrites = 3;
    metrics.backend_calls = 1;

    failures += !check(!nps::begin_resource_profile("bad\noperation"),
                       "invalid operation tokens are rejected");
    const nps::ResourceProfileResult invalid = nps::finish_resource_profile(metrics);
    failures += !check(!invalid.interval_active && !invalid.report_written,
                       "invalid operation tokens do not start an interval");

    int first = 0;
    int second = 0;
    int third = 0;
    int fourth = 0;
    int fifth = 0;
    failures += !check(nps::begin_resource_profile(metrics.operation),
                       "a canonical operation token starts an interval");

    next_pointer = &first;
    failures += !check(nps::__wrap_malloc(40) == &first, "malloc wrapper returns the real pointer");
    next_pointer = &second;
    failures +=
        !check(nps::__wrap_calloc(3, 8) == &second, "calloc wrapper returns the real pointer");
    next_pointer = &third;
    failures += !check(nps::__wrap_realloc(&first, 80) == &third,
                       "realloc wrapper returns the moved pointer");
    nps::__wrap_free(&second);

    next_pointer = &fourth;
    failures += !check(nps::__wrap__malloc_r(nullptr, 10) == &fourth,
                       "reentrant malloc wrapper returns the real pointer");
    nps::__wrap__free_r(nullptr, &fourth);
    next_pointer = nullptr;
    failures += !check(nps::__wrap__realloc_r(nullptr, &third, 120) == nullptr,
                       "failed reentrant realloc returns null");
    next_pointer = &fifth;
    failures += !check(nps::__wrap__calloc_r(nullptr, 2, 6) == &fifth,
                       "reentrant calloc wrapper returns the real pointer");
    nps::__wrap__free_r(nullptr, &fifth);

    configure_io(IoMode::Success, true);
    const nps::ResourceProfileResult profile = nps::finish_resource_profile(metrics);
    failures += !check(profile.interval_active, "finish consumes the active interval");
    failures += !check(profile.report_written, "a complete report is returned as written");
    failures += !check(profile.tracker_capacity == nps::kResourceProfileTrackerCapacity,
                       "the result publishes tracker capacity");
    failures += !check(profile.allocation.baseline_requested_bytes == 0 &&
                           profile.allocation.current_requested_bytes == 80 &&
                           profile.allocation.peak_requested_bytes == 104,
                       "requested-byte baseline, current and peak remain exact");
    failures += !check(profile.allocation.allocation_count == 4 &&
                           profile.allocation.free_count == 3 &&
                           profile.allocation.reallocation_count == 2 &&
                           profile.allocation.failure_count == 1 && profile.allocation.valid &&
                           !profile.allocation.overflowed,
                       "outer wrappers are counted once and failed realloc preserves ownership");
    failures += !check(profile.headroom_before.search_ceiling_bytes != 0 &&
                           profile.headroom_render_ready.search_ceiling_bytes != 0,
                       "begin and finish retain both headroom probes");
    failures += !check(std::strcmp(nps::kResourceProfileReportPath,
                                   "/documents/ndl/nps_resource.txt.tns") == 0,
                       "the report path is fixed and transferable");
    failures += !check(open_calls == 2 && write_calls > 2 && close_calls == 1,
                       "open and write retry interruptions and complete partial writes");
    failures += !check(report_contains("schema = nps-resource-profile-v2\n") &&
                           report_contains("profile = diagnostic-only\n") &&
                           report_contains("operation = differentiate\n") &&
                           report_contains("operation_argument_match = yes\n") &&
                           report_contains("tracker_instrumentation_storage_bytes = ") &&
                           report_contains("native_requested_current_bytes = 80\n") &&
                           report_contains("lua_live_scope = render-ready live heap snapshot, not a high-water mark\n") &&
                           report_contains("render_ready_ms = 17\n"),
                       "the successful path emits the fixed report schema and honest scope labels");
    failures += !check(report_contains("request_status = completed\n") &&
                           report_contains("solver_metrics_available = yes\n") &&
                           report_contains("arena_nodes = 12\n") &&
                           report_contains("arena_child_slots = 19\n") &&
                           report_contains("derivation_steps = 4\n") &&
                           report_contains("rewrites = 3\n") &&
                           report_contains("backend_calls = 1\n"),
                       "completed requests explicitly report their available solver counters");

    next_pointer = nullptr;
    nps::__wrap_free(&third);

    configure_io(IoMode::OpenFailure);
    failures += !check(nps::begin_resource_profile("integrate"),
                       "a second canonical operation starts an interval");
    metrics.operation = "integrate";
    const nps::ResourceProfileResult open_failure = nps::finish_resource_profile(metrics);
    failures += !check(open_failure.interval_active && !open_failure.report_written &&
                           open_calls == 1 && close_calls == 0,
                       "an open failure is returned without closing an invalid descriptor");

    configure_io(IoMode::WriteFailure);
    failures += !check(nps::begin_resource_profile("solve"),
                       "a write-failure interval starts");
    metrics.operation = "solve";
    const nps::ResourceProfileResult write_failure = nps::finish_resource_profile(metrics);
    failures += !check(write_failure.interval_active && !write_failure.report_written &&
                           write_calls == 1 && close_calls == 1,
                       "a write failure is returned and the descriptor is closed");

    configure_io(IoMode::CloseFailure);
    failures += !check(nps::begin_resource_profile("kinematics"),
                       "a close-failure interval starts");
    metrics.operation = "kinematics";
    const nps::ResourceProfileResult close_failure = nps::finish_resource_profile(metrics);
    failures += !check(close_failure.interval_active && !close_failure.report_written &&
                           write_calls > 0 && close_calls == 1,
                       "a close failure cannot report successful persistence");

    const nps::ResourceProfileResult repeated = nps::finish_resource_profile(metrics);
    failures += !check(!repeated.interval_active && !repeated.report_written,
                       "a repeated finish cannot reuse a consumed interval");

    configure_io(IoMode::Success);
    nps::ResourceProfileMetrics failed_metrics;
    failed_metrics.operation = "differentiate";
    failed_metrics.request_failed = true;
    failed_metrics.render_ready_ms = 29;
    failed_metrics.lua_live_bytes = 3072;
    failed_metrics.arena_nodes = 51;
    failed_metrics.arena_child_slots = 73;
    failed_metrics.derivation_steps = 11;
    failed_metrics.rewrites = 7;
    failed_metrics.backend_calls = 2;
    failures += !check(nps::begin_resource_profile(failed_metrics.operation),
                       "an error-frame interval starts after a report persistence failure");
    const nps::ResourceProfileResult failed_request = nps::finish_resource_profile(failed_metrics);
    failures += !check(failed_request.interval_active && failed_request.report_written &&
                           report_contains("request_status = failed\n") &&
                           report_contains("solver_metrics_available = no\n"),
                       "error-frame finalization reports failure and absent solver metrics");
    failures += !check(report_contains("arena_nodes = unavailable\n") &&
                           report_contains("arena_child_slots = unavailable\n") &&
                           report_contains("derivation_steps = unavailable\n") &&
                           report_contains("rewrites = unavailable\n") &&
                           report_contains("backend_calls = unavailable\n"),
                       "unavailable solver counters never publish default or stale values");
    failures += !check(report_contains("lua_live_bytes = 3072\n") &&
                           report_contains("render_ready_ms = 29\n"),
                       "failed requests retain their measured error-frame heap and timing");
    const unsigned failed_write_calls = write_calls;
    const std::size_t failed_report_length = report_length;
    const nps::ResourceProfileResult repeated_failure = nps::finish_resource_profile(failed_metrics);
    failures += !check(!repeated_failure.interval_active && !repeated_failure.report_written &&
                           open_calls == 1 && close_calls == 1 && write_calls == failed_write_calls &&
                           report_length == failed_report_length,
                       "failed request finalization consumes the interval and writes exactly once");

    configure_io(IoMode::Success);
    nps::ResourceProfileMetrics recovered_metrics;
    recovered_metrics.operation = "integrate";
    recovered_metrics.solver_metrics_available = true;
    failures += !check(nps::begin_resource_profile(recovered_metrics.operation),
                       "a new request starts after failed-request finalization");
    const nps::ResourceProfileResult recovered = nps::finish_resource_profile(recovered_metrics);
    failures += !check(recovered.interval_active && recovered.report_written && open_calls == 1 &&
                           close_calls == 1 && report_contains("operation = integrate\n") &&
                           report_contains("request_status = completed\n") &&
                           report_contains("solver_metrics_available = yes\n") &&
                           report_contains("arena_nodes = 0\n") &&
                           report_contains("arena_child_slots = 0\n") &&
                           report_contains("derivation_steps = 0\n") &&
                           report_contains("rewrites = 0\n") &&
                           report_contains("backend_calls = 0\n"),
                       "recovery reports genuine zero counters only when explicitly available");

    for (bool request_failed : {false, true}) {
        configure_io(IoMode::Success);
        nps::ResourceProfileMetrics independent_metrics;
        independent_metrics.operation = "solve";
        independent_metrics.request_failed = request_failed;
        independent_metrics.solver_metrics_available = request_failed;
        independent_metrics.derivation_steps = 6;
        failures += !check(nps::begin_resource_profile(independent_metrics.operation),
                           "request status and solver availability start independent intervals");
        const nps::ResourceProfileResult independent = nps::finish_resource_profile(independent_metrics);
        failures += !check(independent.report_written &&
                               report_contains(request_failed ? "request_status = failed\n"
                                                              : "request_status = completed\n") &&
                               report_contains(request_failed ? "solver_metrics_available = yes\n"
                                                              : "solver_metrics_available = no\n") &&
                               report_contains(request_failed ? "derivation_steps = 6\n"
                                                              : "derivation_steps = unavailable\n"),
                           "request success does not imply availability and failure does not erase known counters");
    }

    for (const char *operation : {static_cast<const char *>(nullptr), "integrate"}) {
        configure_io(IoMode::Success);
        nps::ResourceProfileMetrics unmatched_metrics;
        unmatched_metrics.operation = operation;
        unmatched_metrics.request_failed = true;
        unmatched_metrics.solver_metrics_available = operation != nullptr;
        unmatched_metrics.render_ready_ms = operation ? 91 : 0;
        unmatched_metrics.lua_live_bytes = operation ? 8192 : 0;
        unmatched_metrics.arena_nodes = 12;
        unmatched_metrics.arena_child_slots = 19;
        unmatched_metrics.derivation_steps = 7;
        unmatched_metrics.rewrites = 4;
        unmatched_metrics.backend_calls = 2;
        failures += !check(nps::begin_resource_profile("differentiate"),
                           "validation cleanup and mismatched metrics start an interval");
        const nps::ResourceProfileResult unmatched = nps::finish_resource_profile(unmatched_metrics);
        failures += !check(unmatched.interval_active && unmatched.report_written &&
                               report_contains("operation = differentiate\n") &&
                               report_contains("operation_argument_match = no\n") &&
                               report_contains("request_status = failed\n"),
                           "unmatched metrics consume the original interval and identify the failed request");
        failures += !check(report_contains("render_ready_ms = unavailable\n") &&
                               report_contains("lua_live_bytes = unavailable\n"),
                           "unmatched timing and Lua heap values are unavailable rather than fabricated measurements");
        failures += !check(report_contains("solver_metrics_available = no\n") &&
                               report_contains("arena_nodes = unavailable\n") &&
                               report_contains("arena_child_slots = unavailable\n") &&
                               report_contains("derivation_steps = unavailable\n") &&
                               report_contains("rewrites = unavailable\n") &&
                               report_contains("backend_calls = unavailable\n"),
                           "unmatched solver counters are unavailable even when the caller marks them available");
        failures += !check(report_contains("native_requested_current_bytes = 0\n") &&
                               report_contains("headroom_before_contiguous_lower_bound_bytes = "),
                           "the interval retains its own native allocation and headroom observations");
        const nps::ResourceProfileResult consumed = nps::finish_resource_profile(unmatched_metrics);
        failures += !check(!consumed.interval_active && !consumed.report_written,
                           "unmatched cleanup cannot reuse the consumed interval");
    }

    nps::reset_resource_profile_io_for_testing();

    if (failures == 0)
        std::puts("nspire allocation probe: pass");
    return failures == 0 ? 0 : 1;
}
