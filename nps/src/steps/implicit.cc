#include "nps/steps/implicit.h"

#include <algorithm>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/core/task.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/rearrange.h"

namespace nps {
namespace {

// Small integers on both axes and off them, so a relation with a pole or a root at the origin still
// leaves points where the identity can be read exactly.
const int64_t kSamples[][2] = {{0, 1}, {1, 0}, {1, 1}, {2, 1}, {1, 2}, {-1, 2}, {2, -1},
                               {3, 2}, {-2, -3}, {0, 2}, {2, 0}, {3, -1}};

struct Implicit {
    Arena &arena;
    Derivation &derivation;
    NodeId equation;
    NodeId x;
    NodeId y;
    Budget budget;
    Meter meter;
    size_t mark;
    ImplicitResult result;
    NodeId symbol = kNoNode;

    Implicit(Arena &a, Derivation &d, NodeId e, NodeId independent, NodeId dependent, const Budget &b)
        : arena(a), derivation(d), equation(e), x(independent), y(dependent), budget(b), meter(b),
          mark(d.mark()) {}

    bool work() { return !arena.failed() && meter.rewrite(); }

    NodeId folded(NodeId expression) {
        if (arena.failed() || expression == kNoNode) return kNoNode;
        return canonicalize(arena, expression);
    }

    void stop(ImplicitOutcome outcome, DerivationStatus status, const std::string &detail) {
        result.outcome = outcome;
        result.status = status;
        result.detail = detail;
    }

    StepId step(const char *rule, const std::string &title, NodeId before, NodeId after,
                const std::string &action, const std::string &reason, const std::string &detailed,
                ClaimType claim, bool pending = false) {
        if (before == kNoNode || (!pending && after == kNoNode) || arena.failed() || !meter.step()) return kNoStep;
        Step entry;
        entry.phase = "implicit";
        entry.rule_id = rule;
        entry.rule_name = title;
        entry.goal = title;
        entry.claim = claim;
        entry.explanation_short = reason;
        entry.explanation_detailed = detailed;
        entry.proof_obligations.push_back({"obl.implicit.rule-preserves-relation",
                                           "the rule keeps the relation between the variables and their derivative"});
        VerificationRecord verification;
        verification.method = "rule-local invariant";
        verification.outcome = VerificationOutcome::Passed;
        verification.strength = EvidenceStrength::StructurallyValid;
        verification.detail = reason;
        entry.verifications.push_back(std::move(verification));
        TransformationPayload change;
        change.before = before;
        change.after = pending ? kNoNode : after;
        change.concrete_action = action;
        return derivation.add_transformation(kNoStep, std::move(entry), std::move(change));
    }

    // One partial derivative, or zero when the side does not mention the variable, which keeps the
    // record to the work a learner would actually do.
    bool partial(NodeId side, NodeId variable, NodeId *out) {
        if (!depends_on(arena, side, variable)) {
            *out = arena.integer("0");
            return !arena.failed();
        }
        const DiffResult differentiated = differentiate(arena, derivation, side, variable, meter);
        if (differentiated.outcome == DiffOutcome::Differentiated && differentiated.derivative != kNoNode) {
            *out = folded(differentiated.derivative);
            return *out != kNoNode;
        }
        switch (differentiated.outcome) {
            case DiffOutcome::Cancelled:
                stop(ImplicitOutcome::Cancelled, DerivationStatus::Cancelled, differentiated.detail);
                break;
            case DiffOutcome::ResourceExceeded:
                stop(ImplicitOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached, differentiated.detail);
                break;
            default:
                stop(ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported,
                     differentiated.detail.empty() ? "the native differentiation engine has no rule for this expression"
                                                   : differentiated.detail);
                break;
        }
        return false;
    }

    // d/dx of one side, with the dependent variable read as a function of x.
    bool side(NodeId expression, NodeId *out, NodeId *along_x, NodeId *along_y) {
        const std::string xn = arena.text(x), yn = arena.text(y);
        const NodeId asked = arena.call("d", {expression, x});
        const StepId chain = step("implicit.chain-rule", "Differentiate one side with " + yn + " depending on " + xn,
            asked, kNoNode,
            "Differentiate with respect to " + xn + " as usual, then add the derivative with respect to " + yn +
                " multiplied by " + arena.text(symbol),
            "Every occurrence of " + yn + " is a function of " + xn + ", so the chain rule multiplies its derivative by " +
                arena.text(symbol),
            "Read " + yn + " as an unknown function of " + xn + ". Terms in " + xn + " alone differentiate as usual. A term in " + yn +
                " differentiates like any other function and then picks up one more factor, the derivative of " + yn +
                " itself, because of the chain rule.",
            ClaimType::Definition, true);
        if (chain == kNoStep) return false;
        const size_t child_mark = derivation.mark();
        const bool ok = partial(expression, x, along_x) && partial(expression, y, along_y);
        derivation.adopt_roots_since(child_mark, chain);
        if (!ok) return false;
        *out = folded(arena.binary(Kind::Add, *along_x, arena.binary(Kind::Mul, *along_y, symbol)));
        return *out != kNoNode && derivation.complete_transformation(chain, *out);
    }

    RearrangeResult isolate(NodeId collected) {
        std::vector<std::byte> storage(262144);
        TaskContext context(storage);
        auto task = make_task(context, rearrange_steps, arena, derivation, collected, symbol, meter, budget,
                              static_cast<Backend *>(nullptr));
        while (task.state() == TaskState::Pending) task.advance(1024);
        if (task.result()) return *task.result();
        RearrangeResult refused;
        refused.outcome = RearrangeOutcome::ResourceExceeded;
        refused.status = DerivationStatus::ResourceLimitReached;
        refused.detail = "coroutine frame storage exhausted";
        return refused;
    }

    void run() {
        const ChildView sides = arena.children(equation);
        const NodeId left = sides[0], right = sides[1];
        NodeId left_rate = kNoNode, right_rate = kNoNode;
        NodeId lx = kNoNode, ly = kNoNode, rx = kNoNode, ry = kNoNode;
        const StepId both = step("implicit.differentiate-both-sides", "Differentiate both sides", equation, kNoNode,
            "Take the derivative of each side with respect to " + arena.text(x),
            "Both sides are equal for every " + arena.text(x) + " on the curve, so their derivatives are equal too",
            "The equation holds along the curve, where " + arena.text(y) + " is a function of " + arena.text(x) +
                ". Two functions that are equal everywhere on an interval have equal derivatives there, so differentiating both sides keeps a true equation.",
            ClaimType::Implication, true);
        if (both == kNoStep) return;
        derivation.restrictions_at(both).push_back(arena.text(y) + " is a differentiable function of " + arena.text(x) +
                                                   " near the point of interest");
        const size_t sides_mark = derivation.mark();
        const bool ok = side(left, &left_rate, &lx, &ly) && side(right, &right_rate, &rx, &ry);
        derivation.adopt_roots_since(sides_mark, both);
        if (!ok) return;
        const NodeId differentiated = arena.binary(Kind::Equals, left_rate, right_rate);
        if (!derivation.complete_transformation(both, differentiated) || !work()) return;

        // Collect: (dL/dy - dR/dy) * y' = dR/dx - dL/dx, read off the two partials of each side.
        const NodeId coefficient = folded(arena.binary(Kind::Add, ly, arena.unary(Kind::Neg, ry)));
        const NodeId rest = folded(arena.binary(Kind::Add, rx, arena.unary(Kind::Neg, lx)));
        if (coefficient == kNoNode || rest == kNoNode || !work()) return;
        Rational constant;
        if (evaluate_rational(arena, coefficient, {}, &constant) && constant.num == 0) {
            stop(ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported,
                 "the terms in " + arena.text(symbol) + " cancel, so the equation does not determine the derivative");
            return;
        }
        const NodeId collected = arena.binary(Kind::Equals, arena.binary(Kind::Mul, coefficient, symbol), rest);
        if (step("implicit.collect", "Collect the derivative terms", differentiated, collected,
                 "Move every term with " + arena.text(symbol) + " to the left and every other term to the right, then factor out " +
                     arena.text(symbol),
                 "Adding the same terms to both sides keeps the equation, and the derivative appears only to the first power",
                 "After differentiating, the unknown derivative appears only to the first power. Gather its terms on one side and factor it out, the same way a linear equation is collected before dividing.",
                 ClaimType::SolutionSetPreserved) == kNoStep) return;
        const StepId isolation = step("implicit.isolate", "Isolate the derivative", collected, kNoNode,
            "Divide both sides by the coefficient of " + arena.text(symbol),
            "The rearrangement rules undo the product around the derivative, recording the divisor as a restriction",
            "Once the derivative terms are collected, isolating it is a rearrangement: divide by its coefficient, which has to be nonzero where the answer is used.",
            ClaimType::SolutionSetPreserved, true);
        if (isolation == kNoStep) return;
        const size_t isolation_mark = derivation.mark();
        const RearrangeResult isolated = isolate(collected);
        derivation.adopt_roots_since(isolation_mark, isolation);
        if (isolated.outcome != RearrangeOutcome::Isolated || isolated.expression == kNoNode) {
            if (isolated.outcome == RearrangeOutcome::Cancelled)
                stop(ImplicitOutcome::Cancelled, DerivationStatus::Cancelled, isolated.detail);
            else if (isolated.outcome == RearrangeOutcome::ResourceExceeded)
                stop(ImplicitOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached, isolated.detail);
            else if (isolated.outcome == RearrangeOutcome::VerificationFailed)
                stop(ImplicitOutcome::VerificationFailed, DerivationStatus::VerificationFailed, isolated.detail);
            else
                stop(ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported,
                     isolated.detail.empty() ? "the derivative could not be isolated" : isolated.detail);
            return;
        }
        if (!derivation.complete_transformation(isolation, isolated.formula)) return;
        for (const std::string &condition : isolated.restrictions) derivation.restrictions_at(isolation).push_back(condition);
        if (!verify(isolated.expression)) return;
        result.outcome = ImplicitOutcome::Differentiated;
        result.derivative = isolated.expression;
        result.restrictions = isolated.restrictions;
    }

    // The final check. The relation is differentiated again as one expression, left minus right,
    // and the chain rule identity F_x + F_y * y' = 0 is evaluated exactly at sample points where
    // the answer is defined. That is sample agreement, which corroborates rather than proves.
    bool verify(NodeId answer) {
        const ChildView sides = arena.children(equation);
        const NodeId whole = arena.binary(Kind::Add, sides[0], arena.unary(Kind::Neg, sides[1]));
        Derivation scratch;
        const DiffResult along_x = differentiate(arena, scratch, whole, x, meter);
        const DiffResult along_y = along_x.outcome == DiffOutcome::Differentiated
            ? differentiate(arena, scratch, whole, y, meter) : DiffResult{};
        size_t read = 0;
        bool matched = true;
        if (along_x.outcome == DiffOutcome::Differentiated && along_y.outcome == DiffOutcome::Differentiated) {
            const NodeId identity = arena.binary(Kind::Add, along_x.derivative,
                                                 arena.binary(Kind::Mul, along_y.derivative, answer));
            for (const auto &sample : kSamples) {
                if (!work()) return false;
                const std::vector<SymbolValue> at = {{arena.text(x), {sample[0], 1}}, {arena.text(y), {sample[1], 1}}};
                Rational slope, value;
                if (!evaluate_rational(arena, answer, at, &slope) || !evaluate_rational(arena, identity, at, &value))
                    continue;
                ++read;
                if (value.num != 0) matched = false;
            }
        }
        matched = matched && read > 0;
        if (!work() || !meter.step()) return false;
        Step check;
        check.phase = "check";
        check.goal = "Check the derivative against the original relation";
        check.rule_id = "implicit.check";
        check.rule_name = "Chain rule identity at sample points";
        check.claim = ClaimType::EquivalentExpression;
        check.explanation_short = "Differentiate left minus right again and check that its x part plus its y part times the answer is zero";
        check.proof_obligations.push_back({"obl.implicit.chain-identity",
                                           "the derivative satisfies the chain rule identity of the original relation"});
        VerificationRecord evidence;
        evidence.method = "exact evaluation of the chain rule identity at sample points";
        evidence.outcome = matched ? VerificationOutcome::Passed
                         : read > 0 ? VerificationOutcome::Failed : VerificationOutcome::Inconclusive;
        evidence.strength = strength_for(evidence.outcome, EvidenceStrength::NumericallyCorroborated);
        evidence.detail = matched ? "the identity is exactly zero at " + std::to_string(read) + " sample points"
                        : read > 0 ? "the identity is nonzero at a sample point"
                                   : "no sample point gave exact values for the answer and the identity";
        check.verifications.push_back(evidence);
        CheckPayload payload;
        payload.target_claim = "the isolated derivative satisfies the differentiated relation";
        payload.check_method = "exact evaluation of the chain rule identity at sample points";
        payload.expected_relation = "zero at every sample point where the answer is defined";
        payload.observed_result = evidence.detail;
        derivation.add_check(kNoStep, std::move(check), std::move(payload));
        // No exact reading is an unchecked answer rather than a failed one, and the record says so.
        if (matched || read == 0) return work();
        if (!work()) return false;
        stop(ImplicitOutcome::VerificationFailed, DerivationStatus::VerificationFailed,
             "the derivative failed its check against the original relation, so it is withheld");
        return false;
    }

    ImplicitResult finish() {
        if (arena.failed() || meter.stopped()) {
            result.derivative = kNoNode;
            result.restrictions.clear();
            result.outcome = meter.halt() == Halt::Cancelled ? ImplicitOutcome::Cancelled : ImplicitOutcome::ResourceExceeded;
            result.status = meter.halt() == Halt::Cancelled ? DerivationStatus::Cancelled : DerivationStatus::ResourceLimitReached;
            result.detail = arena.failed() ? status_name(arena.status()) : halt_name(meter.halt());
            keep_verified_prefix(derivation, mark, arena);
        } else if (result.derivative != kNoNode) {
            result.status = derivation.outcome_from(mark);
        } else if (result.outcome == ImplicitOutcome::Differentiated) {
            result.outcome = ImplicitOutcome::UnsupportedForm;
        }
        if (result.detail.empty() && result.derivative == kNoNode)
            result.detail = "the native calculation could not establish a verified derivative";
        result.symbol = symbol;
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id = "calculus.derivative.implicit";
        context.requested_method = "implicit";
        context.original_expression = derivation.request.original_expression;
        context.normalized_problem_model = arena.failed() || result.status == DerivationStatus::InvalidInput
            ? kNoNode : arena.call("implicit", {equation, x, y});
        context.active_assumptions = result.restrictions;
        context.angle_convention = "radians";
        context.branch_convention = "real domain, principal values";
        context.detail_projection = "standard";
        context.resource_policy = budget_policy(meter.budget());
        context.numeric_mode = derivation.request.numeric_mode;
        context.derivation_status = result.status;
        derivation.context = make_context(context);
        derivation.context.problem_family_envelope_version = "1";
        result.cost = meter.cost();
        return result;
    }
};

}

const char *implicit_outcome_name(ImplicitOutcome outcome) {
    switch (outcome) {
        case ImplicitOutcome::Differentiated: return "differentiated";
        case ImplicitOutcome::NotAnEquation: return "not an equation";
        case ImplicitOutcome::UnsupportedForm: return "unsupported form";
        case ImplicitOutcome::InvalidInput: return "invalid input";
        case ImplicitOutcome::VerificationFailed: return "verification failed";
        case ImplicitOutcome::Cancelled: return "cancelled";
        case ImplicitOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

ImplicitResult implicit_differentiate(Arena &arena, Derivation &derivation, NodeId equation,
                                      NodeId independent, NodeId dependent, const Budget &budget) {
    Implicit run(arena, derivation, equation, independent, dependent, budget);
    const auto present = [&arena](NodeId node) { return node < arena.node_count(); };
    if (!present(equation) || !present(independent) || !present(dependent) ||
        arena.at(independent).kind != Kind::Symbol || arena.at(dependent).kind != Kind::Symbol) {
        run.stop(ImplicitOutcome::InvalidInput, DerivationStatus::InvalidInput,
                 "implicit differentiation needs an equation and two variable names");
    } else if (arena.at(equation).kind != Kind::Equals) {
        run.stop(ImplicitOutcome::NotAnEquation, DerivationStatus::InvalidInput,
                 "implicit differentiation needs an equation relating the two variables");
    } else if (arena.text(independent) == arena.text(dependent)) {
        run.stop(ImplicitOutcome::InvalidInput, DerivationStatus::InvalidInput,
                 "the dependent and independent variables must be different");
    } else if (!depends_on(arena, equation, dependent)) {
        run.stop(ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported,
                 "the equation does not mention " + arena.text(dependent) + ", so it does not define it");
    } else if (derivation.request.numeric_mode != NumericMode::Exact) {
        run.stop(ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported,
                 "implicit differentiation walkthroughs currently require Exact mode");
    } else {
        const std::string name = "d" + arena.text(dependent) + "d" + arena.text(independent);
        run.symbol = arena.symbol(name);
        if (run.symbol == kNoNode || arena.failed()) {
            run.stop(ImplicitOutcome::ResourceExceeded, DerivationStatus::ResourceLimitReached,
                     "the command exceeded the expression limits");
        } else if (depends_on(arena, equation, run.symbol)) {
            run.stop(ImplicitOutcome::UnsupportedForm, DerivationStatus::Unsupported,
                     "the equation already uses the name " + name + ", which stands for the derivative here");
        } else if (run.work()) {
            run.run();
        }
    }
    return run.finish();
}

}
