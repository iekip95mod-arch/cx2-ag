#include "nps/steps/solve_task.h"
#include "nps/core/print.h"
#include "nps/core/parser.h"
#include "nps/core/evaluate.h"
#include "unit/adapter_tests.h"
#include "step_invariants.h"

#include <type_traits>

namespace nps {
namespace {

bool same_cost(const Cost &a, const Cost &b) {
    return a.rewrites == b.rewrites && a.steps == b.steps && a.branches == b.branches &&
           a.backend_calls == b.backend_calls && a.states == b.states && a.replayed == b.replayed;
}

std::string records(const Arena &arena, const Derivation &d) {
    std::string text;
    for (size_t i = 0; i < d.size(); ++i) {
        const StepId id = static_cast<StepId>(i);
        const Step &s = d.at(id);
        text += std::to_string(s.id) + ":" + std::to_string(s.parent) + ":" + s.rule_id +
                ":" + s.goal + ":" + s.explanation_short + ":" + s.explanation_detailed;
        for (const auto &condition : s.domain_restrictions)
            text += ":" + condition;
        if (s.kind != StepKind::Plan) {
            for (const auto &v : s.verifications)
                text += ":" + v.method + ":" + v.detail + ":" + verification_outcome_name(v.outcome);
        }
        if (const auto *p = d.transformation(id))
            text += ":" + print(arena, p->before) + ":" + print(arena, p->after) +
                    ":" + p->concrete_action;
        text += "\n";
    }
    return text;
}

std::string records(const SolveTask &task) {
    return records(task.arena(), task.published());
}

bool poll(void *p) {
    ++*static_cast<size_t *>(p);
    return false;
}

}

void run_solve_task_tests(TestSink &t) {
    for (const size_t steps : {size_t{0}, size_t{4}, size_t{5}}) {
        SolveRequest request{steps == 0 ? "0.5*x=1" : "2*x=1"};
        request.numeric_mode = NumericMode::Decimal;
        Budget budget;
        budget.max_steps = steps;
        SolveTask task(SolveOperation::Linear, request, "x", 65536, Limits{}, budget);
        while (task.state() == TaskState::Pending)
            task.advance(1);
        t.check(task.result() != nullptr, "bounded persistent decimal solve reaches a typed result");
        if (task.result()) {
            const SolveResult &result = std::get<SolveResult>(*task.result());
            t.check(result.status == (steps == 5 ? DerivationStatus::SolvedAndVerified
                                                 : DerivationStatus::ResourceLimitReached) &&
                        result.status == task.published().context.derivation_status &&
                        (steps == 5 ? result.solution != kNoNode && print(task.arena(), result.solution) == "0.5"
                                    : result.solution == kNoNode),
                    "persistent decimal reporting cannot succeed after its step gate refuses");
        }
    }
    for (const std::string ending : {"complete", "cancel", "resource"}) {
        Budget budget;
        if (ending == "resource")
            budget.max_steps = 2;
        SolveTask task(SolveOperation::Rearrange, SolveRequest{"x+1/y=z"}, "x", 65536, Limits{}, budget);
        std::string previous;
        bool saw_prefix = false;
        while (task.state() == TaskState::Pending) {
            task.advance(1);
            t.check(records(task).starts_with(previous), "domain publication never changes an earlier payload");
            previous = records(task);
            if (task.published().size() > 1 && !saw_prefix) {
                saw_prefix = true;
                t.check(task.published().at(0).domain_restrictions ==
                            std::vector<std::string>{"y is not zero"},
                        "the published parent carries original domain before cancellation is possible");
                if (ending == "cancel")
                    task.cancel();
            }
        }
        t.check(saw_prefix && task.published().context.active_assumptions ==
                                 std::vector<std::string>{"y is not zero"},
                "original domain survives completion, cancellation and resource refusal of an inverse prefix");
        if (ending == "cancel")
            t.check(!task.result() && task.state() == TaskState::Cancelled,
                    "cancelled conditional rearrangement withholds its answer");
        if (ending == "resource")
            t.check(task.result() && std::get<RearrangeResult>(*task.result()).formula == kNoNode &&
                        task.published().context.derivation_status == DerivationStatus::ResourceLimitReached,
                    "resource refusal retains original conditions without offering a conditional answer");
    }
    {
        SolveRequest request{"2*x=1"};
        request.numeric_mode = NumericMode::Decimal;
        SolveTask task(SolveOperation::Linear, request, "x", 65536);
        while (task.state() == TaskState::Pending && task.published().size() < 4)
            task.advance(1);
        t.check(task.state() == TaskState::Pending && task.published().size() == 4,
                "decimal reporting yields after the checked exact prefix");
        const std::string prefix = records(task);
        task.cancel();
        t.check(!task.result() && task.state() == TaskState::Cancelled &&
                    task.published().context.derivation_status == DerivationStatus::Cancelled,
                "persistent cancellation before decimal reporting offers no exact fallback answer");
        t.equal(records(task), prefix, "cancelling decimal reporting preserves published exact work");
    }
    static_assert(!std::is_move_constructible_v<SolveTask>);
    static_assert(!std::is_copy_constructible_v<SolveTask>);
    for (const auto operation : {SolveOperation::Linear, SolveOperation::Rearrange}) {
        for (const std::string source : {"", "x=(", "x=1)"}) {
            SolveTask task(operation, SolveRequest{source}, "x", 65536);
            while (task.state() == TaskState::Pending)
                task.advance(1);
            t.check(task.result() && task.published().context.derivation_status == DerivationStatus::InvalidInput,
                    "malformed input is an input refusal with terminal context");
        }
        for (const std::string variable : {"", "x+y", " x", "2"}) {
            SolveTask task(operation, SolveRequest{"0=0"}, variable, 65536);
            while (task.state() == TaskState::Pending)
                task.advance(1);
            t.check(task.result() && task.published().context.derivation_status == DerivationStatus::InvalidInput,
                    "persistent solve rejects non-identifier unknown: " + variable);
        }
        Limits limits;
        limits.max_input_bytes = 8;
        SolveTask long_name(operation, SolveRequest{"0=0"}, "abcdefghi", 65536, limits);
        while (long_name.state() == TaskState::Pending)
            long_name.advance(1);
        t.check(long_name.published().context.derivation_status == DerivationStatus::InvalidInput &&
                    long_name.arena().node_count() == 0,
                "identifier admission honors the configured input-byte bound before parsing");
        SolveTask task(operation, SolveRequest{"2*π=6"}, "π", 65536);
        while (task.state() == TaskState::Pending)
            task.advance(1);
        t.check(task.result() && task.published().context.derivation_status == DerivationStatus::SolvedAndVerified,
                "persistent unknown normalization agrees with parsed Unicode pi");
        if (task.result()) {
            const NodeId answer = operation == SolveOperation::Linear
                                      ? std::get<SolveResult>(*task.result()).solution
                                      : std::get<RearrangeResult>(*task.result()).expression;
            Rational value;
            t.check(answer != kNoNode && evaluate_rational(task.arena(), answer, {}, &value) &&
                        value.num == 3 && value.den == 1,
                    "Unicode pi is the requested unknown and has exact answer three");
        }
        t.equal(task.published().context.original_expression, "2*π=6",
                "normalizing the requested unknown preserves original equation provenance");
    }
    {
        Limits limits;
        limits.max_depth = 4096;
        limits.max_nodes = 20000;
        Arena arena(limits);
        const NodeId x = arena.symbol("x");
        NodeId left = x;
        for (size_t depth = 0; depth < 2048; ++depth)
            left = arena.unary(Kind::Neg, left);
        const NodeId equation = arena.binary(Kind::Equals, left, arena.integer("1"));
        Derivation derivation;
        const SolveResult solved = solve_linear(arena, derivation, equation, x);
        t.check(solved.status == derivation.context.derivation_status,
                "synchronous frame exhaustion records the same terminal status as its result");
    }
    for (const char *source : {"y=x^2", "y=0*x", "a*(sin(x)+1)=b"}) {
        Arena arena;
        Derivation derivation;
        const NodeId equation = parse(arena, source).root;
        const RearrangeResult refused = rearrange(arena, derivation, equation, arena.symbol("x"));
        invariants::Pass pass;
        std::vector<std::string> broken;
        const bool has_answer = refused.formula != kNoNode;
        pass.walk(arena, derivation, true, true, &broken, &has_answer);
        t.check(broken.empty() && refused.formula == kNoNode,
                std::string("the refusal invariant accepts justified inverse-operation prefixes: ") + source);
        for (const std::string flaw : {"unverified", "no claim", "implication", "missing after",
                                       "answer present", "answer unknown", "not recorded", "conditional answer"}) {
            Derivation invalid;
            Step move = derivation.at(1);
            TransformationPayload payload = *derivation.transformation(1);
            if (flaw == "unverified")
                move.verifications.clear();
            if (flaw == "no claim")
                move.claim = ClaimType::NoClaim;
            if (flaw == "implication")
                move.claim = ClaimType::Implication;
            if (flaw == "missing after")
                payload.after = kNoNode;
            invalid.add_transformation(kNoStep, std::move(move), std::move(payload));
            invalid.context = derivation.context;
            if (flaw == "not recorded")
                invalid.context.derivation_status = DerivationStatus::NotRecorded;
            if (flaw == "conditional answer")
                invalid.context.derivation_status = DerivationStatus::ConditionallySolved;
            const bool has_invalid_answer = flaw == "answer present";
            invariants::Pass guard;
            broken.clear();
            guard.walk(arena, invalid, true, true, &broken,
                       flaw == "answer unknown" ? nullptr : &has_invalid_answer);
            t.check(std::any_of(broken.begin(), broken.end(), [](const std::string &why) {
                        return why.starts_with("criterion 8:");
                    }), "the refusal-prefix invariant rejects " + flaw);
        }
    }
    {
        using LinearFunction = SolveResult (*)(Arena &, Derivation &, NodeId, NodeId, const Budget &);
        using RearrangeFunction = RearrangeResult (*)(Arena &, Derivation &, NodeId, NodeId,
                                                      const Budget &, Backend *);
        LinearFunction original_linear = &solve_linear;
        RearrangeFunction original_rearrange = &rearrange;
        t.check(original_linear && original_rearrange, "existing synchronous function signatures remain available");
        for (const size_t capacity : {size_t{0}, size_t{128}}) {
            Arena arena;
            const NodeId equation = parse(arena, "a*(2*x+1)=b").root;
            const NodeId variable = arena.symbol("x");
            Derivation rearranged;
            const RearrangeResult r = rearrange(arena, rearranged, equation, variable, Budget{}, nullptr, capacity);
            t.check(r.status == DerivationStatus::ResourceLimitReached &&
                        r.status == rearranged.context.derivation_status && r.formula == kNoNode &&
                        rearranged.context.normalized_problem_model == equation,
                    "bounded rearrangement frame refusal records terminal context and no answer");
            Derivation linear;
            const NodeId numeric = parse(arena, "2*x=8").root;
            const SolveResult l = solve_linear(arena, linear, numeric, variable, Budget{}, capacity);
            t.check(l.status == DerivationStatus::ResourceLimitReached &&
                        l.status == linear.context.derivation_status && l.solution == kNoNode &&
                        linear.context.normalized_problem_model == numeric,
                    "bounded linear frame refusal records terminal context and no answer");
        }
    }
    for (size_t capacity : {size_t{0}, size_t{1}}) {
        SolveTask task(SolveOperation::Linear, SolveRequest{"x=1"}, "x", capacity);
        t.check(task.state() == TaskState::AllocationFailed &&
                    task.published().context.derivation_status == DerivationStatus::ResourceLimitReached &&
                    task.arena().node_count() == 0,
                "construction frame failure records resource context without parsing");
    }
    for (const char *source : {"y=x^2", "y=0*x", "a*(sin(x)+1)=b"}) {
        SolveTask task(SolveOperation::Rearrange, SolveRequest{source}, "x", 65536);
        bool falsely_passed = false;
        for (size_t i = 0; i < 1000 && task.state() == TaskState::Pending; ++i) {
            task.advance(1);
            for (const VerificationRecord &v : task.published().at(0).verifications) {
                if (v.method == "registered inverse-operation dispatch" &&
                    v.outcome == VerificationOutcome::Passed)
                    falsely_passed = true;
            }
        }
        t.check(!falsely_passed,
                std::string("unsupported inverse path never receives passed evidence: ") + source);
        t.check(task.result() && std::get<RearrangeResult>(*task.result()).formula == kNoNode,
                "unsupported inverse path withholds the terminal formula");
    }
    for (const SolveOperation op : {SolveOperation::Linear, SolveOperation::Rearrange}) {
        const std::string source = op == SolveOperation::Linear ? "2x+5=13" : "3*(2*x+5)=39";
        std::string expected_records;
        Cost expected_cost;
        size_t suspensions = 0;
        for (const size_t slice : {size_t{1}, size_t{2}, size_t{64}}) {
            SolveTask task(op, SolveRequest{source}, "x", 65536);
            t.check(task.state() == TaskState::Pending && task.arena().node_count() == 0 &&
                        task.published().size() == 0 && task.result() == nullptr &&
                        task.resources().cost.rewrites == 0,
                    "solve task construction performs no mathematical work");
            const auto initial = task.resources();
            task.advance(0);
            t.check(task.arena().node_count() == 0 && task.published().size() == 0 &&
                        task.resources().frame_live_bytes == initial.frame_live_bytes,
                    "zero slice does not parse or advance a solve");
            bool prefix = false;
            std::string previous;
            std::vector<std::vector<StepId>> children;
            size_t calls = 0;
            while (task.state() == TaskState::Pending && calls < 10000) {
                task.advance(slice);
                ++calls;
                const std::string next = records(task);
                t.check(next.starts_with(previous), "published mathematical rows remain unchanged");
                previous = next;
                for (size_t i = 0; i < task.published().size(); ++i) {
                    const Step &step = task.published().at(static_cast<StepId>(i));
                    t.check(step.id == i && (step.parent == kNoStep || step.parent < i),
                            "published IDs preserve ancestry without gaps");
                    if (i < children.size())
                        t.check(step.children.size() >= children[i].size() &&
                                    std::equal(children[i].begin(), children[i].end(), step.children.begin()),
                                "published child edges grow only by appending");
                    for (StepId child : step.children)
                        t.check(child < task.published().size() && task.published().at(child).parent == i,
                                "published child edge names a published child with the matching parent");
                    if (i == children.size())
                        children.push_back(step.children);
                    else
                        children[i] = step.children;
                }
                if (task.state() == TaskState::Pending) {
                    t.check(task.result() == nullptr, "pending solve withholds its final answer");
                    prefix = prefix || task.published().size() > 1;
                }
                for (size_t i = 0; i < task.published().size(); ++i) {
                    const Step &step = task.published().at(static_cast<StepId>(i));
                    t.check(step.kind == StepKind::Plan || step.verified(),
                            "every published mathematical row is verified");
                }
            }
            t.check(task.state() == TaskState::Complete && task.result(), "sliced solve completes");
            if (slice == 1) {
                t.check(prefix, "single-unit slices expose a verified prefix before completion");
                suspensions = calls;
                expected_records = records(task);
                expected_cost = task.resources().cost;
            } else {
                t.equal(records(task), expected_records, "slice size preserves derivation records");
                t.check(same_cost(task.resources().cost, expected_cost), "slice size preserves costs");
            }
            if (task.result()) {
                if (const auto *r = std::get_if<SolveResult>(task.result()))
                    t.check(r->outcome == SolveOutcome::Solved && print(task.arena(), r->solution) == "4",
                            "persistent linear solve returns exact four");
                if (const auto *r = std::get_if<RearrangeResult>(task.result()))
                    t.check(r->outcome == RearrangeOutcome::Isolated &&
                                print(task.arena(), r->expression) ==
                                    "(((39 * (3^(-1))) + (-5)) * (2^(-1)))",
                            "persistent nested rearrangement returns the fixed inverse-operation expression");
            }
            const Cost cost = task.resources().cost;
            t.check(cost.rewrites == (op == SolveOperation::Linear ? 6 : 3) &&
                        cost.steps == (op == SolveOperation::Linear ? 4 : 5) &&
                        cost.states == (op == SolveOperation::Linear ? 0 : 3) &&
                        cost.branches == 0 && cost.backend_calls == 0 && cost.replayed == 0,
                    "sliced solves retain fixed native rule costs");
            Arena synchronous_arena;
            Derivation synchronous;
            synchronous.request = SolveRequest{source};
            const NodeId equation = parse(synchronous_arena, source).root;
            const NodeId variable = synchronous_arena.symbol("x");
            Cost synchronous_cost;
            if (op == SolveOperation::Linear)
                synchronous_cost = solve_linear(synchronous_arena, synchronous, equation, variable).cost;
            else
                synchronous_cost = rearrange(synchronous_arena, synchronous, equation, variable).cost;
            t.equal(records(task), records(synchronous_arena, synchronous),
                    "synchronous wrappers and persistent slices record the same mathematics");
            t.check(same_cost(cost, synchronous_cost), "synchronous wrappers retain the same costs");
            const auto before_reads = task.resources();
            for (size_t i = 0; i < 100; ++i) {
                (void)task.result();
                (void)task.state();
                (void)task.published();
                (void)task.arena();
                (void)task.resources();
            }
            t.check(same_cost(before_reads.cost, task.resources().cost) &&
                        before_reads.ast_nodes == task.resources().ast_nodes &&
                        before_reads.frame_peak_bytes == task.resources().frame_peak_bytes,
                    "reading retained solve state does no additional work");
            task.close();
            task.close();
            t.check(task.state() == TaskState::Invalid && task.resources().frame_capacity == 0 &&
                        task.resources().frame_live_bytes == 0,
                    "close is idempotent and releases frame storage");
        }
        for (size_t stop = 0; stop < suspensions; ++stop) {
            SolveTask task(op, SolveRequest{source}, "x", 65536);
            for (size_t i = 0; i < stop; ++i)
                task.advance(1);
            const std::string prefix = records(task);
            task.cancel();
            t.check(task.state() == TaskState::Cancelled && task.result() == nullptr &&
                        task.resources().frame_live_bytes == 0 && task.resources().live_frames == 0,
                    "cancellation at every suspension releases frames and withholds the answer");
            t.equal(records(task), prefix, "cancel preserves published IDs and mathematical content");
            task.advance(64);
            t.equal(records(task), prefix, "cancelled solve cannot resume work");
        }
    }
    {
        SolveRequest request{"2*x=8"};
        std::string variable = "x";
        SolveTask first(SolveOperation::Linear, request, variable, 65536);
        request.original_expression = "x=999";
        variable = "y";
        SolveTask second(SolveOperation::Linear, SolveRequest{"3*y=21"}, "y", 65536);
        for (size_t i = 0; i < 1000 && (first.state() == TaskState::Pending ||
                                      second.state() == TaskState::Pending); ++i) {
            first.advance(1);
            second.advance(2);
        }
        t.check(first.result() && second.result(), "interleaved independent solves finish");
        if (first.result() && second.result()) {
            t.equal(print(first.arena(), std::get<SolveResult>(*first.result()).solution), "4",
                    "solve owns caller expression and variable strings");
            t.equal(print(second.arena(), std::get<SolveResult>(*second.result()).solution), "7",
                    "interleaving keeps arenas and solver state separate");
        }
    }
    {
        SolveTask task(SolveOperation::Rearrange, SolveRequest{"a*(2*x+1)=b"}, "x", 65536);
        bool divided = false;
        for (size_t i = 0; i < 1000 && task.state() == TaskState::Pending; ++i) {
            task.advance(1);
            if (task.published().size() > 1) {
                const Step &move = task.published().at(1);
                if (move.rule_id == "alg.rearrange.divide-both-sides") {
                    divided = true;
                    t.check(move.verified() && move.domain_restrictions == std::vector<std::string>{"a is not zero"},
                            "a symbolic division settles its condition before publication");
                    const TransformationPayload *operation = task.published().transformation(1);
                    t.check(operation && print(task.arena(), operation->after) ==
                                            "(((2 * x) + 1) = (b * (a^(-1))))",
                            "published symbolic division has the exact conditional inverse operation");
                    for (const auto &evidence : task.published().at(0).verifications) {
                        if (evidence.method == "registered inverse-operation dispatch")
                            t.check(evidence.outcome == VerificationOutcome::NotAttempted,
                                    "unvisited inner wrappers keep the whole-path plan pending");
                    }
                    break;
                }
            }
        }
        const std::string prefix = records(task);
        task.cancel();
        t.check(divided && task.state() == TaskState::Cancelled && !task.result(),
                "cancel after conditional outer division withholds the final answer");
        t.equal(records(task), prefix, "symbolic divisor conditions survive frame destruction");
        t.equal(task.published().context.original_expression, "a*(2*x+1)=b",
                "cancelled prefix retains original request provenance in terminal context");
        t.check(task.published().context.normalized_problem_model != kNoNode &&
                    task.published().context.active_assumptions == std::vector<std::string>{"a is not zero"},
                "cancelled prefix context retains its model and settled symbolic conditions");
    }
    {
        SolveTask measured(SolveOperation::Linear, SolveRequest{"0.5*(2*x+1)=3"}, "x", 65536);
        while (measured.state() == TaskState::Pending)
            measured.advance(1);
        SolveTask bounded(SolveOperation::Linear, SolveRequest{"0.5*(2*x+1)=3"}, "x",
                          measured.resources().frame_peak_bytes - 1);
        while (bounded.state() == TaskState::Pending)
            bounded.advance(1);
        t.check(bounded.state() == TaskState::AllocationFailed && bounded.published().size() > 0 &&
                    bounded.published().at(0).verified(),
                "frame exhaustion occurs after publishing the verified decimal-promotion prefix");
        t.check(bounded.published().context.original_expression == "0.5*(2*x+1)=3" &&
                    bounded.published().context.normalized_problem_model != kNoNode,
                "frame exhaustion after publication preserves terminal model and provenance");
    }
    {
        Limits limits;
        limits.max_nodes = 16;
        Arena arena(limits);
        Derivation derivation;
        derivation.request.original_expression = "a*(2*x+1)=b";
        const NodeId equation = parse(arena, derivation.request.original_expression).root;
        const RearrangeResult refused = rearrange(arena, derivation, equation, arena.symbol("x"));
        const std::vector<std::string> condition{"a is not zero"};
        t.check(refused.status == DerivationStatus::ResourceLimitReached && derivation.size() == 0 &&
                    refused.formula == kNoNode,
                "synchronous AST refusal discards its working derivation without an answer");
        Arena supported;
        Derivation prefix;
        const NodeId unsupported = parse(supported, "a*(sin(x)+1)=b").root;
        const RearrangeResult stopped = rearrange(supported, prefix, unsupported, supported.symbol("x"));
        t.check(stopped.status == DerivationStatus::Unsupported && prefix.size() > 1 &&
                    prefix.at(1).verified() && prefix.at(1).domain_restrictions == condition,
                "unsupported inner wrapper follows an independently verified symbolic division");
        t.check(stopped.restrictions == condition && prefix.context.active_assumptions == condition,
                "unsupported rearrangement retains prefix conditions in result and context");
        SolveTask bounded(SolveOperation::Rearrange, SolveRequest{"a*(2*x+1)=b"}, "x", 65536, limits);
        while (bounded.state() == TaskState::Pending)
            bounded.advance(1);
        t.check(bounded.result() && bounded.published().size() > 1 &&
                    bounded.published().at(1).verified() &&
                    bounded.published().context.active_assumptions == condition &&
                    std::get<RearrangeResult>(*bounded.result()).restrictions == condition,
                "persistent AST refusal retains the same verified prefix conditions");
    }
    for (const auto operation : {SolveOperation::Linear, SolveOperation::Rearrange}) {
        SolveTask measured(operation, SolveRequest{"2*x+5=13"}, "x", 65536);
        while (measured.state() == TaskState::Pending)
            measured.advance(1);
        const size_t peak = measured.resources().frame_peak_bytes;
        for (const size_t capacity : {peak - 1, peak}) {
            SolveTask bounded(operation, SolveRequest{"2*x+5=13"}, "x", capacity);
            std::string previous;
            for (size_t i = 0; i < 1000 && bounded.state() == TaskState::Pending; ++i) {
                bounded.advance(1);
                t.check(records(bounded).starts_with(previous), "frame exhaustion preserves published mathematics");
                previous = records(bounded);
            }
            t.check(bounded.state() == (capacity == peak ? TaskState::Complete : TaskState::AllocationFailed),
                    "exact measured frame capacity succeeds and one byte less fails");
            t.check(bounded.resources().frame_live_bytes == 0 &&
                        bounded.resources().frame_peak_bytes <= bounded.resources().frame_capacity,
                    "frame accounting stays inside capacity and releases all terminal frames");
        }
        for (size_t nodes = 0; nodes < 32; ++nodes) {
            Limits limits;
            limits.max_nodes = nodes;
            SolveTask bounded(operation, SolveRequest{"2*x+5=13"}, "x", 65536, limits);
            std::string previous;
            for (size_t i = 0; i < 1000 && bounded.state() == TaskState::Pending; ++i) {
                bounded.advance(1);
                t.check(records(bounded).starts_with(previous), "arena exhaustion preserves published mathematics");
                previous = records(bounded);
            }
            t.check(bounded.resources().ast_nodes <= nodes, "persistent solve respects every tested AST node cap");
            if (bounded.arena().failed() && bounded.result())
                std::visit([&](const auto &outcome) {
                    t.check(outcome.status == DerivationStatus::ResourceLimitReached,
                            "arena exhaustion is a mathematical resource outcome");
                }, *bounded.result());
        }
    }
    {
        size_t calls = 0;
        Budget budget;
        budget.poll = poll;
        budget.poll_context = &calls;
        SolveTask task(SolveOperation::Linear, SolveRequest{"x=1"}, "x", 65536, Limits{}, budget);
        task.advance(64);
        t.check(task.state() == TaskState::Invalid && calls == 0 && task.arena().node_count() == 0 &&
                    task.published().context.derivation_status == DerivationStatus::InvalidInput,
                "persistent solve rejects borrowed polling callbacks without invoking them");
    }
    for (const size_t capacity : {size_t{0}, size_t{1}}) {
        SolveTask task(SolveOperation::Linear, SolveRequest{"x=1"}, "x", capacity);
        task.advance(64);
        t.check(task.state() == TaskState::AllocationFailed && task.result() == nullptr &&
                    task.resources().frame_live_bytes == 0 && task.resources().frame_peak_bytes <= capacity,
                "tiny frame cap refuses without fallback allocation");
    }
}

}
