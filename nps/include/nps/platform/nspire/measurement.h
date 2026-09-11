#ifndef NPS_PLATFORM_NSPIRE_MEASUREMENT_H
#define NPS_PLATFORM_NSPIRE_MEASUREMENT_H

#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>

#include "nps/core/budgets.h"

namespace nps {

using ProbeAllocate = void *(*)(size_t, void *);
using ProbeDeallocate = void (*)(void *, void *);

struct AllocatorHeadroom {
    size_t contiguous_lower_bound_bytes = 0;
    size_t resolution_bytes = 0;
    size_t search_ceiling_bytes = 0;
    bool ceiling_reached = false;
};

// Total free is the sum of what can be held at once, which is a different number from the largest
// single block and the one PERF-011 asks for. A solve fragments rather than consumes, so the two
// move independently and the contiguous one runs out first.
struct AllocatorTotalFree {
    size_t total_bytes = 0;
    size_t largest_block_bytes = 0;
    size_t block_count = 0;
    size_t resolution_bytes = 0;
    bool hold_limit_reached = false;
};

// Baseline and current cover observed requests since reset, peak is interval-local, not process memory.
struct AllocationSnapshot {
    size_t baseline_requested_bytes = 0;
    size_t current_requested_bytes = 0;
    size_t peak_requested_bytes = 0;
    size_t allocation_count = 0;
    size_t free_count = 0;
    size_t reallocation_count = 0;
    size_t failure_count = 0;
    size_t unknown_free_count = 0;
    size_t unknown_reallocation_count = 0;
    bool valid = true;
    bool overflowed = false;
};

template <size_t Capacity>
class AllocationTracker {
    static_assert(Capacity > 0);
    static_assert(Capacity < std::numeric_limits<size_t>::max());

  public:
    constexpr AllocationTracker() noexcept = default;

    // Cold reset forgets all live pointers and history.
    void reset() noexcept {
        for (Entry &entry : entries_)
            entry = {};
        snapshot_ = {};
    }

    // An interval preserves live pointers and resets counters and peak to the current baseline.
    void begin_interval() noexcept {
        snapshot_.baseline_requested_bytes = snapshot_.current_requested_bytes;
        snapshot_.peak_requested_bytes = snapshot_.current_requested_bytes;
        snapshot_.allocation_count = 0;
        snapshot_.free_count = 0;
        snapshot_.reallocation_count = 0;
        snapshot_.failure_count = 0;
        snapshot_.unknown_free_count = 0;
        snapshot_.unknown_reallocation_count = 0;
    }

    // Record methods return whether accounting remains exact, not whether allocation succeeded.
    bool record_allocation(void *pointer, size_t requested_bytes) noexcept {
        const bool counted = increment(snapshot_.allocation_count);
        return record_new_pointer(pointer, requested_bytes, counted);
    }

    bool record_calloc(void *pointer, size_t count, size_t element_size) noexcept {
        bool counted = increment(snapshot_.allocation_count);
        size_t requested_bytes = 0;
        if (__builtin_mul_overflow(count, element_size, &requested_bytes)) {
            if (!pointer)
                counted = increment(snapshot_.failure_count) && counted;
            mark_overflow();
            return false;
        }
        return record_new_pointer(pointer, requested_bytes, counted);
    }

    bool record_free(void *pointer) noexcept {
        const bool counted = increment(snapshot_.free_count);
        if (!pointer)
            return counted && snapshot_.valid;

        Entry *entry = find_entry(pointer);
        if (!entry) {
            increment(snapshot_.unknown_free_count);
            mark_invalid();
            return false;
        }

        size_t current_requested_bytes = 0;
        if (__builtin_sub_overflow(snapshot_.current_requested_bytes, entry->requested_bytes,
                                   &current_requested_bytes)) {
            mark_overflow();
            erase(*entry);
            return false;
        }
        snapshot_.current_requested_bytes = current_requested_bytes;
        erase(*entry);
        return counted && snapshot_.valid;
    }

    bool record_reallocation(void *old_pointer, void *new_pointer,
                             size_t requested_bytes) noexcept {
        bool counted = increment(snapshot_.reallocation_count);
        // Ndl zero-size realloc frees the old pointer and returns null.
        if (requested_bytes == 0) {
            if (new_pointer) {
                mark_invalid();
                return false;
            }
            if (!old_pointer)
                return counted && snapshot_.valid;
            return remove_reallocated_pointer(old_pointer, counted);
        }

        if (!old_pointer)
            return record_new_pointer(new_pointer, requested_bytes, counted);

        Entry *entry = find_entry(old_pointer);
        if (!entry) {
            counted = increment(snapshot_.unknown_reallocation_count) && counted;
            if (!new_pointer)
                counted = increment(snapshot_.failure_count) && counted;
            mark_invalid();
            return false;
        }

        if (!new_pointer) {
            counted = increment(snapshot_.failure_count) && counted;
            return counted && snapshot_.valid;
        }
        if (new_pointer != old_pointer && find_entry(new_pointer)) {
            mark_invalid();
            return false;
        }

        size_t without_old_pointer = 0;
        size_t current_requested_bytes = 0;
        if (__builtin_sub_overflow(snapshot_.current_requested_bytes, entry->requested_bytes,
                                   &without_old_pointer) ||
            __builtin_add_overflow(without_old_pointer, requested_bytes,
                                   &current_requested_bytes)) {
            mark_overflow();
            return false;
        }

        if (new_pointer != old_pointer) {
            erase(*entry);
            entry = insertion_slot(new_pointer);
            if (!entry) {
                mark_overflow();
                return false;
            }
            entry->pointer = new_pointer;
            entry->state = EntryState::Occupied;
        }
        entry->requested_bytes = requested_bytes;
        snapshot_.current_requested_bytes = current_requested_bytes;
        snapshot_.peak_requested_bytes =
            std::max(snapshot_.peak_requested_bytes, current_requested_bytes);
        return counted && snapshot_.valid;
    }

    AllocationSnapshot snapshot() const noexcept { return snapshot_; }
    bool valid() const noexcept { return snapshot_.valid; }

  private:
    enum class EntryState : uint8_t { Empty, Occupied, Deleted };

    struct Entry {
        void *pointer = nullptr;
        size_t requested_bytes = 0;
        EntryState state = EntryState::Empty;
    };

    bool increment(size_t &counter) noexcept {
        size_t incremented = 0;
        if (__builtin_add_overflow(counter, size_t{1}, &incremented)) {
            mark_overflow();
            return false;
        }
        counter = incremented;
        return true;
    }

    bool record_new_pointer(void *pointer, size_t requested_bytes, bool counted) noexcept {
        if (!pointer) {
            if (requested_bytes != 0)
                counted = increment(snapshot_.failure_count) && counted;
            return counted && snapshot_.valid;
        }
        if (find_entry(pointer)) {
            mark_invalid();
            return false;
        }

        Entry *entry = insertion_slot(pointer);
        if (!entry) {
            mark_overflow();
            return false;
        }

        size_t current_requested_bytes = 0;
        if (__builtin_add_overflow(snapshot_.current_requested_bytes, requested_bytes,
                                   &current_requested_bytes)) {
            mark_overflow();
            return false;
        }
        entry->pointer = pointer;
        entry->requested_bytes = requested_bytes;
        entry->state = EntryState::Occupied;
        snapshot_.current_requested_bytes = current_requested_bytes;
        snapshot_.peak_requested_bytes =
            std::max(snapshot_.peak_requested_bytes, current_requested_bytes);
        return counted && snapshot_.valid;
    }

    bool remove_reallocated_pointer(void *pointer, bool counted) noexcept {
        Entry *entry = find_entry(pointer);
        if (!entry) {
            increment(snapshot_.unknown_reallocation_count);
            mark_invalid();
            return false;
        }

        size_t current_requested_bytes = 0;
        if (__builtin_sub_overflow(snapshot_.current_requested_bytes, entry->requested_bytes,
                                   &current_requested_bytes)) {
            mark_overflow();
            erase(*entry);
            return false;
        }
        snapshot_.current_requested_bytes = current_requested_bytes;
        erase(*entry);
        return counted && snapshot_.valid;
    }

    Entry *find_entry(void *pointer) noexcept {
        size_t index = std::hash<void *>{}(pointer) % Capacity;
        for (size_t visited = 0; visited < Capacity; ++visited) {
            Entry &entry = entries_[index];
            if (entry.state == EntryState::Empty)
                return nullptr;
            if (entry.state == EntryState::Occupied && entry.pointer == pointer)
                return &entry;
            advance(index);
        }
        return nullptr;
    }

    Entry *insertion_slot(void *pointer) noexcept {
        Entry *deleted = nullptr;
        size_t index = std::hash<void *>{}(pointer) % Capacity;
        for (size_t visited = 0; visited < Capacity; ++visited) {
            Entry &entry = entries_[index];
            if (entry.state == EntryState::Empty)
                return deleted ? deleted : &entry;
            if (entry.state == EntryState::Deleted && !deleted)
                deleted = &entry;
            advance(index);
        }
        return deleted;
    }

    static void erase(Entry &entry) noexcept {
        entry.pointer = nullptr;
        entry.requested_bytes = 0;
        entry.state = EntryState::Deleted;
    }

    static void advance(size_t &index) noexcept {
        ++index;
        if (index == Capacity)
            index = 0;
    }

    void mark_invalid() noexcept { snapshot_.valid = false; }

    void mark_overflow() noexcept {
        snapshot_.valid = false;
        snapshot_.overflowed = true;
    }

    std::array<Entry, Capacity> entries_{};
    AllocationSnapshot snapshot_{};
};

inline AllocatorHeadroom measure_contiguous_headroom(ProbeAllocate allocate,
                                                      ProbeDeallocate deallocate, void *context,
                                                      size_t search_ceiling_bytes,
                                                      size_t resolution_bytes) {
    AllocatorHeadroom headroom;
    headroom.resolution_bytes = resolution_bytes;
    headroom.search_ceiling_bytes = search_ceiling_bytes;
    if (!allocate || !deallocate || search_ceiling_bytes == 0 || resolution_bytes == 0)
        return headroom;

    if (void *block = allocate(search_ceiling_bytes, context)) {
        deallocate(block, context);
        headroom.contiguous_lower_bound_bytes = search_ceiling_bytes;
        headroom.ceiling_reached = true;
        return headroom;
    }

    size_t lower = 0;
    size_t upper = search_ceiling_bytes;
    while (upper - lower > resolution_bytes) {
        const size_t candidate = lower + (upper - lower) / 2;
        void *block = allocate(candidate, context);
        if (block) {
            deallocate(block, context);
            lower = candidate;
        } else {
            upper = candidate;
        }
    }
    headroom.contiguous_lower_bound_bytes = lower;
    return headroom;
}

inline void *probe_malloc(size_t bytes, void *) { return std::malloc(bytes); }
inline void probe_free(void *block, void *) { std::free(block); }

inline AllocatorHeadroom measure_contiguous_headroom(size_t search_ceiling_bytes = 64u << 20,
                                                      size_t resolution_bytes = 64u << 10) {
    const int saved_errno = errno;
    AllocatorHeadroom headroom = measure_contiguous_headroom(
        probe_malloc, probe_free, nullptr, search_ceiling_bytes, resolution_bytes);
    errno = saved_errno;
    return headroom;
}

inline unsigned allocator_headroom_kb() {
    return static_cast<unsigned>(measure_contiguous_headroom().contiguous_lower_bound_bytes >> 10);
}

// Takes the largest block it can get, holds it, and repeats, because fixed chunks lose whatever does
// not divide the free runs. The caller supplies the hold array: allocating one here would perturb the
// heap being measured. A run that fills the array says so rather than reporting a short total as a
// whole one.
inline AllocatorTotalFree measure_total_free(ProbeAllocate allocate, ProbeDeallocate deallocate,
                                             void *context, void **holds, size_t hold_capacity,
                                             size_t search_ceiling_bytes,
                                             size_t resolution_bytes) {
    AllocatorTotalFree total;
    total.resolution_bytes = resolution_bytes;
    if (!allocate || !deallocate || !holds || hold_capacity == 0 || search_ceiling_bytes == 0 ||
        resolution_bytes == 0)
        return total;

    size_t ceiling = search_ceiling_bytes;
    while (total.block_count < hold_capacity) {
        const AllocatorHeadroom headroom = measure_contiguous_headroom(
            allocate, deallocate, context, ceiling, resolution_bytes);
        // The search returns a lower bound, so a result under the resolution means the search
        // undershot and not that the heap is empty. Asking the allocator for one resolution is the
        // difference between an estimate and a fact, and only the fact can end the walk.
        const size_t block = headroom.contiguous_lower_bound_bytes;
        const size_t take = block < resolution_bytes ? resolution_bytes : block;
        void *held = allocate(take, context);
        if (!held) {
            if (take <= resolution_bytes)
                break;
            ceiling = take - resolution_bytes;
            continue;
        }
        if (total.block_count == 0)
            total.largest_block_bytes = take;
        holds[total.block_count++] = held;
        total.total_bytes += take;
        ceiling = take;
    }
    total.hold_limit_reached = total.block_count == hold_capacity;

    for (size_t index = 0; index < total.block_count; ++index)
        deallocate(holds[index], context);
    return total;
}

// The walk reported block_count 7 to 9 across the budgets lane's four emulator runs, one block per
// free run. The cap has to cover the worst case rather than that, and the worst case is every block
// coming back one resolution at a time, which is the free total over the resolution: 306 for the
// 19609 KB those runs read. 1024 leaves room for a heap three times as fragmented.
inline AllocatorTotalFree measure_total_free(size_t search_ceiling_bytes = 64u << 20,
                                             size_t resolution_bytes = 64u << 10) {
    static void *holds[1024];
    const int saved_errno = errno;
    const AllocatorTotalFree total =
        measure_total_free(probe_malloc, probe_free, nullptr, holds, sizeof(holds) / sizeof(*holds),
                           search_ceiling_bytes, resolution_bytes);
    errno = saved_errno;
    return total;
}

struct ArenaPeak {
    size_t nodes = 0;
    size_t child_slots = 0;
};

struct OperationMeasurement {
    const char *name = "";
    const char *outcome = "";
    uint32_t iterations = 0;
    uint32_t elapsed_ticks = 0;
    uint32_t elapsed_milliseconds = 0;
    ArenaPeak arena_peak;
    size_t derivation_steps = 0;
    Cost cost;
    bool success = false;
};

struct ProbeCounts {
    size_t successes = 0;
    size_t failures = 0;

    void record(bool success) {
        if (success)
            ++successes;
        else
            ++failures;
    }
};

class ProbeReport {
  public:
    explicit ProbeReport(FILE *stream) : stream_(stream), ok_(stream != nullptr) {}

    void text(const char *key, const char *value) {
        if (!stream_)
            return;
        wrote(std::fprintf(stream_, "%s = %s\n", key, value));
    }

    void number(const char *key, uint64_t value, const char *unit = nullptr) {
        if (!stream_)
            return;
        wrote(std::fprintf(stream_, "%s = ", key));
        write_unsigned(value);
        if (unit)
            wrote(std::fprintf(stream_, " %s\n", unit));
        else
            wrote(std::fputc('\n', stream_));
    }

    void headroom(const char *scope, const AllocatorHeadroom &measurement) {
        if (!stream_)
            return;
        scoped_number("allocator", scope, "contiguous_lower_bound",
                      measurement.contiguous_lower_bound_bytes, "bytes");
        scoped_number("allocator", scope, "resolution", measurement.resolution_bytes, "bytes");
        scoped_number("allocator", scope, "search_ceiling", measurement.search_ceiling_bytes,
                      "bytes");
        scoped_text("allocator", scope, "ceiling_reached",
                    measurement.ceiling_reached ? "yes" : "no");
    }

    void operation(const OperationMeasurement &measurement) {
        if (!stream_)
            return;
        scoped_text("operation", measurement.name, "outcome", measurement.outcome);
        scoped_text("operation", measurement.name, "success", measurement.success ? "yes" : "no");
        scoped_number("operation", measurement.name, "iterations", measurement.iterations, "solves");
        scoped_number("operation", measurement.name, "elapsed", measurement.elapsed_ticks, "ticks");
        scoped_number("operation", measurement.name, "time", measurement.elapsed_milliseconds,
                      "milliseconds");
        scoped_text("operation", measurement.name, "arena_scope", "operation_local_monotonic");
        scoped_number("operation", measurement.name, "arena_peak_nodes",
                      measurement.arena_peak.nodes, "nodes");
        scoped_number("operation", measurement.name, "arena_peak_child_slots",
                      measurement.arena_peak.child_slots, "child_slots");
        scoped_number("operation", measurement.name, "derivation_steps",
                      measurement.derivation_steps, "steps");
        scoped_number("operation", measurement.name, "cost_rewrites", measurement.cost.rewrites,
                      "rewrites");
        scoped_number("operation", measurement.name, "cost_steps", measurement.cost.steps, "steps");
        scoped_number("operation", measurement.name, "cost_backend_calls",
                      measurement.cost.backend_calls, "calls");
    }

    void summary(const ProbeCounts &counts) {
        number("summary.successes", counts.successes, "checks");
        number("summary.failures", counts.failures, "checks");
    }

    bool ok() const { return ok_; }

  private:
    void write_unsigned(uint64_t value) {
        char digits[32];
        const auto conversion = std::to_chars(digits, digits + sizeof digits, value);
        if (conversion.ec != std::errc()) {
            ok_ = false;
            return;
        }
        wrote(std::fprintf(stream_, "%.*s", static_cast<int>(conversion.ptr - digits), digits));
    }

    void scoped_text(const char *group, const char *scope, const char *field, const char *value) {
        wrote(std::fprintf(stream_, "%s.%s.%s = %s\n", group, scope, field, value));
    }

    void scoped_number(const char *group, const char *scope, const char *field, uint64_t value,
                       const char *unit) {
        wrote(std::fprintf(stream_, "%s.%s.%s = ", group, scope, field));
        write_unsigned(value);
        wrote(std::fprintf(stream_, " %s\n", unit));
    }

    void wrote(int count) {
        if (count < 0)
            ok_ = false;
    }

    FILE *stream_;
    bool ok_;
};

}  // namespace nps

#endif
