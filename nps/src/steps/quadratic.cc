#include "nps/steps/quadratic.h"

#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/core/rational.h"
#include "nps/steps/linear.h"

namespace nps {
namespace {

NodeId rational_node(Arena &arena, const Rational &value) {
    const bool negative = value.num < 0;
    const int64_t magnitude = negative ? -value.num : value.num;
    NodeId n = arena.integer(integer_text(magnitude));
    if (value.den != 1) {
        NodeId d = arena.integer(integer_text(value.den));
        n = arena.binary(Kind::Mul, n,
                         arena.binary(Kind::Pow, d, arena.unary(Kind::Neg, arena.integer("1"))));
    }
    return negative ? arena.unary(Kind::Neg, n) : n;
}

VerificationRecord passed(const char *method, EvidenceStrength strength,
                          const std::string &detail) {
    VerificationRecord v;
    v.method = method;
    v.outcome = VerificationOutcome::Passed;
    v.strength = strength;
    v.detail = detail;
    return v;
}

VerificationRecord failed(const char *method, const std::string &detail) {
    VerificationRecord v;
    v.method = method;
    v.outcome = VerificationOutcome::Failed;
    v.strength = EvidenceStrength::Failed;
    v.detail = detail;
    return v;
}

VerificationRecord inconclusive(const char *method, const std::string &detail) {
    VerificationRecord v;
    v.method = method;
    v.outcome = VerificationOutcome::Inconclusive;
    v.strength = strength_for(VerificationOutcome::Inconclusive, EvidenceStrength::CandidateChecked);
    v.detail = detail;
    return v;
}

}  // namespace

// A monic quadratic is its roots: (x - r1)(x - r2) = x^2 - (r1 + r2)x + r1*r2, so matching it
// against x^2 - square asks two things of the recorded set, that the coefficient of x it implies is
// zero and that its constant term is -square. One recorded case stands for a repeated root, which is
// the same identity with r1 = r2.
// Chosen over expanding the factored form because it is exact integer arithmetic with no backend
// behind it, so the fixtures and the corpus can both run it, and it stays out of canonical.cc.
Reconstruction cases_reconstruct_the_square(const std::vector<Rational> &roots,
                                            const Rational &square, std::string *why) {
    Rational sum{0, 1};
    Rational product{1, 1};
    if (roots.size() == 1) {
        // The repeated root, counted twice, because a single case for a degree-two equation claims
        // multiplicity rather than claiming the other root was not worth recording.
        if (!rational_add(roots[0], roots[0], &sum) ||
            !rational_mul(roots[0], roots[0], &product)) {
            *why = "rebuilding the quadratic from its cases ran out of exact arithmetic";
            return Reconstruction::OutOfRoom;
        }
    } else if (roots.size() == 2) {
        if (!rational_add(roots[0], roots[1], &sum) ||
            !rational_mul(roots[0], roots[1], &product)) {
            *why = "rebuilding the quadratic from its cases ran out of exact arithmetic";
            return Reconstruction::OutOfRoom;
        }
    } else {
        *why = "a degree two equation was split into " +
               integer_text(static_cast<int64_t>(roots.size())) +
               " cases, which cannot rebuild it";
        return Reconstruction::Missing;
    }

    Rational negated_square;
    if (!rational_sub(Rational{0, 1}, square, &negated_square)) {
        *why = "rebuilding the quadratic from its cases ran out of exact arithmetic";
        return Reconstruction::OutOfRoom;
    }
    if (!rational_equal(sum, Rational{0, 1})) {
        *why = "the cases imply a term in the unknown of degree one, which the equation solved does "
               "not have, so a case is missing or wrong";
        return Reconstruction::Missing;
    }
    if (!rational_equal(product, negated_square)) {
        *why = "the cases multiply out to a constant term the equation solved does not have";
        return Reconstruction::Missing;
    }
    return Reconstruction::Rebuilt;
}

namespace {

std::string rational_text_for(Arena &arena, const Rational &value) {
    return print(arena, rational_node(arena, value));
}

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, NumericMode mode) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "algebra.quadratic.pure-square.one-unknown";
    inputs.requested_method = "isolate the square and split on its roots";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.angle_convention = "radians";
    inputs.branch_convention = "real domain";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.numeric_mode = mode;
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

// Contradicted disproves a candidate. OutOfRoom proves nothing about it, and VER-017's soundness
// half has no slot for a case whose check never ran, so the two cannot share a refusal.
enum class Substitution : uint8_t { Satisfied, OutOfRoom, Contradicted };

// What one case needs before it can be recorded, gathered so the three arms below build a case the
// same way rather than each spelling out its own payload.
struct Case {
    Rational root;
    NodeId condition = kNoNode;
    Substitution substitution = Substitution::OutOfRoom;
    std::string substitution_detail;
};

// The candidate check VER-017 calls soundness, done by walking the equation as it was typed. Reading
// the collected coefficients instead would ask the case to agree with the analysis that produced it,
// and a miscollection would then check out against itself.
Substitution satisfies_original(const Arena &arena, NodeId equation, const std::string &name,
                                const Rational &root, std::string *detail) {
    const std::vector<SymbolValue> values{{name, root}};
    const ChildView sides = arena.children(arena.at(equation));
    Rational left;
    Rational right;
    if (!evaluate_rational(arena, sides[0], values, &left) ||
        !evaluate_rational(arena, sides[1], values, &right)) {
        *detail = "the substitution ran out of exact arithmetic";
        return Substitution::OutOfRoom;
    }
    if (!rational_equal(left, right)) {
        *detail = "the two sides did not come out equal";
        return Substitution::Contradicted;
    }
    *detail = "both sides came out equal";
    return Substitution::Satisfied;
}

}  // namespace

const char *quadratic_outcome_name(QuadraticOutcome o) {
    switch (o) {
        case QuadraticOutcome::Solved: return "solved";
        case QuadraticOutcome::NoRealSolution: return "no real solution";
        case QuadraticOutcome::NotPureQuadratic: return "not a pure quadratic in the unknown";
        case QuadraticOutcome::NotAnEquation: return "not an equation";
        case QuadraticOutcome::OutsideEnvelope: return "outside the declared envelope";
        case QuadraticOutcome::Refused: return "refused";
        case QuadraticOutcome::Cancelled: return "cancelled";
        case QuadraticOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

namespace {

// The body of a solve. The wrapper below owns the halt handling, the same split linear.cc uses, so
// no return in here has to remember to roll anything back.
QuadraticResult solve_body(Arena &arena, Derivation &derivation, NodeId equation, NodeId unknown,
                           Meter &meter) {
    QuadraticResult result;
    if (equation == kNoNode || unknown == kNoNode || arena.failed()) {
        result.detail = "nothing to solve";
        result.status = DerivationStatus::InvalidInput;
        return result;
    }
    if (arena.at(equation).kind != Kind::Equals) {
        result.outcome = QuadraticOutcome::NotAnEquation;
        result.detail = "this rule solves an equation, and that is not one";
        result.status = DerivationStatus::InvalidInput;
        return result;
    }
    if (arena.at(unknown).kind != Kind::Symbol) {
        result.detail = "the unknown has to be a symbol";
        result.status = DerivationStatus::InvalidInput;
        return result;
    }

    if (contains_list(arena, equation)) {
        result.outcome = QuadraticOutcome::NotPureQuadratic;
        result.detail = "list and matrix equations are not supported";
        result.status = DerivationStatus::Unsupported;
        return result;
    }

    const std::string name = arena.text(unknown);
    const NodeId square_term = arena.binary(Kind::Pow, unknown, arena.integer("2"));
    if (square_term == kNoNode || arena.failed()) {
        result.outcome = QuadraticOutcome::ResourceExceeded;
        result.detail = "the arena could not hold the squared term";
        result.status = DerivationStatus::ResourceLimitReached;
        return result;
    }

    // Read as linear in the squared term rather than in the unknown. An equation with a term of
    // degree one leaves a bare symbol the analysis cannot account for, so it comes back NotLinear
    // and is refused here rather than half solved.
    Rational coefficient;
    Rational constant;
    const LinearForm form =
        linear_form(arena, equation, square_term, meter, &coefficient, &constant);
    if (meter.stopped())
        return result;
    switch (form) {
        case LinearForm::Reduced:
            break;
        case LinearForm::NotAnEquation:
            result.outcome = QuadraticOutcome::NotAnEquation;
            result.detail = "this rule solves an equation, and that is not one";
            result.status = DerivationStatus::InvalidInput;
            return result;
        case LinearForm::Overflowed:
            result.outcome = QuadraticOutcome::ResourceExceeded;
            result.detail = "a value grew past what exact integer arithmetic here can hold";
            result.status = DerivationStatus::ResourceLimitReached;
            return result;
        case LinearForm::Halted:
            return result;
        case LinearForm::NotLinear:
            result.outcome = QuadraticOutcome::NotPureQuadratic;
            result.detail = "this rule needs " + name +
                            "^2 against constants and nothing else in " + name;
            result.status = DerivationStatus::Unsupported;
            return result;
    }

    // a*x^2 + b = 0 becomes x^2 = -b/a.
    Rational square;
    Rational negated_constant;
    if (!rational_sub(Rational{0, 1}, constant, &negated_constant) ||
        !rational_div(negated_constant, coefficient, &square)) {
        result.outcome = QuadraticOutcome::ResourceExceeded;
        result.detail = "the coefficients grew past what exact integer arithmetic here can hold";
        result.status = DerivationStatus::ResourceLimitReached;
        return result;
    }

    Rational root;
    const bool negative_square = square.num < 0;
    if (!negative_square && !rational_sqrt_exact(square, &root)) {
        // Inside the family and outside the envelope. Reported as its own outcome rather than as
        // "not a quadratic", because the two send a reader to different places: one says bring a
        // different equation and the other says this engine does not go that far yet.
        result.outcome = QuadraticOutcome::OutsideEnvelope;
        result.detail = "the square of " + name + " is " + rational_text_for(arena, square) +
                        ", whose square root is not exact, and this rule reports no decimals";
        result.status = DerivationStatus::Unsupported;
        return result;
    }

    PlanPayload plan;
    plan.strategy_id = "eq.quadratic.square-root";
    plan.selected_strategy = "Isolate the square and split on its roots";
    plan.matched_problem_facts.push_back("one unknown, " + name);
    plan.matched_problem_facts.push_back("degree two in " + name + " with no term of degree one");
    plan.alternatives_considered.push_back("the quadratic formula, which this equation does not need"
                                           " because it has no term of degree one");
    plan.selection_rationale =
        "with no term of degree one the square can be isolated in one move, and every real " + name +
        " whose square is that value is then read off directly";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Solve for " + name;
    plan_step.rule_id = plan.strategy_id;
    plan_step.rule_name = "Isolate the square and split on its roots";
    plan_step.claim = ClaimType::NoClaim;
    plan_step.explanation_short = "Get " + name + "^2 alone, then find all real square roots";
    register_strategy_precondition(
        plan, plan_step, "pre.quadratic.pure-square",
        "the equation is degree two in " + name + " with no term of degree one",
        "exact linear analysis in the squared term", EvidenceStrength::StructurallyValid,
        VerificationOutcome::Passed,
        "both sides reduced to a coefficient on " + name + "^2 and a constant term");
    register_strategy_precondition(
        plan, plan_step, "pre.quadratic.exact-square-root",
        "the isolated square has an exact rational square root or is negative",
        "exact rational square root", EvidenceStrength::StructurallyValid,
        VerificationOutcome::Passed,
        negative_square ? "the isolated square is negative, so no real root is needed"
                        : "the isolated square is a rational perfect square");
    if (!meter.step())
        return result;
    const StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    const NodeId isolated = arena.binary(Kind::Equals, square_term, rational_node(arena, square));
    if (isolated == kNoNode || arena.failed()) {
        result.detail = "the arena could not hold the isolated equation";
        result.status = DerivationStatus::ResourceLimitReached;
        return result;
    }

    // A square already sitting alone is not divided by anything, and a step saying it was would be
    // describing a move nobody made. STEP-022 is about a label standing in for the work; this is the
    // same fault the other way round, a label naming work that did not happen.
    const bool already_isolated = isolated == equation;

    if (!meter.step())
        return result;
    Step isolate;
    isolate.phase = "solve";
    isolate.goal = "Get " + name + "^2 by itself";
    isolate.rule_id = "eq.quadratic.isolate-the-square";
    isolate.rule_name = "Isolate the square";
    isolate.claim = ClaimType::SolutionSetPreserved;
    std::string action = "Read the equation as it stands, with " + name + "^2 already alone";
    if (!already_isolated) {
        action = "Collect on the left: ";
        if (!rational_equal(coefficient, Rational{1, 1}))
            action += rational_text(coefficient) + "*";
        action += name + "^2";
        if (constant.num < 0)
            action += " - " + rational_text(negated_constant);
        else if (constant.num > 0)
            action += " + " + rational_text(constant);
        action += " = 0. ";
        if (constant.num < 0)
            action += "Add " + rational_text(negated_constant) + " to both sides";
        else if (constant.num > 0)
            action += "Subtract " + rational_text(constant) + " from both sides";
        if (!rational_equal(coefficient, Rational{1, 1}))
            action += std::string(constant.num == 0 ? "Divide" : ", then divide") +
                      " both sides by " + rational_text(coefficient);
        else if (constant.num == 0)
            action += "Read off " + name + "^2 = 0";
    }
    isolate.explanation_short = already_isolated
                                    ? name + "^2 is already by itself, so there is nothing to move"
                                    : "Treat " + name + "^2 as the unknown and undo its operations";
    isolate.explanation_detailed =
        "Adding the same quantity to both sides and dividing by a nonzero coefficient preserve "
        "the solutions.";
    isolate.proof_obligations.push_back(
        {"obl.eq.same-solutions", "the rewritten equation has the solutions the original had"});
    isolate.verifications.push_back(
        passed("rule-local equality invariant", EvidenceStrength::StructurallyValid,
               already_isolated
                   ? "the equation was already " + name + "^2 against a constant, so nothing moved"
                   : "the coefficient of " + name + "^2 was checked to be non-zero before dividing"));
    TransformationPayload isolate_payload;
    isolate_payload.before = equation;
    isolate_payload.after = isolated;
    isolate_payload.concrete_action = action;
    isolate_payload.reversible = true;
    const StepId isolate_id =
        derivation.add_transformation(plan_id, std::move(isolate), std::move(isolate_payload));

    // The cases, built before any is recorded. A split whose second case turns out to be impossible
    // to record must leave no first case behind, and deciding the whole set first is what makes that
    // one rewind rather than a repair.
    std::vector<Case> cases;
    if (!negative_square) {
        Rational negated_root;
        if (!rational_sub(Rational{0, 1}, root, &negated_root)) {
            result.detail = "the roots grew past what exact integer arithmetic here can hold";
            result.status = DerivationStatus::ResourceLimitReached;
            return result;
        }
        Case positive;
        positive.root = root;
        Case negative;
        negative.root = negated_root;
        cases.push_back(positive);
        // A square of zero has one root rather than two written twice. Exclusivity is what would be
        // false about recording it twice, and completeness is what would be false about recording
        // only one root of a positive square, so the two arms are not interchangeable.
        if (root.num != 0)
            cases.push_back(negative);
        for (size_t i = 0; i < cases.size(); ++i) {
            cases[i].condition =
                arena.binary(Kind::Equals, unknown, rational_node(arena, cases[i].root));
            if (cases[i].condition == kNoNode || arena.failed()) {
                result.detail = "the arena could not hold a case";
                result.status = DerivationStatus::ResourceLimitReached;
                return result;
            }
            cases[i].substitution = satisfies_original(arena, equation, name, cases[i].root,
                                                       &cases[i].substitution_detail);
        }
    }

    // A substitution that ran out of exact arithmetic checked nothing, and VER-017's soundness half
    // has no slot for a case recorded as solved without a passing candidate check. So this refuses
    // before any case is recorded, for the reason the branch-budget halt below gives: a split cut
    // short still carries siblings_exhaustive on every case it did record.
    for (size_t i = 0; i < cases.size(); ++i) {
        if (cases[i].substitution == Substitution::OutOfRoom) {
            result.outcome = QuadraticOutcome::ResourceExceeded;
            result.detail = "checking a case against the equation as it was typed ran out of exact "
                            "arithmetic, so no answer is offered";
            result.status = DerivationStatus::ResourceLimitReached;
            return result;
        }
    }

    // Exclusivity read off the cases rather than asserted about them: no two of them are the same
    // value, so no value of the unknown satisfies two. A single case has nothing to overlap with.
    bool exclusive = true;
    for (size_t i = 0; i < cases.size(); ++i) {
        for (size_t j = i + 1; j < cases.size(); ++j)
            exclusive = exclusive && !rational_equal(cases[i].root, cases[j].root);
    }

    std::vector<StepId> case_ids;
    for (size_t i = 0; i < cases.size(); ++i) {
        Step s;
        s.phase = "solve";
        s.goal = cases[i].root.num == 0
                     ? "Take the square root of zero"
                     : "Take the " + std::string(cases[i].root.num < 0 ? "negative" : "positive") +
                           " square root";
        s.rule_id = "eq.quadratic.square-root-case";
        s.rule_name = "Square root case";
        s.claim = ClaimType::SolutionSetNarrowed;
        s.explanation_short = name + " = " + rational_text_for(arena, cases[i].root) +
                              (cases[i].root.num == 0 ? " is the only value whose square is "
                                                     : " is one of the values whose square is ") +
                              rational_text_for(arena, square);
        s.explanation_detailed =
            cases[i].root.num == 0
                ? "Only zero squares to zero. The positive and negative choices coincide, so write " +
                      name + " = 0 once."
                : "A positive number has two real square roots, one of each sign. Write both "
                  "cases to keep every solution.";
        s.proof_obligations.push_back({"obl.quadratic.case-is-a-root",
                                       "this case's value squares to the isolated value"});
        Rational squared;
        const bool squares_back = rational_mul(cases[i].root, cases[i].root, &squared) &&
                                  rational_equal(squared, square);
        s.verifications.push_back(
            squares_back ? passed("exact rational square", EvidenceStrength::StructurallyValid,
                                  rational_text_for(arena, cases[i].root) + " squared is " +
                                      rational_text_for(arena, square))
                         : failed("exact rational square",
                                  "this case's value does not square to the isolated value"));
        BranchPayload payload;
        payload.condition = cases[i].condition;
        payload.siblings_exhaustive = true;
        payload.siblings_exclusive = exclusive;
        // No restriction is in force anywhere in this rule: nothing was divided by an expression in
        // the unknown and nothing entered a domain a case could fall outside. So the cases agree
        // with the conditions the derivation carries by construction rather than by a check.
        payload.siblings_domain_consistent = true;
        payload.exhaustive_evidence = "root-coefficient reconstruction";
        payload.feasibility_status = "feasible";
        const bool satisfied = cases[i].substitution == Substitution::Satisfied;
        payload.resolution = satisfied ? BranchResolution::Solved : BranchResolution::Rejected;
        payload.resolution_evidence = "substitution into the original equation";
        if (!meter.step())
            return result;
        const StepId id = derivation.add_branch(meter, isolate_id, std::move(s), std::move(payload));
        if (id == kNoStep) {
            // PERF-008's limit landed in the middle of a split. Anything already recorded would read
            // as an exhaustive set that is not one, so the halt path below rewinds rather than
            // leaving a half split behind, and this returns without claiming anything.
            return result;
        }
        case_ids.push_back(id);

        if (!meter.step())
            return result;
        Step check;
        check.phase = "check";
        check.goal = "Check this case";
        check.rule_id = "eq.quadratic.check-by-substitution";
        check.rule_name = "Check by substitution";
        check.claim = ClaimType::SolutionSetPreserved;
        check.explanation_short = "Put " + rational_text_for(arena, cases[i].root) + " back into "
                                  "the equation as it was typed";
        check.explanation_detailed =
            "Substituting into the original rather than into the isolated form is what makes this a "
            "check: the isolated form came from the step being checked, so agreeing with it would "
            "prove nothing about the answer.";
        check.proof_obligations.push_back(
            {"obl.quadratic.candidate-satisfies", "the candidate satisfies the original equation"});
        check.verifications.push_back(
            satisfied ? passed("substitution", EvidenceStrength::CandidateChecked,
                               cases[i].substitution_detail)
                      : failed("substitution", cases[i].substitution_detail));
        CheckPayload check_payload;
        check_payload.target_claim = name + " = " + rational_text_for(arena, cases[i].root) +
                                     " satisfies the equation";
        check_payload.check_method = "substitute the case into the original equation";
        check_payload.expected_relation = "left side equals right side";
        check_payload.observed_result = cases[i].substitution_detail;
        derivation.add_check(id, std::move(check), std::move(check_payload));

        if (!satisfied) {
            result.outcome = QuadraticOutcome::Refused;
            result.detail = "a case failed its own substitution check, so no answer is offered";
            result.status = DerivationStatus::VerificationFailed;
            return result;
        }
    }

    if (negative_square) {
        if (!meter.step())
            return result;
        Step s;
        s.phase = "solve";
        s.goal = "Rule out a real root";
        s.rule_id = "eq.quadratic.reject-negative-square";
        s.rule_name = "Negative square has no real root";
        // Dropping a case nothing could satisfy leaves the solution set exactly as it was, so this
        // preserves rather than narrows. Narrowing would claim the rejection removed something.
        s.claim = ClaimType::SolutionSetPreserved;
        s.explanation_short = "No real number squares to " + rational_text_for(arena, square);
        s.explanation_detailed =
            "Reach for this whenever an isolated square comes out negative. Squaring a real number "
            "never gives a negative, so the case is rejected rather than solved, and the equation "
            "has no real solution rather than an unfinished one.";
        s.proof_obligations.push_back(
            {"obl.quadratic.rejected-case-is-infeasible",
             "no real value satisfies the condition this case stands for"});
        s.verifications.push_back(
            passed("sign of a real square", EvidenceStrength::StructurallyValid,
                   "the isolated square is " + rational_text_for(arena, square) +
                       ", and a real square is never negative"));
        BranchPayload payload;
        payload.condition = isolated;
        payload.siblings_exhaustive = true;
        payload.siblings_exclusive = true;
        payload.siblings_domain_consistent = true;
        payload.exhaustive_evidence = "sign of a real square";
        payload.feasibility_status = "infeasible over the reals";
        payload.resolution = BranchResolution::Rejected;
        payload.resolution_evidence = "no real number squares to a negative";
        const StepId id = derivation.add_branch(meter, isolate_id, std::move(s), std::move(payload));
        if (id == kNoStep)
            return result;
        case_ids.push_back(id);
    }

    // The closing check, and the one VER-017 calls completeness. It reads the cases back out of the
    // records rather than off the vector above, because what has to be complete is the split a
    // reader can see rather than the one the rule believed it made.
    if (!meter.step())
        return result;
    std::vector<Rational> recorded;
    bool readable = true;
    if (!negative_square) {
        for (size_t i = 0; i < case_ids.size(); ++i) {
            const BranchPayload *p = derivation.branch(case_ids[i]);
            // Read back through the same analysis that reduced the problem, rather than through a
            // reader written to match how the case was built. A reader that mirrored the builder
            // would agree with it however wrong the pair were together.
            Rational case_coefficient;
            Rational case_constant;
            Rational value;
            if (p == nullptr || p->condition == kNoNode ||
                linear_form(arena, p->condition, unknown, meter, &case_coefficient,
                            &case_constant) != LinearForm::Reduced ||
                !rational_sub(Rational{0, 1}, case_constant, &value) ||
                !rational_div(value, case_coefficient, &value)) {
                readable = false;
                break;
            }
            recorded.push_back(value);
        }
    }
    if (meter.stopped())
        return result;

    std::string why;
    // The meter was checked above, so a read-back that could not be made is arithmetic running out
    // rather than a case that is missing, which is the same distinction the predicate below draws.
    if (!readable)
        why = "a recorded case could not be read back as an exact value";
    const Reconstruction rebuilt =
        negative_square  ? Reconstruction::Rebuilt
        : !readable      ? Reconstruction::OutOfRoom
                         : cases_reconstruct_the_square(recorded, square, &why);
    const bool complete = rebuilt == Reconstruction::Rebuilt;

    Step closing;
    closing.phase = "check";
    closing.goal = "Check that no case is missing";
    closing.rule_id = "eq.quadratic.cases-reconstruct-the-original";
    closing.rule_name = "Completeness of the split";
    closing.claim = ClaimType::SolutionSetPreserved;
    closing.explanation_short = negative_square
                                    ? "A real square is never negative, so there is no case to miss"
                                    : "The cases multiply back out to the equation that was split";
    closing.explanation_detailed =
        "Reach for this at the end of any split. Checking each case on its own says the answers "
        "given are right, and says nothing about an answer left out, so the cases are multiplied "
        "back together and compared with the equation they came from.";
    closing.proof_obligations.push_back(
        {"obl.quadratic.cases-are-complete", "every real value satisfying the equation is one of "
                                             "the cases recorded"});
    if (negative_square) {
        closing.verifications.push_back(
            passed("sign of a real square", EvidenceStrength::StructurallyValid,
                   "the isolated square is negative, so the empty set of roots is complete"));
    } else {
        closing.verifications.push_back(
            complete ? passed("root-coefficient reconstruction",
                              EvidenceStrength::SymbolicallyEquivalentUnderAssumptions,
                              "the " + integer_text(static_cast<int64_t>(recorded.size())) +
                                  (recorded.size() == 1 ? " recorded case rebuilds "
                                                        : " recorded cases rebuild ") +
                                  name + "^2 = " + rational_text_for(arena, square))
            : rebuilt == Reconstruction::OutOfRoom
                     ? inconclusive("root-coefficient reconstruction", why)
                     : failed("root-coefficient reconstruction", why));
    }
    CheckPayload closing_payload;
    closing_payload.target_claim = "the cases recorded are every real solution of the equation";
    closing_payload.check_method =
        negative_square ? "inspect the sign of the isolated square"
                        : "rebuild the quadratic from the recorded cases and compare it";
    closing_payload.expected_relation = negative_square
                                            ? "the isolated square is negative"
                                            : "the rebuilt quadratic is the one that was split";
    closing_payload.observed_result =
        complete ? closing_payload.expected_relation : why;
    derivation.add_check(isolate_id, std::move(closing), std::move(closing_payload));

    if (rebuilt == Reconstruction::OutOfRoom) {
        result.outcome = QuadraticOutcome::ResourceExceeded;
        result.detail = "rebuilding the equation from its cases ran out of exact arithmetic, so no "
                        "answer is offered";
        result.status = DerivationStatus::ResourceLimitReached;
        return result;
    }
    if (!complete) {
        result.outcome = QuadraticOutcome::Refused;
        result.detail = "the split could not be shown to be complete, so no answer is offered";
        result.status = DerivationStatus::VerificationFailed;
        return result;
    }

    if (negative_square) {
        result.outcome = QuadraticOutcome::NoRealSolution;
        result.detail = "the square of " + name + " would have to be " +
                        rational_text_for(arena, square) + ", and no real square is negative";
        return result;
    }

    result.outcome = QuadraticOutcome::Solved;
    for (size_t i = 0; i < cases.size(); ++i)
        result.solutions.push_back(rational_node(arena, cases[i].root));
    return result;
}

}  // namespace

QuadraticResult solve_by_square_root(Arena &arena, Derivation &derivation, NodeId equation,
                                     NodeId unknown, const Budget &budget) {
    Meter meter(budget);
    const size_t mark = derivation.mark();
    const NumericMode mode = derivation.request.numeric_mode;

    QuadraticResult result = solve_body(arena, derivation, equation, unknown, meter);

    // A halt inside a split cannot keep the checked prefix the way a linear solve does. The prefix
    // of a split is some of its cases, and a partial set of cases still carries siblings_exhaustive,
    // which is the one thing a reader must not be shown when it is false. So a halt anywhere in this
    // rule drops everything rather than keeping what was checked.
    if (meter.stopped()) {
        const bool cancelled = meter.halt() == Halt::Cancelled;
        derivation.rewind_to(mark);
        QuadraticResult halted;
        halted.outcome = cancelled ? QuadraticOutcome::Cancelled
                                   : QuadraticOutcome::ResourceExceeded;
        halted.detail = halt_name(meter.halt());
        halted.status = cancelled ? DerivationStatus::NotRecorded
                                  : DerivationStatus::ResourceLimitReached;
        halted.cost = meter.cost();
        record_context(derivation, budget, equation, halted.status, mode);
        return halted;
    }

    if (result.outcome == QuadraticOutcome::Solved ||
        result.outcome == QuadraticOutcome::NoRealSolution)
        result.status = derivation.outcome_from(mark);
    result.cost = meter.cost();
    record_context(derivation, budget, equation, result.status, mode);
    return result;
}

}  // namespace nps
