#ifndef NPS_SOLVE_TASK_H
#define NPS_SOLVE_TASK_H

#include <optional>
#include <variant>

#include "nps/steps/linear.h"
#include "nps/steps/rearrange.h"

namespace nps {

enum class SolveOperation { Linear, Rearrange };
using SolveTaskResult = std::variant<SolveResult, RearrangeResult>;

struct SolveResources {
    size_t frame_capacity = 0;
    size_t frame_live_bytes = 0;
    size_t frame_peak_bytes = 0;
    size_t live_frames = 0;
    size_t ast_nodes = 0;
    size_t ast_child_slots = 0;
    Cost cost;
};

class SolveTask {
  public:
    SolveTask(SolveOperation operation, SolveRequest request, std::string variable, size_t frame_bytes,
              Limits limits = {}, Budget budget = {});
    SolveTask(const SolveTask &) = delete;
    SolveTask &operator=(const SolveTask &) = delete;
    SolveTask(SolveTask &&) = delete;
    SolveTask &operator=(SolveTask &&) = delete;
    ~SolveTask();

    TaskState state() const noexcept;
    // Units count cooperative checkpoints, not elapsed time or allocator work.
    TaskState advance(size_t units);
    void cancel();
    void close();
    const SolveTaskResult *result() const noexcept;
    const Arena &arena() const noexcept { return arena_; }
    // Borrowed record references must not span advance, cancel or close.
    const Derivation &published() const noexcept { return published_; }
    SolveResources resources() const noexcept;

  private:
    static Coroutine<SolveTaskResult> run(TaskContext &context, SolveTask &owner, Budget budget);
    void publish();
    SolveOperation operation_;
    std::string variable_;
    Arena arena_;
    Derivation working_;
    Derivation published_;
    Meter meter_;
    std::vector<std::byte> frames_;
    std::optional<TaskContext> context_;
    std::optional<Task<SolveTaskResult>> task_;
    std::optional<SolveTaskResult> result_;
    TaskState state_ = TaskState::Pending;
    size_t peak_ = 0;
};

}
#endif
