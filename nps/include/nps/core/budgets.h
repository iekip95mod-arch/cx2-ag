// Named budgets.h rather than limits.h on purpose. A header called limits.h anywhere on the include
// path shadows the C standard <limits.h>, and the failure is a wall of errors about CHAR_BIT from
// inside the standard library, nowhere near the file that caused it.
#ifndef NPS_BUDGETS_H
#define NPS_BUDGETS_H

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nps {

// The build's identity, recorded in every SolutionContext so a saved derivation says which rules
// produced it. Bumped by hand when the rules or the wire format change.
inline const char *application_version() { return "nps 0.2"; }

// PERF-008 wants enforced limits for AST depth, expression size, rewrite count, branch count,
// backend calls and repeated canonical states. The three the AST itself can enforce live here, and
// the other three belong to the meter below.
//
// PERF-010 freezes real numbers against target hardware in Milestone 0. These are provisional and
// deliberately low: a bound that never fires teaches nothing, and the measurement that replaces
// them wants to see the failure path exercised.
struct Limits {
    size_t max_depth = 64;
    size_t max_nodes = 4096;
    size_t max_input_bytes = 4096;
};

// PERF-003 wants symbolic work to be cancellable. The toolchain is built without threads, so there
// is no worker to interrupt and nothing can be stopped from outside: the solver has to ask, and
// this is what it asks. poll returns true when the user has asked to stop.
struct Budget {
    size_t max_rewrites = 4096;
    size_t max_steps = 512;
    size_t max_branches = 64;
    size_t max_backend_calls = 32;
    bool (*poll)(void *) = 0;
    void *poll_context = 0;
};

// The limits a solve ran under, for the resource_policy field of its SolutionContext. A different
// budget can produce a different derivation, so reproducing one means knowing which budget it had.
inline std::string budget_policy(const Budget &budget) {
    std::string out = "rewrites<=";
    const size_t values[4] = {budget.max_rewrites, budget.max_steps, budget.max_branches,
                              budget.max_backend_calls};
    const char *labels[4] = {"", " steps<=", " branches<=", " backend<="};
    for (int i = 0; i < 4; ++i) {
        out += labels[i];
        char digits[32];
        const auto conversion = std::to_chars(digits, digits + sizeof digits, values[i]);
        if (conversion.ec != std::errc())
            return std::string();
        out.append(digits, conversion.ptr);
    }
    return out;
}

// What a solve actually spent. PERF-010 has to freeze budgets for derivation steps, branches and
// backend calls, and a budget frozen against a number nobody measured is a guess with a version on
// it. Reported whether the solve finished or halted, since a halt is when the numbers matter most.
struct Cost {
    size_t rewrites = 0;
    size_t steps = 0;
    size_t branches = 0;
    size_t backend_calls = 0;
    // Distinct canonical states the solve passed through. Reported rather than left inside the meter
    // because PERF-010 has to freeze a cap on them, and a cap frozen against a number nobody
    // measured is a guess. It is also the only thing outside the meter that says the engine asked.
    size_t states = 0;
    // Steps appended from a remembered run instead of derived again. Visible here so a test can
    // read the behaviour rather than the record, which looks the same either way.
    size_t replayed = 0;
};

enum class Halt : uint8_t {
    Running,
    Cancelled,
    RewriteLimit,
    StepLimit,
    BranchLimit,
    BackendLimit,
    RepeatedState,
};

inline const char *halt_name(Halt h) {
    switch (h) {
        case Halt::Running: return "running";
        case Halt::Cancelled: return "cancelled";
        case Halt::RewriteLimit: return "rewrite limit";
        case Halt::StepLimit: return "step limit";
        case Halt::BranchLimit: return "branch limit";
        case Halt::BackendLimit: return "backend call limit";
        case Halt::RepeatedState: return "repeated state";
    }
    return "unknown";
}

// One meter per solve. It polls once on entry, then counts every rule through one shared gate.
class Meter {
  public:
    explicit Meter(const Budget &budget) : budget_(budget) { poll(); }

    bool rewrite() {
        if (halt_ != Halt::Running)
            return false;
        if (++rewrites_ > budget_.max_rewrites) {
            halt_ = Halt::RewriteLimit;
            return false;
        }
        // Derivation and replay share a stride to bound keypad scans.
        return poll_work();
    }

    bool step() {
        if (halt_ != Halt::Running)
            return false;
        if (++steps_ > budget_.max_steps) {
            halt_ = Halt::StepLimit;
            return false;
        }
        return poll_work();
    }

    // A step taken from a remembered run is still a step against PERF-008's limit, and counted
    // separately so reuse can be seen.
    bool replay() {
        if (!step())
            return false;
        ++replayed_;
        return true;
    }

    bool branch() {
        if (halt_ != Halt::Running)
            return false;
        if (++branches_ > budget_.max_branches) {
            halt_ = Halt::BranchLimit;
            return false;
        }
        return poll_work();
    }

    bool backend_call() {
        if (halt_ != Halt::Running)
            return false;
        if (++backend_calls_ > budget_.max_backend_calls) {
            halt_ = Halt::BackendLimit;
            return false;
        }
        // Asked every time rather than on the shared stride, which one backend call outlasts.
        return poll();
    }

    bool reached(uint32_t state) {
        if (halt_ != Halt::Running)
            return false;
        const auto at = std::lower_bound(seen_.begin(), seen_.end(), state);
        if (at != seen_.end() && *at == state) {
            halt_ = Halt::RepeatedState;
            return false;
        }
        seen_.insert(at, state);
        return true;
    }

    const Budget &budget() const { return budget_; }
    bool checkpoint() { return poll(); }
    bool stopped() const { return halt_ != Halt::Running; }
    Halt halt() const { return halt_; }
    Cost cost() const {
        Cost c;
        c.rewrites = rewrites_;
        c.steps = steps_;
        c.branches = branches_;
        c.backend_calls = backend_calls_;
        c.states = seen_.size();
        c.replayed = replayed_;
        return c;
    }
    size_t rewrites() const { return rewrites_; }
    size_t steps() const { return steps_; }
    size_t branches() const { return branches_; }
    size_t backend_calls() const { return backend_calls_; }
    size_t states() const { return seen_.size(); }

  private:
    static const size_t kPollStride = 64;

    bool poll_work() {
        if (++work_since_poll_ < kPollStride)
            return true;
        work_since_poll_ = 0;
        return poll();
    }

    bool poll() {
        if (halt_ != Halt::Running)
            return false;
        if (budget_.poll && budget_.poll(budget_.poll_context)) {
            halt_ = Halt::Cancelled;
            return false;
        }
        return true;
    }

    Budget budget_;
    size_t rewrites_ = 0;
    size_t steps_ = 0;
    size_t branches_ = 0;
    size_t backend_calls_ = 0;
    size_t replayed_ = 0;
    size_t work_since_poll_ = 0;
    // Sorted rather than a set, because a state is asked at most once per rewrite and the counts are
    // small enough that a flat vector costs less on the handheld than a node per entry.
    std::vector<uint32_t> seen_;
    Halt halt_ = Halt::Running;
};

inline size_t remaining(size_t limit, size_t used) { return used < limit ? limit - used : 0; }

// A nested engine on its own meter would spend the declared cap again, so hand it what is left.
inline Budget remaining_budget(const Budget &budget, const Meter &meter) {
    Budget nested = budget;
    nested.max_rewrites = remaining(budget.max_rewrites, meter.rewrites());
    nested.max_steps = remaining(budget.max_steps, meter.steps());
    nested.max_branches = remaining(budget.max_branches, meter.branches());
    nested.max_backend_calls = remaining(budget.max_backend_calls, meter.backend_calls());
    return nested;
}

// The other half: what the nested engine spent comes back, so the next remainder has moved.
inline bool charge(Meter &meter, const Cost &cost) {
    for (size_t count = 0; count < cost.rewrites; ++count) {
        if (!meter.rewrite())
            return false;
    }
    for (size_t count = 0; count < cost.steps; ++count) {
        if (!meter.step())
            return false;
    }
    for (size_t count = 0; count < cost.branches; ++count) {
        if (!meter.branch())
            return false;
    }
    for (size_t count = 0; count < cost.backend_calls; ++count) {
        if (!meter.backend_call())
            return false;
    }
    return true;
}

}  // namespace nps

#endif
