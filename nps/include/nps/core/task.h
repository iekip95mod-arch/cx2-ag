#ifndef NPS_CORE_TASK_H
#define NPS_CORE_TASK_H

#include <coroutine>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace nps {

enum class TaskState { Pending, Complete, Cancelled, AllocationFailed, Invalid };

template<class T> class Task;
template<class T> class Coroutine;

class TaskContext {
  public:
    // Storage and this context must outlive every task using them.
    explicit TaskContext(std::span<std::byte> storage) noexcept;
    TaskContext(const TaskContext &) = delete;
    TaskContext &operator=(const TaskContext &) = delete;

    class Checkpoint {
      public:
        explicit Checkpoint(TaskContext &context) noexcept : context_(context) {}
        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> paused) const noexcept;
        void await_resume() const noexcept;
      private:
        template<class T> friend class Coroutine;
        TaskContext &context_;
    };

    // Consume one slice unit before the next bounded piece of work.
    Checkpoint checkpoint() noexcept { return Checkpoint(*this); }
    void cancel() noexcept { stop(TaskState::Cancelled); }
    TaskState stop_state() const noexcept { return stopped_; }
    size_t capacity() const noexcept { return capacity_; }
    size_t used_bytes() const noexcept { return used_; }
    size_t peak_bytes() const noexcept { return peak_; }
    size_t live_frames() const noexcept { return live_frames_; }

  private:
    template<class T> friend class Task;
    template<class T> friend class Coroutine;
    struct Frame;
    void *allocate(size_t bytes) noexcept;
    static void release(void *frame) noexcept;
    void stop(TaskState state) noexcept;
    bool start(std::coroutine_handle<> root) noexcept;
    void detach(std::coroutine_handle<> root) noexcept;

    std::byte *storage_ = nullptr;
    size_t capacity_ = 0;
    size_t used_ = 0;
    size_t peak_ = 0;
    size_t live_frames_ = 0;
    size_t remaining_ = 0;
    Frame *last_ = nullptr;
    std::coroutine_handle<> root_{};
    std::coroutine_handle<> active_{};
    TaskState stopped_ = TaskState::Pending;
};

// Coroutine functions take TaskContext first and use ordinary max_align_t frame alignment.
template<class T> class Coroutine {
    static_assert(!std::is_void_v<T> && !std::is_reference_v<T>);
    static_assert(std::is_nothrow_move_constructible_v<T>);
  public:
    using value_type = T;
    struct promise_type {
        TaskContext &context;
        std::coroutine_handle<> parent{};
        std::optional<T> value;

        template<class... Args>
        explicit promise_type(TaskContext &owner, Args &&...) noexcept : context(owner) {}
        template<class... Args>
        static void *operator new(size_t bytes, TaskContext &owner, Args &&...) noexcept {
            return owner.allocate(bytes);
        }
        static void operator delete(void *frame, size_t) noexcept { TaskContext::release(frame); }
        static Coroutine get_return_object_on_allocation_failure() noexcept {
            return Coroutine(TaskState::AllocationFailed);
        }
        Coroutine get_return_object() noexcept {
            return Coroutine(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        TaskContext::Checkpoint await_transform(TaskContext::Checkpoint checkpoint) noexcept {
            // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult): Analyzer skips promise construction.
            if (&checkpoint.context_ != &context)
                context.stop(TaskState::Invalid);
            return context.checkpoint();
        }
        template<class U>
        typename Coroutine<U>::Awaiter await_transform(Coroutine<U> &&child) noexcept {
            return std::move(child).operator co_await();
        }
        struct Final {
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> completed) const noexcept {
                auto &promise = completed.promise();
                if (promise.context.stop_state() == TaskState::Pending && promise.parent) {
                    promise.context.active_ = promise.parent;
                    return promise.parent;
                }
                promise.context.active_ = {};
                return std::noop_coroutine();
            }
            void await_resume() const noexcept {}
        };
        Final final_suspend() const noexcept { return {}; }
        void return_value(T returned) noexcept { value.emplace(std::move(returned)); }
        void unhandled_exception() noexcept { context.stop(TaskState::Invalid); }
    };
    using Handle = std::coroutine_handle<promise_type>;

    Coroutine() = default;
    Coroutine(const Coroutine &) = delete;
    Coroutine &operator=(const Coroutine &) = delete;
    Coroutine(Coroutine &&other) noexcept
        : handle_(std::exchange(other.handle_, {})), empty_state_(other.empty_state_) {
        other.empty_state_ = TaskState::Invalid;
    }
    Coroutine &operator=(Coroutine &&other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, {});
            empty_state_ = other.empty_state_;
            other.empty_state_ = TaskState::Invalid;
        }
        return *this;
    }
    ~Coroutine() { reset(); }

    struct Awaiter;
    Awaiter operator co_await() && noexcept;

  private:
    template<class U> friend class Task;
    void reset() noexcept {
        if (handle_) {
            handle_.promise().context.detach(handle_);
            std::exchange(handle_, {}).destroy();
        }
        empty_state_ = TaskState::Invalid;
    }

    explicit Coroutine(TaskState state) noexcept : empty_state_(state) {}
    explicit Coroutine(Handle handle) noexcept : handle_(handle) {}
    Handle handle_{};
    TaskState empty_state_ = TaskState::Invalid;
};

template<class T> struct Coroutine<T>::Awaiter {
    Coroutine child;
    bool await_ready() const noexcept { return false; }
    template<class Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> parent) noexcept {
        TaskContext &context = parent.promise().context;
        if (!child.handle_) {
            // Failed children stop the parent before it can accept a default value.
            context.stop(child.empty_state_);
            return std::noop_coroutine();
        }
        if (&child.handle_.promise().context != &context) {
            context.stop(TaskState::Invalid);
            return std::noop_coroutine();
        }
        if (context.stop_state() != TaskState::Pending)
            return std::noop_coroutine();
        if (child.handle_.done()) {
            context.active_ = parent;
            return parent;
        }
        child.handle_.promise().parent = parent;
        context.active_ = child.handle_;
        return child.handle_;
    }
    T await_resume() noexcept { return std::move(*child.handle_.promise().value); }
};

template<class T> typename Coroutine<T>::Awaiter Coroutine<T>::operator co_await() && noexcept {
    return Awaiter{std::move(*this)};
}

// Only factory-bound drivers expose execution and terminal status.
template<class T> class Task {
  public:
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;
    Task(Task &&other) noexcept
        : context_(std::exchange(other.context_, nullptr)), coroutine_(std::move(other.coroutine_)) {}
    Task &operator=(Task &&other) noexcept {
        if (this != &other) {
            reset();
            context_ = std::exchange(other.context_, nullptr);
            coroutine_ = std::move(other.coroutine_);
        }
        return *this;
    }

    TaskState state() const noexcept {
        if (!context_)
            return TaskState::Invalid;
        if (context_->stop_state() != TaskState::Pending)
            return context_->stop_state();
        return coroutine_.handle_.done() ? TaskState::Complete : TaskState::Pending;
    }
    const T *result() const noexcept {
        return state() == TaskState::Complete ? &*coroutine_.handle_.promise().value : nullptr;
    }
    TaskState advance(size_t units) noexcept {
        if (state() == TaskState::Pending && units != 0 && context_->start(coroutine_.handle_)) {
            context_->remaining_ = units;
            context_->active_.resume();
        }
        TaskState status = state();
        if (status != TaskState::Pending && status != TaskState::Complete)
            coroutine_.reset();
        return status;
    }
    void cancel() noexcept {
        if (context_)
            context_->cancel();
        advance(0);
    }
    void reset() noexcept {
        coroutine_.reset();
        context_ = nullptr;
    }

  private:
    template<class Function, class... Args>
    friend auto make_task(TaskContext &, Function &&, Args &&...) noexcept;

    Task(TaskContext &context, Coroutine<T> &&coroutine) noexcept
        : context_(&context), coroutine_(std::move(coroutine)) {
        if (context.stop_state() != TaskState::Pending)
            coroutine_.reset();
        else if (!coroutine_.handle_)
            context.stop(coroutine_.empty_state_);
        else if (&coroutine_.handle_.promise().context != &context) {
            context.stop(TaskState::Invalid);
            coroutine_.reset();
        }
    }
    TaskContext *context_;
    Coroutine<T> coroutine_;
};

template<class Function, class... Args>
auto make_task(TaskContext &context, Function &&function, Args &&...args) noexcept {
    using Produced = decltype(std::forward<Function>(function)(context, std::forward<Args>(args)...));
    using T = typename Produced::value_type;
    if (context.stop_state() != TaskState::Pending)
        return Task<T>(context, Coroutine<T>{});
    return Task<T>(context, std::forward<Function>(function)(context, std::forward<Args>(args)...));
}

}
#endif
