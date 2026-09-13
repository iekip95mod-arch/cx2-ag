#include "nps/steps/solve_task.h"

#include "nps/core/parser.h"
#include "nps/core/context.h"

namespace nps {
namespace {

Budget without_callback(Budget budget) {
    budget.poll = nullptr;
    budget.poll_context = nullptr;
    return budget;
}

}

SolveTask::SolveTask(SolveOperation operation, SolveRequest request, std::string variable,
                     size_t frame_bytes, Limits limits, Budget budget)
    : operation_(operation), variable_(std::move(variable)), arena_(limits),
      meter_(without_callback(budget)), frames_(frame_bytes) {
    working_.request = std::move(request);
    published_.request = working_.request;
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = operation == SolveOperation::Linear
                                   ? "algebra.linear-equation.one-unknown"
                                   : "algebra.formula-rearrangement.single-occurrence";
    inputs.requested_method = "inverse operations";
    inputs.original_expression = working_.request.original_expression;
    inputs.numeric_mode = working_.request.numeric_mode;
    inputs.angle_convention = "radians";
    inputs.branch_convention = "real domain";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    working_.context = make_context(inputs);
    published_.context = working_.context;
    if (budget.poll || budget.poll_context) {
        state_ = TaskState::Invalid;
        published_.context.derivation_status = DerivationStatus::InvalidInput;
        return;
    }
    context_.emplace(frames_);
    task_.emplace(make_task(*context_, run, *this, budget));
    state_ = task_->state();
    if (state_ == TaskState::AllocationFailed)
        published_.context.derivation_status = DerivationStatus::ResourceLimitReached;
}

SolveTask::~SolveTask() { close(); }

Coroutine<SolveTaskResult> SolveTask::run(TaskContext &context, SolveTask &owner, Budget budget) {
    co_await context.checkpoint();
    // Parsing is atomic under the arena input-byte, node and depth limits.
    NodeId equation = kNoNode;
    if (is_identifier(owner.variable_, owner.arena_.limits().max_input_bytes))
        equation = parse(owner.arena_, owner.working_.request.original_expression).root;
    owner.working_.context.normalized_problem_model = equation;
    co_await context.checkpoint();
    const NodeId variable = equation == kNoNode ? kNoNode
                                               : owner.arena_.symbol(normalize_identifier(owner.variable_));
    if (owner.operation_ == SolveOperation::Linear)
        co_return co_await solve_linear_steps(context, owner.arena_, owner.working_, equation,
                                              variable, owner.meter_, budget);
    co_return co_await rearrange_steps(context, owner.arena_, owner.working_, equation,
                                       variable, owner.meter_, budget);
}

TaskState SolveTask::state() const noexcept { return state_; }

void SolveTask::publish() {
    if (!arena_.failed())
        published_.publish_verified(working_);
    if (state_ != TaskState::Pending) {
        published_.context = working_.context;
        for (size_t i = 0; i < published_.size(); ++i) {
            for (const std::string &condition : published_.at(static_cast<StepId>(i)).domain_restrictions) {
                auto &assumptions = published_.context.active_assumptions;
                if (std::find(assumptions.begin(), assumptions.end(), condition) == assumptions.end())
                    assumptions.push_back(condition);
            }
        }
    }
}

TaskState SolveTask::advance(size_t units) {
    if (state_ != TaskState::Pending || units == 0)
        return state_;
    state_ = task_->advance(units);
    peak_ = context_->peak_bytes();
    publish();
    if (state_ == TaskState::Complete) {
        result_ = *task_->result();
        std::visit([this](auto &solved) {
            if constexpr (std::is_same_v<std::decay_t<decltype(solved)>, RearrangeResult>)
                solved.restrictions = published_.context.active_assumptions;
            if (solved.status != DerivationStatus::SolvedAndVerified &&
                solved.status != DerivationStatus::ConditionallySolved) {
                if constexpr (std::is_same_v<std::decay_t<decltype(solved)>, SolveResult>)
                    solved.solution = kNoNode;
                else {
                    solved.formula = kNoNode;
                    solved.expression = kNoNode;
                }
            }
        }, *result_);
        task_->reset();
    } else if (state_ != TaskState::Pending) {
        published_.context.derivation_status = DerivationStatus::ResourceLimitReached;
    }
    return state_;
}

void SolveTask::cancel() {
    if (state_ != TaskState::Pending)
        return;
    task_->cancel();
    state_ = TaskState::Cancelled;
    publish();
    published_.context.derivation_status = DerivationStatus::Cancelled;
}

void SolveTask::close() {
    task_.reset();
    context_.reset();
    std::vector<std::byte>().swap(frames_);
    result_.reset();
    working_ = Derivation{};
    published_ = Derivation{};
    arena_ = Arena{};
    meter_ = Meter(Budget{});
    std::string().swap(variable_);
    state_ = TaskState::Invalid;
}

const SolveTaskResult *SolveTask::result() const noexcept {
    return result_ ? &*result_ : nullptr;
}

SolveResources SolveTask::resources() const noexcept {
    SolveResources resources;
    resources.frame_capacity = context_ ? context_->capacity() : frames_.size();
    resources.frame_live_bytes = context_ ? context_->used_bytes() : 0;
    resources.frame_peak_bytes = context_ ? context_->peak_bytes() : peak_;
    resources.live_frames = context_ ? context_->live_frames() : 0;
    resources.ast_nodes = arena_.node_count();
    resources.ast_child_slots = arena_.child_slot_count();
    resources.cost = meter_.cost();
    return resources;
}

}
