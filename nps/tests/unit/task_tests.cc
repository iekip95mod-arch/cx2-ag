#include "nps/core/task.h"
#include "nps/core/budgets.h"
#include "unit/adapter_tests.h"

#include <array>
#include <cstdint>

namespace nps {
namespace {

template<class T> concept HasDriverState = requires(T &task) {
    task.state();
    task.result();
    task.advance(1);
};
static_assert(!HasDriverState<Coroutine<int>>);
static_assert(HasDriverState<Task<int>> && !std::is_default_constructible_v<Task<int>>);

struct Trace {
    std::array<int, 9> work{};
    std::array<int, 3> entries{};
    std::array<int, 3> exits{};
    std::array<int, 3> exit_order{};
    size_t completed = 0;
    size_t exited = 0;
    bool accepted_child = false;
};

struct Lifetime {
    Trace &trace;
    size_t depth;
    Lifetime(Trace &owner, size_t level) : trace(owner), depth(level) { ++trace.entries[depth]; }
    ~Lifetime() {
        ++trace.exits[depth];
        trace.exit_order[trace.exited++] = static_cast<int>(depth);
    }
};

Coroutine<int> leaf(TaskContext &context, Trace &trace) {
    Lifetime lifetime(trace, 2);
    int sum = 0;
    for (int term = 1; term <= 5; ++term) {
        co_await context.checkpoint();
        trace.work[trace.completed++] = term;
        sum += term;
    }
    co_return sum;
}

Coroutine<int> middle(TaskContext &context, Trace &trace) {
    Lifetime lifetime(trace, 1);
    co_await context.checkpoint();
    trace.work[trace.completed++] = 20;
    int sum = co_await leaf(context, trace);
    trace.accepted_child = true;
    co_await context.checkpoint();
    trace.work[trace.completed++] = 21;
    co_return sum + 2;
}

Coroutine<int> root(TaskContext &context, Trace &trace) {
    Lifetime lifetime(trace, 0);
    co_await context.checkpoint();
    trace.work[trace.completed++] = 30;
    int sum = co_await middle(context, trace);
    co_await context.checkpoint();
    trace.work[trace.completed++] = 31;
    co_return sum + 2;
}

void schedules(TestSink &sink) {
    constexpr std::array<int, 9> expected{30, 20, 1, 2, 3, 4, 5, 21, 31};
    for (size_t slice = 1; slice <= 12; ++slice) {
        alignas(std::max_align_t) std::array<std::byte, 4096> storage;
        TaskContext context(storage);
        Trace trace;
        auto task = make_task(context, root, trace);
        sink.check(task.state() == TaskState::Pending && task.result() == nullptr &&
                       trace.completed == 0 && trace.entries[0] == 0,
                   "task construction leaves all computation pending");
        sink.check(task.advance(0) == TaskState::Pending && trace.entries[0] == 0,
                   "zero work slice does not enter the coroutine");
        for (int call = 0; call < 12 && task.state() == TaskState::Pending; ++call) {
            size_t before = trace.completed;
            task.advance(slice);
            sink.check(trace.completed > before && trace.completed - before <= slice,
                       "advance performs only its allotted work units");
            sink.check(trace.completed == 9 || (task.state() == TaskState::Pending && !task.result()),
                       "unfinished native computation has no completed answer");
        }
        sink.check(task.state() == TaskState::Complete && task.result() && *task.result() == 19,
                   "nested continuation produces the complete value");
        sink.check(trace.work == expected && trace.completed == 9 && trace.accepted_child,
                   "slice schedules preserve order and local accumulated values");
        sink.check(trace.entries == std::array<int, 3>{1, 1, 1} && trace.entries == trace.exits &&
                       trace.exit_order == std::array<int, 3>{2, 1, 0},
                   "nested bodies enter once and destroy locals from child to parent");
        sink.check(context.live_frames() == 1 && context.peak_bytes() <= context.capacity(),
                   "completed children release frames within the byte capacity");
        task.advance(1);
        sink.check(trace.completed == 9, "advancing completion never repeats work");
        task.reset();
        sink.check(context.live_frames() == 0 && context.used_bytes() == 0,
                   "reset releases the entire frame chain");
    }
}

void cancellation_and_destruction(TestSink &sink) {
    for (size_t stop = 0; stop < 9; ++stop) {
        alignas(std::max_align_t) std::array<std::byte, 4096> storage;
        TaskContext context(storage);
        Trace trace;
        auto task = make_task(context, root, trace);
        for (size_t unit = 0; unit < stop; ++unit)
            task.advance(1);
        task.cancel();
        sink.check(task.state() == TaskState::Cancelled && task.result() == nullptr &&
                       trace.completed == stop && context.stop_state() == TaskState::Cancelled,
                   "cancellation at every checkpoint preserves the work boundary");
        sink.check(context.live_frames() == 0 && context.used_bytes() == 0 && trace.entries == trace.exits,
                   "cancellation destroys suspended parent and child frames");
        task.advance(4);
        sink.check(trace.completed == stop, "a cancelled task cannot restart");
    }
    for (size_t stop = 0; stop <= 9; ++stop) {
        alignas(std::max_align_t) std::array<std::byte, 4096> storage;
        TaskContext context(storage);
        Trace trace;
        {
            auto first = make_task(context, root, trace);
            first.advance(stop);
            Task<int> moved(std::move(first));
            auto assigned = make_task(context, root, trace);
            assigned = std::move(moved);
            sink.check(first.state() == TaskState::Invalid && moved.state() == TaskState::Invalid,
                       "task moves remove ownership from the source");
        }
        sink.check(trace.completed == stop && trace.entries == trace.exits &&
                       context.live_frames() == 0 && context.used_bytes() == 0,
                   "destruction after moves releases each suspended lifetime once");
    }
}

void allocation_limits(TestSink &sink) {
    bool saw_parent_refusal = false;
    bool saw_leaf_refusal = false;
    bool saw_success = false;
    for (size_t capacity = 0; capacity <= 1024; capacity += 16) {
        alignas(std::max_align_t) std::array<std::byte, 1024> storage;
        TaskContext context(std::span<std::byte>(storage.data(), capacity));
        Trace trace;
        auto task = make_task(context, root, trace);
        task.advance(9);
        sink.check(context.peak_bytes() <= capacity && context.used_bytes() <= capacity,
                   "frame headers and alignment stay inside the supplied byte capacity");
        if (task.state() == TaskState::AllocationFailed) {
            saw_parent_refusal |= trace.completed == 1;
            saw_leaf_refusal |= trace.completed == 2;
            sink.check(!task.result() && !trace.accepted_child && trace.completed <= 2,
                       "failed child allocation never supplies a default value to its parent");
            sink.check(context.live_frames() == 0 && context.used_bytes() == 0 &&
                           trace.entries == trace.exits,
                       "allocation refusal unwinds all already allocated task frames");
            context.cancel();
            sink.check(context.stop_state() == TaskState::AllocationFailed,
                       "allocation failure stays distinct from later cancellation");
        } else {
            saw_success |= task.state() == TaskState::Complete;
            sink.check(task.result() && *task.result() == 19,
                       "sufficient frame capacity preserves the full result");
        }
    }
    sink.check(saw_parent_refusal && saw_leaf_refusal && saw_success,
               "byte-cap sweep crosses root, parent, child and successful allocation boundaries");

    alignas(std::max_align_t) std::array<std::byte, 4096> storage;
    TaskContext context(storage);
    Trace trace;
    auto first = leaf(context, trace);
    size_t one = context.used_bytes();
    auto second = leaf(context, trace);
    first = Coroutine<int>{};
    sink.check(context.live_frames() == 1 && context.used_bytes() > one,
               "out-of-order frame destruction retains only bounded arena space");
    second = Coroutine<int>{};
    sink.check(context.live_frames() == 0 && context.used_bytes() == 0,
               "releasing the later frame reclaims the earlier freed block");
    auto reused = leaf(context, trace);
    sink.check(context.used_bytes() == one, "released frame space is reused");
}

struct Owned {
    int *live;
    int *destroyed;
    Owned(int &count, int &deaths) noexcept : live(&count), destroyed(&deaths) { ++*live; }
    Owned(const Owned &) = delete;
    Owned(Owned &&other) noexcept
        : live(std::exchange(other.live, nullptr)), destroyed(other.destroyed) {}
    ~Owned() { if (live) { --*live; ++*destroyed; } }
};

Coroutine<Owned> own_value(TaskContext &context, int &live, int &destroyed) {
    co_await context.checkpoint();
    co_return Owned(live, destroyed);
}

Coroutine<Owned> pass_value(TaskContext &context, int &live, int &destroyed) {
    Owned value = co_await own_value(context, live, destroyed);
    co_await context.checkpoint();
    co_return std::move(value);
}

Coroutine<int> metered(TaskContext &context, Meter &meter, int &work) {
    for (int count = 0; count < 7; ++count) {
        co_await context.checkpoint();
        if (!meter.rewrite())
            co_return -1;
        ++work;
    }
    co_return work;
}

Coroutine<int> alien_child(TaskContext &context, TaskContext &other, Trace &trace) {
    co_await context.checkpoint();
    co_return co_await leaf(other, trace);
}

Coroutine<int> one_unit(TaskContext &context) {
    co_await context.checkpoint();
    co_return 1;
}

Coroutine<int> foreign_checkpoint(TaskContext &context, TaskContext &other, int &work) {
    static_cast<void>(context);
    for (int unit = 0; unit < 5; ++unit) {
        co_await other.checkpoint();
        ++work;
    }
    co_return work;
}

void checkpoint_ownership(TestSink &sink) {
    for (bool available_credit : {false, true}) {
        alignas(std::max_align_t) std::array<std::byte, 4096> storage;
        alignas(std::max_align_t) std::array<std::byte, 4096> other_storage;
        TaskContext context(storage);
        TaskContext other(other_storage);
        if (available_credit) {
            auto credit = make_task(other, one_unit);
            credit.advance(10);
        }
        int work = 0;
        auto task = make_task(context, foreign_checkpoint, other, work);
        task.advance(1);
        task.advance(1);
        sink.check(task.state() == TaskState::Invalid && work == 0 && !task.result(),
                   "foreign checkpoints are rejected before consuming available credit");
        sink.check(context.live_frames() == 0 && other.live_frames() == 0,
                   "foreign checkpoint refusal releases the owning task");
    }
}

Coroutine<int> cancelled_child(TaskContext &context, int &accepted) {
    co_await context.checkpoint();
    context.cancel();
    int value = co_await one_unit(context);
    ++accepted;
    co_return value;
}

void factory_context(TestSink &sink) {
    for (size_t capacity : {size_t{0}, size_t{4096}}) {
        alignas(std::max_align_t) std::array<std::byte, 4096> storage;
        TaskContext context(std::span<std::byte>(storage.data(), capacity));
        context.cancel();
        int invoked = 0;
        auto task = make_task(context, [&invoked](TaskContext &owner) {
            ++invoked;
            return one_unit(owner);
        });
        sink.check(task.state() == TaskState::Cancelled && !task.result() &&
                       task.advance(1) == TaskState::Cancelled,
                   "cancelled context binds the root status even without frame storage");
        sink.check(invoked == 0 && context.live_frames() == 0 && context.peak_bytes() == 0,
                   "cancelled factory never invokes its producer or allocates a frame");
    }
    TaskContext empty(std::span<std::byte>{});
    auto failed = make_task(empty, one_unit);
    sink.check(failed.state() == TaskState::AllocationFailed && !failed.result(),
               "root allocation refusal remains an explicit terminal status");
    empty.cancel();
    int invoked = 0;
    auto refused = make_task(empty, [&invoked](TaskContext &owner) {
        ++invoked;
        return one_unit(owner);
    });
    sink.check(refused.advance(1) == TaskState::AllocationFailed && !refused.result() && invoked == 0,
               "a previously failed context refuses factory work without changing its reason");

    alignas(std::max_align_t) std::array<std::byte, 4096> storage;
    alignas(std::max_align_t) std::array<std::byte, 4096> other_storage;
    TaskContext context(storage);
    TaskContext other(other_storage);
    Trace trace;
    auto foreign = make_task(context, [&other, &trace](TaskContext &) { return leaf(other, trace); });
    sink.check(foreign.state() == TaskState::Invalid && !foreign.result() &&
                   foreign.advance(1) == TaskState::Invalid && trace.entries[2] == 0,
               "root factory rejects a producer frame from another context before execution");
    sink.check(context.live_frames() == 0 && other.live_frames() == 0 &&
                   other.stop_state() == TaskState::Pending,
               "foreign root refusal releases the frame without stopping its source context");

    int accepted = 0;
    auto cancelled = make_task(other, cancelled_child, accepted);
    sink.check(cancelled.advance(1) == TaskState::Cancelled && !cancelled.result() && accepted == 0 &&
                   other.live_frames() == 0 && other.used_bytes() == 0,
               "failed child creation preserves prior parent cancellation and unwinds its frame");
}

void values_and_context(TestSink &sink) {
    alignas(std::max_align_t) std::array<std::byte, 4096> storage;
    TaskContext context(storage);
    int live = 0;
    int destroyed = 0;
    {
        auto task = make_task(context, pass_value, live, destroyed);
        sink.check(task.advance(1) == TaskState::Pending && live == 1 && destroyed == 0,
                   "move-only child result survives while its parent is suspended");
        sink.check(task.advance(1) == TaskState::Complete && task.result() && live == 1,
                   "move-only ownership reaches the completed task result");
    }
    sink.check(live == 0 && destroyed == 1, "move-only result ownership is destroyed once");
    Budget budget;
    Meter meter(budget);
    int work = 0;
    auto task = make_task(context, metered, meter, work);
    while (task.state() == TaskState::Pending)
        task.advance(2);
    sink.check(task.result() && *task.result() == 7 && meter.cost().rewrites == 7,
               "existing Meter owns cumulative work across all task slices");
    task.reset();

    alignas(std::max_align_t) std::array<std::byte, 4096> other_storage;
    TaskContext other(other_storage);
    Trace trace;
    auto invalid = make_task(context, alien_child, other, trace);
    sink.check(invalid.advance(1) == TaskState::Invalid && !invalid.result() &&
                   context.live_frames() == 0 && other.live_frames() == 0,
               "awaiting a child from another job refuses without running it");
    sink.check(trace.completed == 0, "an alien child never executes against another job");
}

}

void run_task_tests(TestSink &sink) {
    schedules(sink);
    cancellation_and_destruction(sink);
    allocation_limits(sink);
    values_and_context(sink);
    checkpoint_ownership(sink);
    factory_context(sink);
}

}
