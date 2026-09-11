#include "nps/core/task.h"

#include <memory>
#include <new>

namespace nps {

struct alignas(std::max_align_t) TaskContext::Frame {
    TaskContext *owner;
    Frame *previous;
    size_t begin;
    bool live;
};

TaskContext::TaskContext(std::span<std::byte> storage) noexcept {
    void *start = storage.data();
    size_t available = storage.size();
    if (std::align(alignof(std::max_align_t), 1, start, available)) {
        storage_ = static_cast<std::byte *>(start);
        capacity_ = available;
    }
}

void *TaskContext::allocate(size_t bytes) noexcept {
    if (stopped_ != TaskState::Pending)
        return nullptr;
    constexpr size_t alignment = alignof(std::max_align_t);
    size_t padding = (alignment - bytes % alignment) % alignment;
    size_t available = capacity_ - used_;
    if (available < sizeof(Frame) || bytes > available - sizeof(Frame) ||
        padding > available - sizeof(Frame) - bytes) {
        stop(TaskState::AllocationFailed);
        return nullptr;
    }
    Frame *frame = ::new (storage_ + used_) Frame{this, last_, used_, true};
    used_ += sizeof(Frame) + bytes + padding;
    if (used_ > peak_)
        peak_ = used_;
    ++live_frames_;
    last_ = frame;
    return reinterpret_cast<std::byte *>(frame) + sizeof(Frame);
}

void TaskContext::release(void *address) noexcept {
    auto *frame = reinterpret_cast<Frame *>(static_cast<std::byte *>(address) - sizeof(Frame));
    TaskContext &context = *frame->owner;
    frame->live = false;
    --context.live_frames_;
    // Retired inner blocks stay charged until the later frames are released.
    while (context.last_ && !context.last_->live) {
        context.used_ = context.last_->begin;
        context.last_ = context.last_->previous;
    }
}

void TaskContext::stop(TaskState state) noexcept {
    if (stopped_ == TaskState::Pending)
        stopped_ = state;
    active_ = {};
}

bool TaskContext::start(std::coroutine_handle<> root) noexcept {
    if (!root_) {
        root_ = root;
        active_ = root;
    } else if (root_ != root) {
        stop(TaskState::Invalid);
        return false;
    }
    return true;
}

void TaskContext::detach(std::coroutine_handle<> root) noexcept {
    if (root_ == root) {
        root_ = {};
        active_ = {};
    }
}

bool TaskContext::Checkpoint::await_ready() const noexcept {
    return context_.stopped_ == TaskState::Pending && context_.remaining_ != 0;
}

void TaskContext::Checkpoint::await_suspend(std::coroutine_handle<> paused) const noexcept {
    context_.active_ = context_.stopped_ == TaskState::Pending ? paused : std::coroutine_handle<>{};
}

void TaskContext::Checkpoint::await_resume() const noexcept {
    --context_.remaining_;
}

}
