#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "nps/platform/nspire/measurement.h"

namespace {

struct FakeAllocator {
    size_t maximum;
    unsigned allocations = 0;
    unsigned successful_allocations = 0;
    unsigned deallocations = 0;
};

void *fake_allocate(size_t bytes, void *context) {
    FakeAllocator *allocator = static_cast<FakeAllocator *>(context);
    ++allocator->allocations;
    if (bytes > allocator->maximum)
        return nullptr;
    ++allocator->successful_allocations;
    return allocator;
}

void fake_deallocate(void *, void *context) {
    FakeAllocator *allocator = static_cast<FakeAllocator *>(context);
    ++allocator->deallocations;
}

bool contains(const std::string &text, const char *expected) {
    return text.find(expected) != std::string::npos;
}

// A heap of separate free runs rather than one ceiling, because the walk's failures are about which
// run it moves to next and a single maximum cannot express that.
struct RunHeap {
    std::vector<size_t> runs;
    std::vector<std::pair<size_t, size_t>> live;
};

void *run_allocate(size_t bytes, void *context) {
    RunHeap *heap = static_cast<RunHeap *>(context);
    for (size_t index = 0; index < heap->runs.size(); ++index) {
        if (heap->runs[index] >= bytes) {
            heap->runs[index] -= bytes;
            heap->live.push_back({index, bytes});
            return &heap->runs[index];
        }
    }
    return nullptr;
}

void run_deallocate(void *, void *context) {
    RunHeap *heap = static_cast<RunHeap *>(context);
    if (heap->live.empty())
        return;
    heap->runs[heap->live.back().first] += heap->live.back().second;
    heap->live.pop_back();
}

// The walk used to end on the search's own output. The search returns a lower bound, so on a heap
// whose largest run is an odd multiple of the resolution the bisection undershoots the next run and
// the walk read that as an empty heap. 205 resolutions is the shape the handheld actually has.
bool total_free_walk_reaches_every_run() {
    const size_t resolution = 64u << 10;
    void *holds[64];

    RunHeap stranded;
    stranded.runs = {205 * resolution, resolution};
    const nps::AllocatorTotalFree found = nps::measure_total_free(
        run_allocate, run_deallocate, &stranded, holds, 64, 64u << 20, resolution);
    if (found.total_bytes != 206 * resolution || found.block_count != 2 ||
        found.largest_block_bytes != 205 * resolution || found.hold_limit_reached)
        return false;
    if (!stranded.live.empty())
        return false;

    // The same heap one resolution wider on the first run, which makes the ceiling dyadic and used
    // to pass. Both shapes have to work or the fix is a special case.
    RunHeap dyadic;
    dyadic.runs = {256 * resolution, resolution};
    const nps::AllocatorTotalFree unchanged = nps::measure_total_free(
        run_allocate, run_deallocate, &dyadic, holds, 64, 64u << 20, resolution);
    if (unchanged.total_bytes != 257 * resolution || unchanged.block_count != 2)
        return false;

    // A tail of several runs the old walk abandoned together rather than one.
    RunHeap tail;
    tail.runs = {205 * resolution, resolution, resolution, resolution};
    const nps::AllocatorTotalFree swept = nps::measure_total_free(
        run_allocate, run_deallocate, &tail, holds, 64, 64u << 20, resolution);
    if (swept.total_bytes != 208 * resolution || swept.block_count != 4)
        return false;

    // A run that is not a whole number of resolutions still loses under one resolution and no more,
    // and the walk keeps going rather than stopping at it.
    RunHeap ragged;
    ragged.runs = {205 * resolution + 1000, 100000};
    const nps::AllocatorTotalFree partial = nps::measure_total_free(
        run_allocate, run_deallocate, &ragged, holds, 64, 64u << 20, resolution);
    if (partial.block_count < 2 || partial.total_bytes + 2 * resolution < 205 * resolution + 101000 ||
        partial.total_bytes > 205 * resolution + 101000)
        return false;
    if (!ragged.live.empty())
        return false;

    RunHeap empty;
    const nps::AllocatorTotalFree nothing = nps::measure_total_free(
        run_allocate, run_deallocate, &empty, holds, 64, 64u << 20, resolution);
    return nothing.total_bytes == 0 && nothing.block_count == 0 && !nothing.hold_limit_reached;
}

constinit nps::AllocationTracker<4> constant_initialized_tracker;

bool allocation_and_interval_accounting() {
    int first = 0;
    int second = 0;
    int third = 0;
    constant_initialized_tracker.reset();
    if (!constant_initialized_tracker.record_allocation(&first, 40) ||
        !constant_initialized_tracker.record_allocation(&second, 24))
        return false;

    constant_initialized_tracker.begin_interval();
    nps::AllocationSnapshot interval = constant_initialized_tracker.snapshot();
    if (interval.baseline_requested_bytes != 64 || interval.current_requested_bytes != 64 ||
        interval.peak_requested_bytes != 64 || interval.allocation_count != 0 ||
        interval.free_count != 0 || interval.reallocation_count != 0 ||
        interval.failure_count != 0)
        return false;

    if (!constant_initialized_tracker.record_free(&first) ||
        !constant_initialized_tracker.record_allocation(&third, 80))
        return false;
    interval = constant_initialized_tracker.snapshot();
    if (interval.baseline_requested_bytes != 64 || interval.current_requested_bytes != 104 ||
        interval.peak_requested_bytes != 104 || interval.allocation_count != 1 ||
        interval.free_count != 1 || !interval.valid || interval.overflowed)
        return false;

    constant_initialized_tracker.reset();
    const nps::AllocationSnapshot reset = constant_initialized_tracker.snapshot();
    if (reset.baseline_requested_bytes != 0 || reset.current_requested_bytes != 0 ||
        reset.peak_requested_bytes != 0 || reset.allocation_count != 0 || !reset.valid ||
        reset.overflowed)
        return false;
    if (constant_initialized_tracker.record_free(&second))
        return false;
    const nps::AllocationSnapshot forgotten = constant_initialized_tracker.snapshot();
    return !forgotten.valid && forgotten.unknown_free_count == 1 &&
           forgotten.current_requested_bytes == 0;
}

bool null_failure_and_zero_size_accounting() {
    nps::AllocationTracker<4> tracker;
    int zero_malloc = 0;
    int zero_calloc = 0;
    if (!tracker.record_allocation(nullptr, 0) || !tracker.record_allocation(nullptr, 8) ||
        !tracker.record_allocation(&zero_malloc, 0) || !tracker.record_free(&zero_malloc) ||
        !tracker.record_calloc(nullptr, 0, std::numeric_limits<size_t>::max()) ||
        !tracker.record_calloc(&zero_calloc, 7, 0) || !tracker.record_free(&zero_calloc) ||
        !tracker.record_free(nullptr))
        return false;

    const nps::AllocationSnapshot snapshot = tracker.snapshot();
    return snapshot.current_requested_bytes == 0 && snapshot.peak_requested_bytes == 0 &&
           snapshot.allocation_count == 5 && snapshot.free_count == 3 &&
           snapshot.failure_count == 1 && snapshot.unknown_free_count == 0 && snapshot.valid &&
           !snapshot.overflowed;
}

bool reallocation_accounting() {
    nps::AllocationTracker<4> tracker;
    int first = 0;
    int second = 0;
    int third = 0;
    if (!tracker.record_allocation(&first, 16) ||
        !tracker.record_reallocation(&first, &first, 32) ||
        !tracker.record_reallocation(&first, &second, 8) ||
        !tracker.record_reallocation(&second, nullptr, 64) ||
        !tracker.record_reallocation(&second, nullptr, 0) ||
        !tracker.record_reallocation(nullptr, &third, 12) ||
        !tracker.record_reallocation(nullptr, nullptr, 7) ||
        !tracker.record_reallocation(nullptr, nullptr, 0) || !tracker.record_free(&third))
        return false;

    const nps::AllocationSnapshot snapshot = tracker.snapshot();
    return snapshot.current_requested_bytes == 0 && snapshot.peak_requested_bytes == 32 &&
           snapshot.allocation_count == 1 && snapshot.reallocation_count == 7 &&
           snapshot.free_count == 1 && snapshot.failure_count == 2 &&
           snapshot.unknown_reallocation_count == 0 && snapshot.valid && !snapshot.overflowed;
}

bool unknown_and_duplicate_accounting() {
    nps::AllocationTracker<4> tracker;
    int known = 0;
    int unknown = 0;
    if (!tracker.record_allocation(&known, 10) || tracker.record_allocation(&known, 10) ||
        tracker.record_free(&unknown) || tracker.record_free(&known) || tracker.record_free(&known) ||
        tracker.record_reallocation(&unknown, nullptr, 12))
        return false;

    const nps::AllocationSnapshot snapshot = tracker.snapshot();
    return snapshot.current_requested_bytes == 0 && snapshot.allocation_count == 2 &&
           snapshot.free_count == 3 && snapshot.reallocation_count == 1 &&
           snapshot.failure_count == 1 && snapshot.unknown_free_count == 2 &&
           snapshot.unknown_reallocation_count == 1 && !snapshot.valid && !snapshot.overflowed;
}

bool capacity_and_tombstone_accounting() {
    int first = 0;
    int second = 0;
    int third = 0;
    nps::AllocationTracker<1> reusable;
    if (!reusable.record_allocation(&first, 1) || !reusable.record_free(&first) ||
        !reusable.record_allocation(&third, 3) || !reusable.record_free(&third))
        return false;
    const nps::AllocationSnapshot reused = reusable.snapshot();
    if (!reused.valid || reused.current_requested_bytes != 0 || reused.peak_requested_bytes != 3)
        return false;

    nps::AllocationTracker<1> moving;
    if (!moving.record_allocation(&first, 4) ||
        !moving.record_reallocation(&first, &second, 6) || !moving.record_free(&second))
        return false;
    const nps::AllocationSnapshot moved = moving.snapshot();
    if (!moved.valid || moved.current_requested_bytes != 0 || moved.peak_requested_bytes != 6)
        return false;

    nps::AllocationTracker<1> full;
    if (!full.record_allocation(&first, 4) || full.record_allocation(&second, 5))
        return false;
    const nps::AllocationSnapshot overflow = full.snapshot();
    return overflow.current_requested_bytes == 4 && overflow.peak_requested_bytes == 4 &&
           overflow.allocation_count == 2 && !overflow.valid && overflow.overflowed;
}

bool arithmetic_overflow_accounting() {
    nps::AllocationTracker<2> calloc_tracker;
    if (calloc_tracker.record_calloc(nullptr, std::numeric_limits<size_t>::max(), 2))
        return false;
    const nps::AllocationSnapshot calloc_overflow = calloc_tracker.snapshot();
    if (calloc_overflow.allocation_count != 1 || calloc_overflow.failure_count != 1 ||
        calloc_overflow.current_requested_bytes != 0 || calloc_overflow.valid ||
        !calloc_overflow.overflowed)
        return false;

    int first = 0;
    int second = 0;
    nps::AllocationTracker<2> byte_tracker;
    if (!byte_tracker.record_allocation(&first, std::numeric_limits<size_t>::max()) ||
        byte_tracker.record_allocation(&second, 1))
        return false;
    const nps::AllocationSnapshot byte_overflow = byte_tracker.snapshot();
    return byte_overflow.current_requested_bytes == std::numeric_limits<size_t>::max() &&
           byte_overflow.peak_requested_bytes == std::numeric_limits<size_t>::max() &&
           byte_overflow.allocation_count == 2 && !byte_overflow.valid &&
           byte_overflow.overflowed;
}

bool zero_reallocation_contract() {
    int old_pointer = 0;
    int impossible_pointer = 0;
    nps::AllocationTracker<2> tracker;
    if (!tracker.record_allocation(&old_pointer, 11) ||
        tracker.record_reallocation(&old_pointer, &impossible_pointer, 0))
        return false;
    const nps::AllocationSnapshot snapshot = tracker.snapshot();
    return snapshot.current_requested_bytes == 11 && snapshot.reallocation_count == 1 &&
           !snapshot.valid && !snapshot.overflowed;
}

}  // namespace

int main() {
    unsigned failures = 0;

    if (!allocation_and_interval_accounting())
        ++failures;
    if (!null_failure_and_zero_size_accounting())
        ++failures;
    if (!reallocation_accounting())
        ++failures;
    if (!unknown_and_duplicate_accounting())
        ++failures;
    if (!capacity_and_tombstone_accounting())
        ++failures;
    if (!arithmetic_overflow_accounting())
        ++failures;
    if (!zero_reallocation_contract())
        ++failures;
    if (!total_free_walk_reaches_every_run())
        ++failures;

    nps::ProbeReport missing_stream(nullptr);
    missing_stream.text("ignored", "ignored");
    missing_stream.number("ignored", 1);
    if (missing_stream.ok())
        ++failures;

    FakeAllocator bounded{1500};
    nps::AllocatorHeadroom bounded_headroom =
        nps::measure_contiguous_headroom(fake_allocate, fake_deallocate, &bounded, 4096, 64);
    if (bounded_headroom.ceiling_reached || bounded_headroom.contiguous_lower_bound_bytes > 1500 ||
        1500 - bounded_headroom.contiguous_lower_bound_bytes >= 64 ||
        bounded.successful_allocations != bounded.deallocations)
        ++failures;

    FakeAllocator roomy{8192};
    nps::AllocatorHeadroom roomy_headroom =
        nps::measure_contiguous_headroom(fake_allocate, fake_deallocate, &roomy, 4096, 64);
    if (!roomy_headroom.ceiling_reached || roomy_headroom.contiguous_lower_bound_bytes != 4096 ||
        roomy.allocations != 1 || roomy.successful_allocations != 1 || roomy.deallocations != 1)
        ++failures;
    if (bounded_headroom.ceiling_reached == roomy_headroom.ceiling_reached ||
        bounded_headroom.contiguous_lower_bound_bytes >= bounded_headroom.search_ceiling_bytes ||
        roomy_headroom.contiguous_lower_bound_bytes != roomy_headroom.search_ceiling_bytes)
        ++failures;

    FILE *stream = std::tmpfile();
    if (!stream)
        return 2;

    nps::ProbeReport report(stream);
    report.text("schema", "nps-m0-probe-v1");
    report.number("zero", 0);
    report.number("two_groups", UINT64_C(4294967296), "counts");
    report.number("maximum", std::numeric_limits<uint64_t>::max(), "counts");
    report.headroom("before", bounded_headroom);

    nps::OperationMeasurement operation;
    operation.name = "linear_solve";
    operation.outcome = "solved";
    operation.iterations = 1;
    operation.elapsed_ticks = 3;
    operation.elapsed_milliseconds = 3;
    operation.arena_peak = {12, 18};
    operation.derivation_steps = 4;
    operation.cost = {7, 4, 0};
    operation.success = true;
    report.operation(operation);

    nps::ProbeCounts counts;
    counts.record(true);
    counts.record(false);
    report.summary(counts);
    if (!report.ok())
        ++failures;

    if (std::fflush(stream) != 0 || std::fseek(stream, 0, SEEK_END) != 0)
        return 2;
    const long length = std::ftell(stream);
    if (length < 0 || std::fseek(stream, 0, SEEK_SET) != 0)
        return 2;
    std::string output(static_cast<size_t>(length), '\0');
    if (!output.empty() && std::fread(output.data(), 1, output.size(), stream) != output.size())
        return 2;
    std::fclose(stream);

    const char *required[] = {
        "schema = nps-m0-probe-v1\n",
        "zero = 0\n",
        "two_groups = 4294967296 counts\n",
        "maximum = 18446744073709551615 counts\n",
        "allocator.before.contiguous_lower_bound = 1472 bytes\n",
        "allocator.before.resolution = 64 bytes\n",
        "operation.linear_solve.elapsed = 3 ticks\n",
        "operation.linear_solve.time = 3 milliseconds\n",
        "operation.linear_solve.arena_scope = operation_local_monotonic\n",
        "operation.linear_solve.arena_peak_nodes = 12 nodes\n",
        "operation.linear_solve.arena_peak_child_slots = 18 child_slots\n",
        "operation.linear_solve.derivation_steps = 4 steps\n",
        "operation.linear_solve.cost_rewrites = 7 rewrites\n",
        "summary.successes = 1 checks\n",
        "summary.failures = 1 checks\n",
    };
    for (const char *line : required) {
        if (!contains(output, line))
            ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "nspire measurement contract: %u failures\n%s", failures,
                     output.c_str());
        return 1;
    }
    std::puts("nspire measurement contract: ok");
    return 0;
}
