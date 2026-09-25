#include <string>
#include <vector>

#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "nps/steps/quadratic.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

bool mentions(const std::vector<std::string> &broken, const char *piece) {
    for (size_t i = 0; i < broken.size(); ++i) {
        if (broken[i].find(piece) != std::string::npos)
            return true;
    }
    return false;
}

QuadraticResult solve(Arena &arena, Derivation &d, const char *equation, const char *name,
                      const Budget &budget = Budget()) {
    ParseResult parsed = parse(arena, equation);
    if (!parsed.ok())
        return QuadraticResult();
    return solve_by_square_root(arena, d, parsed.root, arena.symbol(name), budget);
}

std::string roots(Arena &arena, const QuadraticResult &r) {
    std::string out;
    for (size_t i = 0; i < r.solutions.size(); ++i) {
        if (!out.empty())
            out += " ";
        out += print(arena, r.solutions[i]);
    }
    return out;
}

void test_square_root_split(TestSink &t) {
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "x^2 = 4", "x");
        t.equal(quadratic_outcome_name(r.outcome), "solved", "a pure square is solved");
        t.equal(roots(arena, r), "2 (-2)", "and both roots are returned, not the positive one");
        t.equal(derivation_status_name(r.status), "solved and verified",
                "with a status that says every check passed");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "3x^2 - 12 = 0", "x");
        t.equal(roots(arena, r), "2 (-2)", "a scaled square is isolated before it is split");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "4x^2 = 9", "x");
        t.equal(roots(arena, r), "(3 * (2^(-1))) (-(3 * (2^(-1))))",
                "roots that are fractions stay exact rather than becoming decimals");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "x^2 = 0", "x");
        t.equal(roots(arena, r), "0", "a square of zero has one root rather than zero written twice");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "x^2 + 4 = 0", "x");
        t.equal(quadratic_outcome_name(r.outcome), "no real solution",
                "a negative square has an empty solution set over the reals");
        t.equal(derivation_status_name(r.status), "solved and verified",
                "which is an answer with evidence rather than a refusal");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "x^2 = 5", "x");
        t.equal(quadratic_outcome_name(r.outcome), "outside the declared envelope",
                "a square with no exact rational root is refused rather than approximated");
        t.check(r.solutions.empty(), "and it hands back no value at all");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "x^2 + x = 6", "x");
        t.equal(quadratic_outcome_name(r.outcome), "not a pure quadratic in the unknown",
                "an equation with a term of degree one is outside this rule");
    }
}

void test_quadratic_teaching(TestSink &t) {
    struct Example {
        const char *equation;
        const char *action;
        const char *solutions;
    };
    const Example examples[] = {
        {"3x^2 - 12 = 0",
         "Collect on the left: 3*x^2 - 12 = 0. Add 12 to both sides, then divide both sides by 3",
         "2 (-2)"},
        {"-3x^2 + 12 = 0",
         "Collect on the left: -3*x^2 + 12 = 0. Subtract 12 from both sides, then divide both sides by -3",
         "2 (-2)"},
        {"x^2 - 4 = 0",
         "Collect on the left: x^2 - 4 = 0. Add 4 to both sides", "2 (-2)"},
        {"3x^2 = 0",
         "Collect on the left: 3*x^2 = 0. Divide both sides by 3", "0"},
        {"4x^2 = 9",
         "Collect on the left: 4*x^2 - 9 = 0. Add 9 to both sides, then divide both sides by 4",
         "(3 * (2^(-1))) (-(3 * (2^(-1))))"},
        {"2x^2 + 5 = x^2 + 9",
         "Collect on the left: x^2 - 4 = 0. Add 4 to both sides", "2 (-2)"},
        {"x^2 = 4", "Read the equation as it stands, with x^2 already alone", "2 (-2)"},
        {"x^2 = 0", "Read the equation as it stands, with x^2 already alone", "0"},
        {"x^2 + 4 = 0",
         "Collect on the left: x^2 + 4 = 0. Subtract 4 from both sides", ""},
    };
    for (const Example &example : examples) {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, example.equation, "x");
        t.equal(roots(arena, r), example.solutions, "teaching preserves the complete exact root set");
        t.check(r.status == DerivationStatus::SolvedAndVerified,
                "teaching retains verified quadratic outcomes");
        const TransformationPayload *isolation = nullptr;
        for (size_t i = 0; i < d.size(); ++i) {
            const StepId id = static_cast<StepId>(i);
            if (d.at(id).rule_id == "eq.quadratic.isolate-the-square")
                isolation = d.transformation(id);
            if (d.at(id).rule_id == "eq.quadratic.check-by-substitution") {
                const CheckPayload *check = d.check(id);
                t.check(check != nullptr, "each candidate exposes its original-equation check");
                if (check)
                    t.equal(check->expected_relation, "left side equals right side",
                            "substitution checks equality even when the original right side is nonzero");
            }
        }
        t.check(isolation != nullptr, "the square isolation exposes the operation to perform");
        if (isolation)
            t.equal(isolation->concrete_action, example.action,
                    "the isolation instruction accounts for every operation and its operand");
    }
    Arena arena;
    Derivation d;
    const QuadraticResult r = solve(arena, d, "y^2 = 0", "y");
    size_t cases = 0;
    for (size_t i = 0; i < d.size(); ++i) {
        const StepId id = static_cast<StepId>(i);
        const Step &step = d.at(id);
        if (step.kind == StepKind::Plan)
            t.check(step.explanation_short.find("both square roots") == std::string::npos,
                    "the zero-square plan does not promise two roots");
        if (step.rule_id != "eq.quadratic.square-root-case")
            continue;
        ++cases;
        t.equal(step.goal, "Take the square root of zero", "zero is not described as a positive root");
        t.equal(step.explanation_short, "y = 0 is the only value whose square is 0",
                "the zero branch is presented as the complete single solution");
        t.equal(step.explanation_detailed, "Only zero squares to zero. The positive and negative choices coincide, so write y = 0 once.",
                "zero-root teaching explains why the single case is complete");
        const BranchPayload *branch = d.branch(id);
        t.check(branch && branch->siblings_exhaustive &&
                    branch->resolution == BranchResolution::Solved,
                "the single zero case retains its completeness and soundness certificate");
    }
    t.check(cases == 1 && r.solutions.size() == 1, "the zero solution is recorded only once");
}

// Each way this rule can run out of room, against the one way an equation can genuinely be the wrong
// shape. The three refusals used to arrive as a claim that the input was not a pure quadratic, which
// is what the bridge reads before deciding whether Giac is worth asking.
void test_capacity_refusals_are_not_a_shape_claim(TestSink &t) {
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "9000000000*4000000000*x^2 = 1", "x");
        t.equal(quadratic_outcome_name(r.outcome), "resource exceeded",
                "a coefficient too large to read is the arithmetic running out");
        t.check(derivation_status_name(r.status) == std::string("resource limit reached"),
                "with the status naming the limit");
        t.check(r.solutions.empty(), "and no root is offered");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "x^2/2 + 9223372036854775807 = 0", "x");
        t.equal(quadratic_outcome_name(r.outcome), "resource exceeded",
                "and so is a square that will not fit once the constant is divided across");
        t.check(derivation_status_name(r.status) == std::string("resource limit reached"),
                "under the same status");
    }
    {
        Limits limits;
        limits.max_nodes = 4;
        Arena arena(limits);
        Derivation d;
        const QuadraticResult r = solve(arena, d, "x*x = 5", "x");
        t.equal(quadratic_outcome_name(r.outcome), "resource exceeded",
                "an arena with no room for the squared term refuses as a limit rather than a shape");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = solve(arena, d, "x^2 + x = 6", "x");
        t.equal(quadratic_outcome_name(r.outcome), "not a pure quadratic in the unknown",
                "while a term of degree one is still a claim about the equation");
        t.check(derivation_status_name(r.status) == std::string("unsupported"),
                "under the unsupported status rather than a resource limit");
    }
}

// PERF-008's branch limit landing inside a split. A split whose second case is refused would leave
// the first standing with siblings_exhaustive set, which is a complete-looking answer missing a root.
void test_branch_budget_inside_a_split(TestSink &t) {
    Arena arena;
    Derivation d;
    Budget one;
    one.max_branches = 1;
    const QuadraticResult r = solve(arena, d, "x^2 = 4", "x", one);
    t.equal(quadratic_outcome_name(r.outcome), "resource exceeded",
            "a budget of one branch cannot record a split of two");
    t.check(r.solutions.empty(), "and no root is offered");
    size_t branches = 0;
    for (size_t i = 0; i < d.size(); ++i)
        if (d.branch(static_cast<StepId>(i)) != nullptr) ++branches;
    t.evidence("STEP-024", d.size() == 2 && branches == 0,
               "a split that ran out of branch budget keeps the checked setup but no case, so a set "
               "of one cannot be read as the exhaustive set of two it was going to be");
    t.check(derivation_status_name(r.status) == std::string("resource limit reached"),
            "with the status naming the limit rather than claiming a partial answer");
}

// The substitution check running out of exact arithmetic on an equation that is true and whose roots
// are exact. Its sibling below is the control: one sign apart, and the sum that overflows becomes a
// difference that does not.
void test_substitution_out_of_exact_arithmetic(TestSink &t) {
    Arena arena;
    Derivation d;
    const QuadraticResult r =
        solve(arena, d, "x^2 + 9223372036854775806 - 9223372036854775806 = 4", "x");
    t.equal(quadratic_outcome_name(r.outcome), "resource exceeded",
            "a substitution that ran out of exact arithmetic is the build running out of room");
    t.check(derivation_status_name(r.status) == std::string("resource limit reached"),
            "and not a verification that failed, because nothing about the candidate was disproved");
    t.check(r.solutions.empty(), "no root is offered, because none was checked");
    size_t recorded_cases = 0;
    for (size_t i = 0; i < d.size(); ++i)
        if (d.branch(static_cast<StepId>(i)) != nullptr) ++recorded_cases;
    t.evidence("STEP-024", recorded_cases == 0,
               "and no case is recorded at all, so a split of one cannot be read as the exhaustive "
               "set of two it was going to be");
    invariants::Pass audit;
    std::vector<std::string> broken;
    audit.walk(arena, d, false, true, &broken);
    t.check(broken.empty(), std::string("the record left behind passes the invariant pass") +
                                (broken.empty() ? "" : ", got " + broken.front()));

    Arena control_arena;
    Derivation control_d;
    const QuadraticResult control =
        solve(control_arena, control_d, "x^2 - 9223372036854775806 = 4 - 9223372036854775806", "x");
    t.equal(quadratic_outcome_name(control.outcome), "solved",
            "the same shape one sign apart still solves, so the refusal above is the arithmetic "
            "rather than the size of the numbers");
    t.check(control.solutions.size() == 2 &&
                derivation_status_name(control.status) == std::string("solved and verified"),
            "with both roots and a verified walkthrough");
}

// VER-017's completeness half, tested as arithmetic rather than through a record. This is where the
// dropped root is caught, and it has to be here rather than in the invariant pass: the pass reads
// records, so it can ask whether a completeness claim is backed by a check and can never ask whether
// that check was right. Both questions have to be answered somewhere, and this is the second one.
void test_completeness_predicate(TestSink &t) {
    const auto complete = [](std::vector<Rational> roots, Rational square) {
        std::string why;
        return cases_reconstruct_the_square(roots, square, &why) == Reconstruction::Rebuilt;
    };
    const auto out_of_room = [](std::vector<Rational> roots, Rational square) {
        std::string why;
        return cases_reconstruct_the_square(roots, square, &why) == Reconstruction::OutOfRoom;
    };

    t.check(complete({{2, 1}, {-2, 1}}, {4, 1}), "both roots of a positive square rebuild it");
    t.evidence("VER-017", !complete({{2, 1}}, {4, 1}),
               "one root of a positive square does not rebuild it, so a split that keeps the "
               "positive root and drops the negative one cannot pass its completeness check");
    t.evidence("VER-017", !complete({{-2, 1}}, {4, 1}),
               "and dropping the positive one fails the same way, so the check is not keyed to the "
               "sign of the case that happened to survive");
    t.check(!complete({{2, 1}, {2, 1}}, {4, 1}),
            "the same root twice does not rebuild a square, so padding a split cannot fake it");
    t.check(!complete({{2, 1}, {-2, 1}, {3, 1}}, {4, 1}),
            "a case that is not a root of the equation is refused along with the set holding it");
    t.check(complete({{0, 1}}, {0, 1}),
            "a square of zero is rebuilt by its one case, counted as the repeated root it is");
    t.check(!complete({{0, 1}}, {4, 1}), "and that one case does not rebuild a square of four");
    t.check(complete({{3, 2}, {-3, 2}}, {9, 4}), "roots that are fractions rebuild their square");
    t.check(!complete({}, {4, 1}), "no cases at all rebuild nothing");

    // The arithmetic running out, driven at the predicate rather than through the rule, because the
    // construction inside the rule appears to keep the sum and product in range and no input has
    // been found that reaches this arm through it. A guard nobody has shown to work reads as
    // coverage and is worse than an honest gap.
    const Rational huge{4611686018427387904, 1};
    t.check(out_of_room({huge, huge}, {4, 1}),
            "a sum that leaves the exact range is the arithmetic running out, not a missing case");
    t.check(!complete({huge, huge}, {4, 1}) && !out_of_room({{2, 1}}, {4, 1}),
            "and the two answers stay apart: a dropped root is still missing rather than out of room");
}

// The shape of a split, with one knob per gate arm. Every mutation below flips exactly one of these
// and nothing else, so a gate going red names the field that moved rather than something that came
// with it.
struct Shape {
    bool exhaustive = true;
    bool exclusive = true;
    bool domain_consistent = true;
    const char *exhaustive_evidence = "root-coefficient reconstruction";
    bool completeness_passed = true;
    bool candidate_checks = true;
    BranchResolution resolution = BranchResolution::Solved;
    const char *resolution_evidence = "substitution into the original equation";
    bool second_case = true;
    bool second_case_repeats_the_first = false;
    bool siblings_agree = true;
    DerivationStatus status = DerivationStatus::SolvedAndVerified;
};

Step case_step() {
    Step s;
    s.phase = "solve";
    s.goal = "Take a square root";
    s.rule_id = "eq.quadratic.square-root-case";
    s.rule_name = "Square root case";
    s.claim = ClaimType::SolutionSetNarrowed;
    s.explanation_short = "one of the values whose square is 4";
    s.explanation_detailed = "A positive number has two real square roots, one of each sign.";
    s.proof_obligations.push_back(
        {"obl.quadratic.case-is-a-root", "this case's value squares to the isolated value"});
    VerificationRecord v;
    v.method = "exact rational square";
    v.outcome = VerificationOutcome::Passed;
    v.strength = EvidenceStrength::StructurallyValid;
    v.detail = "the case squares to the isolated value";
    s.verifications.push_back(std::move(v));
    return s;
}

void attach_candidate_check(Derivation &d, StepId branch) {
    Step s;
    s.phase = "check";
    s.goal = "Check this case";
    s.rule_id = "eq.quadratic.check-by-substitution";
    s.rule_name = "Check by substitution";
    s.claim = ClaimType::SolutionSetPreserved;
    s.explanation_short = "Put the case back into the equation as it was typed";
    s.explanation_detailed = "Substituting into the original is what makes this a check.";
    s.proof_obligations.push_back(
        {"obl.quadratic.candidate-satisfies", "the candidate satisfies the original equation"});
    VerificationRecord v;
    v.method = "substitution";
    v.outcome = VerificationOutcome::Passed;
    v.strength = EvidenceStrength::CandidateChecked;
    v.detail = "the left side reduced to zero";
    s.verifications.push_back(std::move(v));
    CheckPayload p;
    p.target_claim = "the case satisfies the equation";
    p.check_method = "substitute the case into the original equation";
    p.expected_relation = "left side equals zero";
    p.observed_result = "left side is zero";
    d.add_check(branch, std::move(s), std::move(p));
}

// One split, built to the shape given. Two cases x = 2 and x = -2 under an isolating transformation,
// with a closing completeness check beside them, which is what the engine produces.
void build_split(Arena &arena, Derivation &d, const Shape &shape) {
    Step isolate;
    isolate.phase = "solve";
    isolate.goal = "Get x^2 by itself";
    isolate.rule_id = "eq.quadratic.isolate-the-square";
    isolate.rule_name = "Isolate the square";
    isolate.claim = ClaimType::SolutionSetPreserved;
    isolate.explanation_short = "x^2 is already by itself";
    isolate.explanation_detailed = "Treating x^2 as the subject makes this a one-step equation.";
    isolate.proof_obligations.push_back(
        {"obl.eq.same-solutions", "the rewritten equation has the solutions the original had"});
    VerificationRecord invariant;
    invariant.method = "rule-local equality invariant";
    invariant.outcome = VerificationOutcome::Passed;
    invariant.strength = EvidenceStrength::StructurallyValid;
    invariant.detail = "nothing moved";
    isolate.verifications.push_back(std::move(invariant));
    TransformationPayload isolate_payload;
    isolate_payload.before = parse(arena, "x^2 = 4").root;
    isolate_payload.after = isolate_payload.before;
    isolate_payload.concrete_action = "Read the equation as it stands";
    const StepId parent =
        d.add_transformation(kNoStep, std::move(isolate), std::move(isolate_payload));

    Meter meter{Budget()};
    const NodeId first_condition = parse(arena, "x = 2").root;
    const NodeId second_condition =
        shape.second_case_repeats_the_first ? first_condition : parse(arena, "x = -2").root;

    const size_t wanted = shape.second_case ? 2 : 1;
    for (size_t i = 0; i < wanted; ++i) {
        BranchPayload payload;
        payload.condition = i == 0 ? first_condition : second_condition;
        payload.siblings_exhaustive = shape.exhaustive;
        payload.siblings_exclusive = shape.exclusive;
        payload.siblings_domain_consistent = shape.domain_consistent;
        // The one knob that makes a sibling disagree rather than making the whole split wrong.
        if (i == 1 && !shape.siblings_agree)
            payload.siblings_exclusive = !shape.exclusive;
        payload.exhaustive_evidence = shape.exhaustive_evidence;
        payload.feasibility_status = "feasible";
        payload.resolution = shape.resolution;
        payload.resolution_evidence = shape.resolution_evidence;
        const StepId id = d.add_branch(meter, parent, case_step(), std::move(payload));
        if (shape.candidate_checks && shape.resolution == BranchResolution::Solved)
            attach_candidate_check(d, id);
    }

    Step closing;
    closing.phase = "check";
    closing.goal = "Check that no case is missing";
    closing.rule_id = "eq.quadratic.cases-reconstruct-the-original";
    closing.rule_name = "Completeness of the split";
    closing.claim = ClaimType::SolutionSetPreserved;
    closing.explanation_short = "The cases multiply back out to the equation that was split";
    closing.explanation_detailed = "Checking each case says nothing about a case left out.";
    closing.proof_obligations.push_back(
        {"obl.quadratic.cases-are-complete",
         "every real value satisfying the equation is one of the cases recorded"});
    VerificationRecord v;
    v.method = "root-coefficient reconstruction";
    v.outcome = shape.completeness_passed ? VerificationOutcome::Passed
                                          : VerificationOutcome::Failed;
    v.strength = strength_for(v.outcome, EvidenceStrength::SymbolicallyEquivalentUnderAssumptions);
    v.detail = "the cases rebuild x^2 = 4";
    closing.verifications.push_back(std::move(v));
    CheckPayload closing_payload;
    closing_payload.target_claim = "the cases recorded are every real solution";
    closing_payload.check_method = "rebuild the quadratic from the recorded cases and compare it";
    closing_payload.expected_relation = "the rebuilt quadratic is the one that was split";
    closing_payload.observed_result = "the rebuilt quadratic is the one that was split";
    d.add_check(parent, std::move(closing), std::move(closing_payload));

    d.context.derivation_status = shape.status;
}

std::vector<std::string> walk_shape(const Shape &shape) {
    Arena arena;
    Derivation d;
    build_split(arena, d, shape);
    invariants::Pass pass;
    std::vector<std::string> broken;
    pass.walk(arena, d, false, false, &broken);
    return broken;
}

// Every gate arm, driven red by changing one field of a split that is otherwise clean. The clean
// control runs first, because a mutation failing a gate proves nothing if the unmutated shape fails
// it too: the pair is what says the gate reads the field that moved.
void test_split_gates_fail_when_a_case_goes_missing(TestSink &t) {
    const std::vector<std::string> clean = walk_shape(Shape());
    t.check(!mentions(clean, "STEP-024") && !mentions(clean, "VER-017"),
            "a well formed split breaks neither STEP-024 nor VER-017");

    {
        Shape duplicated;
        duplicated.second_case_repeats_the_first = true;
        const std::vector<std::string> broken = walk_shape(duplicated);
        t.evidence("STEP-024", mentions(broken, "states one of them twice"),
                   "a split claiming its cases are mutually exclusive and listing one twice is "
                   "reported, so a duplicate case is compared rather than believed, which is as "
                   "far as this reads exclusivity");
    }
    {
        Shape unresolved;
        unresolved.resolution = BranchResolution::Unresolved;
        const std::vector<std::string> broken = walk_shape(unresolved);
        t.evidence("STEP-024", mentions(broken, "left a case unresolved"),
                   "a case that is neither solved nor rejected is reported");
        t.evidence("VER-017", mentions(broken, "over a case"),
                   "and a derivation reporting a solution over it is reported separately, so the "
                   "status cannot outrun the cases");
    }
    {
        Shape unexplained;
        unexplained.resolution_evidence = "";
        const std::vector<std::string> broken = walk_shape(unexplained);
        t.evidence("STEP-024", mentions(broken, "does not say what settled it"),
                   "a case recorded as solved with nothing named as settling it is reported");
    }
    {
        Shape unargued;
        unargued.exhaustive_evidence = "";
        const std::vector<std::string> broken = walk_shape(unargued);
        t.evidence("VER-017", mentions(broken, "names no method that argues it"),
                   "exhaustiveness claimed with no method named to argue it is reported, so the "
                   "claim cannot be made by setting a boolean");
    }
    {
        Shape failing;
        failing.completeness_passed = false;
        const std::vector<std::string> broken = walk_shape(failing);
        t.evidence("VER-017",
                   mentions(broken, "no record in the split carries a passing verification"),
                   "exhaustiveness claimed while the check that argues it came back false is "
                   "reported, so a failed completeness check cannot pass for evidence");
    }
    {
        Shape unchecked;
        unchecked.candidate_checks = false;
        const std::vector<std::string> broken = walk_shape(unchecked);
        t.evidence("VER-017", mentions(broken, "no passing candidate check"),
                   "a case recorded as solved with nothing substituted back is reported, which is "
                   "VER-017's soundness half read one case at a time");
    }
    {
        Shape disagreeing;
        disagreeing.siblings_agree = false;
        const std::vector<std::string> broken = walk_shape(disagreeing);
        t.evidence("STEP-024", mentions(broken, "cases that disagree"),
                   "two cases of one split disagreeing about whether the set is exclusive is "
                   "reported, so a property of the set cannot be stated two ways");
    }
    {
        // The counts the two claims are quantified over. A pass that met no split would report both
        // claims held, which is the reading this measures against.
        Arena arena;
        Derivation d;
        build_split(arena, d, Shape());
        invariants::Pass pass;
        std::vector<std::string> broken;
        pass.walk(arena, d, false, false, &broken);
        t.check(pass.split_count() == 1 && pass.branch_steps() == 2,
                "the pass counted one split of two cases, which is the population the two claims "
                "above are quantified over");
        t.check(pass.restricted_splits() == 0,
                "and none of them ran with a domain restriction in force, so the arm asking a "
                "domain-consistency claim to be argued has judged nothing");
    }
}

QuadraticResult by_formula(Arena &arena, Derivation &d, const char *equation, const char *name,
                           const Budget &budget = Budget()) {
    ParseResult parsed = parse(arena, equation);
    if (!parsed.ok())
        return QuadraticResult();
    return solve_quadratic(arena, d, parsed.root, arena.symbol(name), budget);
}

std::string rule_ids(const Derivation &d) {
    std::string out;
    for (size_t i = 0; i < d.size(); ++i) {
        const Step &s = d.at(static_cast<StepId>(i));
        if (!s.rule_id.empty())
            out += (out.empty() ? "" : " ") + s.rule_id;
    }
    return out;
}

bool says(const std::string &text, const char *piece) {
    return text.find(piece) != std::string::npos;
}

// The half of degree two the square-root rule cannot reach. Issue 412.
void test_quadratic_formula(TestSink &t) {
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, "3x^2 + 10x - 88 = 0", "x");
        t.equal(quadratic_outcome_name(r.outcome), "solved",
                "an equation with a term of degree one is solved rather than refused");
        t.equal(roots(arena, r), "4 (-(22 * (3^(-1))))",
                "and both roots come back exact, in the order the formula takes its signs");
        t.equal(derivation_status_name(r.status), "solved and verified",
                "with every check in the split passing");
        const std::string rules = rule_ids(d);
        t.check(says(rules, "eq.quadratic.formula") && says(rules, "eq.quadratic.standard-form") &&
                    says(rules, "eq.quadratic.discriminant") &&
                    says(rules, "eq.quadratic.formula-case") &&
                    says(rules, "eq.quadratic.cases-reconstruct-the-original"),
                "the plan, the collection, the discriminant, the cases and the completeness check "
                "are all recorded");
        t.check(says(rules, "eq.quadratic.check-by-substitution"),
                "and each case is put back into the equation as it was typed");
    }
    {
        // Read the other way round, so the leading coefficient is negative and the signs swap order.
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, "88 = 3x^2 + 10x", "x");
        t.equal(roots(arena, r), "(-(22 * (3^(-1)))) 4",
                "an equation with terms on both sides reaches the same pair of roots");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, "x^2 + 2x + 1 = 0", "x");
        t.equal(roots(arena, r), "(-1)",
                "a discriminant of zero records one root rather than the same one twice");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, "x^2 + x + 1 = 0", "x");
        t.equal(quadratic_outcome_name(r.outcome), "no real solution",
                "a negative discriminant is an answer rather than a refusal");
        t.equal(derivation_status_name(r.status), "solved and verified",
                "and it carries the evidence for the empty solution set");
        t.check(says(rule_ids(d), "eq.quadratic.reject-negative-discriminant"),
                "with the rejection recorded as a case of its own");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, "x^2 + x - 1 = 0", "x");
        t.equal(quadratic_outcome_name(r.outcome), "outside the declared envelope",
                "a discriminant with no exact root is inside the family and outside the envelope");
        t.check(says(r.detail, "5"), "and the refusal names the discriminant it stopped at");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, "2x + 1 = 0", "x");
        t.equal(quadratic_outcome_name(r.outcome), "not a pure quadratic in the unknown",
                "an equation of degree one is handed back rather than divided by a zero leading "
                "coefficient");
        t.check(says(r.detail, "linear rule"), "naming the rule that does solve it");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, "x^3 + x = 0", "x");
        t.check(says(r.detail, "above the second power"),
                "degree three is refused for being too high rather than read as degree two");
        t.check(d.size() == 0, "and nothing is recorded before the refusal");
    }
    {
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, "x^2 + y*x = 1", "x");
        t.check(says(r.detail, "rational coefficients"),
                "a symbolic coefficient is refused rather than evaluated to something");
    }
    {
        // A pure square is this rule's b equal to zero, so it still solves.
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, "x^2 = 4", "x");
        t.equal(roots(arena, r), "2 (-2)", "a pure square is the formula's b equal to zero");
    }
    // Shapes with no bounded degree, where reading three values would be sampling. Issue 412.
    for (const char *shape : {"8*2^x = x^2 + 7x + 8", "2^x = x + 1", "x^2 = 2^x",
                              "sin(x) + x^2 = 0", "sqrt(x) = x^2", "1/x + x = 2",
                              "abs(x) = x^2", "x^x = 4"}) {
        Arena arena;
        Derivation d;
        const QuadraticResult r = by_formula(arena, d, shape, "x");
        t.check(r.outcome != QuadraticOutcome::Solved && r.solutions.empty() && d.size() == 0,
                std::string("a shape that is not a polynomial is refused with nothing recorded: ") +
                    shape);
        t.check(says(r.detail, "polynomial of degree two"),
                std::string("and the refusal says that is what it needed: ") + shape);
    }
    {
        // The controls, so the refusals above are about the shape rather than about any power.
        Arena arena;
        Derivation d;
        t.equal(roots(arena, by_formula(arena, d, "x^2 + x = 2^2 + 2", "x")), "2 (-3)",
                "a constant raised to a constant is still a constant");
    }
    {
        Arena arena;
        Derivation d;
        t.equal(roots(arena, by_formula(arena, d, "x^2/2 + x/2 = 3", "x")), "2 (-3)",
                "and division by a constant, which is a power of minus one, still reads");
    }
    {
        // The completeness predicate on its own, with a linear term in it.
        std::string why;
        const std::vector<Rational> both{Rational{4, 1}, Rational{-22, 3}};
        t.check(cases_reconstruct_the_monic(both, Rational{10, 3}, Rational{-88, 3}, &why) ==
                    Reconstruction::Rebuilt,
                "two roots rebuild the monic quadratic they came from");
        const std::vector<Rational> one{Rational{4, 1}};
        t.check(cases_reconstruct_the_monic(one, Rational{10, 3}, Rational{-88, 3}, &why) ==
                    Reconstruction::Missing,
                "and a split that dropped a root does not");
        t.check(says(why, "degree one"), "saying which coefficient disagreed");
    }
}

}  // namespace

void run_quadratic_tests(TestSink &sink) {
    test_quadratic_formula(sink);
    for (const char *source : {"x^2=[4]", "x^2+0*[1]=4", "[[4]]=x^2", "x^2=f([4])"}) {
        Arena arena;
        const NodeId equation = parse(arena, source).root;
        Derivation derivation;
        const QuadraticResult result =
            solve_by_square_root(arena, derivation, equation, arena.symbol("x"));
        sink.check(result.status == DerivationStatus::Unsupported && result.solutions.empty() &&
                       derivation.size() == 0 && result.cost.rewrites == 0,
                   std::string("quadratic solving refuses collections before scalar work: ") + source);
    }
    test_square_root_split(sink);
    test_quadratic_teaching(sink);
    test_branch_budget_inside_a_split(sink);
    test_capacity_refusals_are_not_a_shape_claim(sink);
    test_substitution_out_of_exact_arithmetic(sink);
    test_completeness_predicate(sink);
    test_split_gates_fail_when_a_case_goes_missing(sink);
}

}  // namespace nps
