#include "nps/steps/linear.h"

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/print.h"
#include "nps/core/rational.h"
#include "nps/steps/numeric_mode.h"

namespace nps {
namespace {

// A linear expression in one unknown, as coefficient and constant with both kept exact. Rational
// arithmetic is done as a pair of int64 rather than through Giac, because the whole point of these
// steps is that the engine owns the reasoning and only delegates what it cannot do itself.
struct Linear {
    int64_t coeff_num = 0;
    int64_t coeff_den = 1;
    int64_t const_num = 0;
    int64_t const_den = 1;
    Precision coeff_precision;
    Precision const_precision;
    bool valid = false;
    // Set when exact arithmetic ran out rather than when the expression was the wrong shape. Both
    // stop the analysis and they are different refusals to report.
    bool overflowed = false;
};

Linear invalid() { return Linear(); }

Linear overflow() {
    Linear l;
    l.overflowed = true;
    return l;
}

Linear constant(int64_t n, int64_t d, const Precision &precision = Precision()) {
    Linear l;
    l.const_num = n;
    l.const_den = d;
    l.const_precision = precision;
    l.valid = normalise(&l.const_num, &l.const_den);
    l.overflowed = !l.valid;
    return l;
}

Coroutine<Linear> analyse(TaskContext &task, const Arena &arena, NodeId id, NodeId unknown,
                          Meter &meter, const std::vector<LinearKnown> *knowns) {
    co_await task.checkpoint();
    if (!meter.rewrite())
        co_return invalid();
    if (id == unknown) {
        Linear l;
        l.coeff_num = 1;
        l.valid = true;
        co_return l;
    }
    const Node &n = arena.at(id);
    switch (n.kind) {
        case Kind::Integer:
            co_return n.small_valid ? constant(n.small, 1) : overflow();
        case Kind::Decimal: {
            Quantity quantity;
            std::string why;
            if (!parse_quantity(arena.text(id), &quantity, &why))
                co_return invalid();
            co_return constant(quantity.value.num, quantity.value.den, quantity.precision);
        }
        case Kind::Symbol:
            if (knowns) {
                for (const LinearKnown &known : *knowns) {
                    co_await task.checkpoint();
                    if (known.symbol == arena.text(id))
                        co_return constant(known.value.num, known.value.den, known.precision);
                }
            }
            co_return invalid();
        case Kind::Add: {
            Linear acc = constant(0, 1);
            for (NodeId child : arena.children(n)) {
                Linear part = co_await analyse(task, arena, child, unknown, meter, knowns);
                co_await task.checkpoint();
                if (!part.valid)
                    co_return part;
                Linear next;
                if (!add_fraction(acc.coeff_num, acc.coeff_den, part.coeff_num, part.coeff_den,
                                  &next.coeff_num, &next.coeff_den) ||
                    !add_fraction(acc.const_num, acc.const_den, part.const_num, part.const_den,
                                  &next.const_num, &next.const_den))
                    co_return overflow();
                next.coeff_precision = precision_sum(Rational{next.coeff_num, next.coeff_den},
                                                     acc.coeff_precision, part.coeff_precision);
                next.const_precision = precision_sum(Rational{next.const_num, next.const_den},
                                                     acc.const_precision, part.const_precision);
                next.valid = true;
                acc = next;
            }
            co_return acc;
        }
        case Kind::Mul: {
            Linear scale = constant(1, 1);
            Linear carrier = constant(0, 1);
            bool have_carrier = false;
            for (NodeId child : arena.children(n)) {
                Linear part = co_await analyse(task, arena, child, unknown, meter, knowns);
                co_await task.checkpoint();
                if (!part.valid)
                    co_return part;
                if (part.coeff_num == 0) {
                    Linear next;
                    if (!mul_fraction(scale.const_num, scale.const_den, part.const_num, part.const_den,
                                      &next.const_num, &next.const_den))
                        co_return overflow();
                    next.const_precision = precision_product(
                        Rational{next.const_num, next.const_den},
                        Rational{scale.const_num, scale.const_den}, scale.const_precision,
                        Rational{part.const_num, part.const_den}, part.const_precision);
                    next.valid = true;
                    scale = next;
                } else {
                    if (have_carrier)
                        co_return invalid();
                    carrier = part;
                    have_carrier = true;
                }
            }
            if (!have_carrier)
                co_return scale;
            Linear out;
            if (!mul_fraction(carrier.coeff_num, carrier.coeff_den, scale.const_num, scale.const_den,
                              &out.coeff_num, &out.coeff_den) ||
                !mul_fraction(carrier.const_num, carrier.const_den, scale.const_num, scale.const_den,
                              &out.const_num, &out.const_den))
                co_return overflow();
            out.coeff_precision = precision_product(
                Rational{out.coeff_num, out.coeff_den}, Rational{carrier.coeff_num, carrier.coeff_den},
                carrier.coeff_precision, Rational{scale.const_num, scale.const_den}, scale.const_precision);
            out.const_precision = precision_product(
                Rational{out.const_num, out.const_den}, Rational{carrier.const_num, carrier.const_den},
                carrier.const_precision, Rational{scale.const_num, scale.const_den}, scale.const_precision);
            out.valid = true;
            co_return out;
        }
        case Kind::Neg: {
            Linear inner = co_await analyse(task, arena, arena.children(n)[0], unknown, meter, knowns);
            co_await task.checkpoint();
            if (!inner.valid)
                co_return inner;
            Linear out;
            if (!mul_fraction(inner.coeff_num, inner.coeff_den, -1, 1, &out.coeff_num, &out.coeff_den) ||
                !mul_fraction(inner.const_num, inner.const_den, -1, 1, &out.const_num, &out.const_den))
                co_return overflow();
            out.coeff_precision = precision_product(
                Rational{out.coeff_num, out.coeff_den}, Rational{inner.coeff_num, inner.coeff_den},
                inner.coeff_precision, Rational{-1, 1}, Precision());
            out.const_precision = precision_product(
                Rational{out.const_num, out.const_den}, Rational{inner.const_num, inner.const_den},
                inner.const_precision, Rational{-1, 1}, Precision());
            out.valid = true;
            co_return out;
        }
        case Kind::Pow: {
            int64_t exponent;
            if (!small_integer(arena, arena.children(n)[1], &exponent))
                co_return invalid();
            Linear base = co_await analyse(task, arena, arena.children(n)[0], unknown, meter, knowns);
            co_await task.checkpoint();
            if (exponent == 1 || !base.valid)
                co_return base;
            if (base.coeff_num != 0)
                co_return invalid();
            const Rational base_value{base.const_num, base.const_den};
            if (base_value.num == 0 && exponent < 0)
                co_return invalid();
            Rational powered;
            if (!rational_power(base_value, exponent, &powered))
                co_return overflow();
            const Precision precision = exponent == 0 ? Precision() :
                precision_product(powered, base_value, base.const_precision, Rational{1, 1}, Precision());
            co_return constant(powered.num, powered.den, precision);
        }
        default:
            co_return invalid();
    }
}

Linear analyse(const Arena &arena, NodeId id, NodeId unknown, Meter &meter,
               const std::vector<LinearKnown> *knowns) {
    std::vector<std::byte> storage(262144);
    TaskContext context(storage);
    auto task = make_task(context, [](TaskContext &owner, const Arena &a, NodeId node,
                                     NodeId variable, Meter &cost,
                                     const std::vector<LinearKnown> *values) {
        return analyse(owner, a, node, variable, cost, values);
    }, arena, id, unknown, meter, knowns);
    while (task.state() == TaskState::Pending)
        task.advance(1024);
    return task.result() ? *task.result() : overflow();
}

NodeId fraction_node(Arena &arena, int64_t num, int64_t den) {
    NodeId n = arena.integer(integer_text(num));
    if (den == 1)
        return n;
    NodeId d = arena.integer(integer_text(den));
    NodeId inverse = arena.binary(Kind::Pow, d, arena.unary(Kind::Neg, arena.integer("1")));
    return arena.binary(Kind::Mul, n, inverse);
}

Step make_step(const std::string &goal, const char *rule_id, const char *rule_name,
               const char *why) {
    Step s;
    s.phase = "solve";
    s.goal = goal;
    s.rule_id = rule_id;
    s.rule_name = rule_name;
    s.explanation_short = why;
    s.claim = ClaimType::SolutionSetPreserved;
    return s;
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

}  // namespace

const char *solve_outcome_name(SolveOutcome o) {
    switch (o) {
        case SolveOutcome::Solved: return "solved";
        case SolveOutcome::NoSolution: return "no solution";
        case SolveOutcome::AllValues: return "true for every value";
        case SolveOutcome::NotLinear: return "not linear in the unknown";
        case SolveOutcome::NotAnEquation: return "not an equation";
        case SolveOutcome::Refused: return "refused";
        case SolveOutcome::Cancelled: return "cancelled";
        case SolveOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

namespace {

// The body of a solve. The wrapper below owns the halt handling, so every return in here is an
// ordinary outcome and none of them has to remember to roll anything back.
Coroutine<SolveResult> solve_body(TaskContext &task, Arena &arena, Derivation &derivation,
                                  NodeId equation, NodeId unknown, Meter &meter) {
    co_await task.checkpoint();
    SolveResult result;
    if (equation == kNoNode || unknown == kNoNode || arena.failed()) {
        result.detail = "nothing to solve";
        result.status = DerivationStatus::InvalidInput;
        co_return result;
    }
    if (arena.at(equation).kind != Kind::Equals) {
        result.outcome = SolveOutcome::NotAnEquation;
        result.detail = "this rule solves an equation, and that is not one";
        result.status = DerivationStatus::InvalidInput;
        co_return result;
    }
    if (divides_by_zero(arena, equation)) {
        result.outcome = SolveOutcome::Refused;
        result.detail = "the equation divides by zero, which has no value to solve for";
        result.status = DerivationStatus::InvalidInput;
        co_return result;
    }
    if (has_unmeetable_condition(arena, equation)) {
        result.outcome = SolveOutcome::Refused;
        result.detail = "the equation is undefined here, so there is nothing to solve";
        result.status = DerivationStatus::InvalidInput;
        co_return result;
    }
    if (arena.at(unknown).kind != Kind::Symbol) {
        result.detail = "the unknown has to be a symbol";
        result.status = DerivationStatus::InvalidInput;
        co_return result;
    }

    Linear left = co_await analyse(task, arena, arena.children(equation)[0], unknown, meter, nullptr);
    Linear right = co_await analyse(task, arena, arena.children(equation)[1], unknown, meter, nullptr);
    co_await task.checkpoint();
    if (meter.stopped())
        co_return result;
    if (!left.valid || !right.valid) {
        const bool ran_out = left.overflowed || right.overflowed;
        result.outcome = ran_out ? SolveOutcome::ResourceExceeded : SolveOutcome::NotLinear;
        result.detail = ran_out ? "a value grew past what exact integer arithmetic here can hold"
                                : "this equation is not linear in " + arena.text(unknown);
        result.status = ran_out ? DerivationStatus::ResourceLimitReached
                                : DerivationStatus::Unsupported;
        co_return result;
    }

    // a*x + b = 0 after moving everything to the left.
    int64_t an, ad, bn, bd;
    if (!sub_fraction(left.coeff_num, left.coeff_den, right.coeff_num, right.coeff_den, &an, &ad) ||
        !sub_fraction(left.const_num, left.const_den, right.const_num, right.const_den, &bn, &bd)) {
        result.outcome = SolveOutcome::ResourceExceeded;
        result.detail = "the coefficients grew past what exact integer arithmetic here can hold";
        result.status = DerivationStatus::ResourceLimitReached;
        co_return result;
    }

    const std::string name = arena.text(unknown);

    PlanPayload plan;
    plan.strategy_id = "eq.linear.inverse-operations";
    plan.selected_strategy = "Inverse operations on a linear equation";
    plan.matched_problem_facts.push_back("one unknown, " + name);
    plan.selection_rationale =
        "collect the terms, remove the constant and isolate " + name +
        " using its non-zero coefficient, or inspect the remaining constant if the coefficient is zero";
    Step plan_step;
    plan_step.phase = "plan";
    plan_step.goal = "Isolate " + name;
    plan_step.rule_id = plan.strategy_id;
    plan_step.rule_name = "Inverse operations on a linear equation";
    plan_step.claim = ClaimType::NoClaim;
    plan_step.explanation_short = "Solve by undoing what was done to " + name;
    register_strategy_precondition(
        plan, plan_step, "pre.linear.degree-one", "the equation is degree one in " + name,
        "exact linear analysis", EvidenceStrength::StructurallyValid,
        VerificationOutcome::Passed, "both sides reduced to a coefficient and constant term");
    register_strategy_precondition(
        plan, plan_step, "pre.linear.rational-constants",
        "every other quantity is a rational constant", "exact linear analysis",
        EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
        "all coefficients and constants are exact rationals");
    co_await task.checkpoint();
    if (!meter.step())
        co_return result;
    StepId plan_id = derivation.add_plan(kNoStep, std::move(plan_step), std::move(plan));

    NodeId collected = arena.binary(
        Kind::Equals,
        arena.binary(Kind::Add, arena.binary(Kind::Mul, fraction_node(arena, an, ad), unknown),
                     fraction_node(arena, bn, bd)),
        arena.integer("0"));

    co_await task.checkpoint();
    if (!meter.step())
        co_return result;
    {
        Step s = make_step("Collect the terms in " + name, "eq.collect-like-terms",
                           "Collect like terms",
                           "Moving every term to one side keeps both sides equal");
        s.explanation_detailed =
            "Reach for this whenever the unknown appears more than once, on either side or both. "
            "Adding or subtracting the same quantity from both sides leaves the equation with the "
            "same solutions, so the terms in the unknown can all be gathered on one side and the "
            "plain numbers on the other without changing the answer.";
        s.verifications.push_back(passed(
            "rule-local equality invariant", EvidenceStrength::StructurallyValid,
            "the same quantity was added to both sides, which cannot change the solution set"));
        s.proof_obligations.push_back(
            {"obl.eq.same-solutions", "the rewritten equation has the solutions the original had"});
        TransformationPayload p;
        p.before = equation;
        p.after = collected;
        p.concrete_action = "Move every term to the left side";
        p.reversible = true;
        derivation.add_transformation(plan_id, std::move(s), std::move(p));
    }

    if (an == 0) {
        co_await task.checkpoint();
        if (!meter.step())
            co_return result;
        Step s;
        s.phase = "solve";
        s.goal = "Read off the outcome";
        s.rule_id = "eq.linear.inspect-collected-coefficient";
        s.rule_name = "Coefficient inspection";
        s.explanation_detailed =
            "Once every term in the unknown is collected on one side, the coefficient of the "
            "unknown decides the shape of the answer: if it is zero the unknown has cancelled, and "
            "then the constant term says whether what is left is true for every value or for none.";
        s.claim = ClaimType::SolutionSetPreserved;
        s.verifications.push_back(passed("inspection of the collected coefficient",
                                         EvidenceStrength::StructurallyValid,
                                         "the coefficient of the unknown is zero, which is what "
                                         "decides between no solution and every value"));
        s.proof_obligations.push_back({"obl.linear.coefficient-decides",
                                       "the collected coefficient of the unknown is what decides "
                                       "between no solution and every value"});
        CheckPayload c;
        c.target_claim = "the equation has no term in " + name;
        c.check_method = "inspect the collected coefficient";
        c.expected_relation = "coefficient of " + name + " is zero";
        if (bn == 0) {
            result.outcome = SolveOutcome::AllValues;
            result.detail = "both sides are the same expression, so every value of " + name +
                            " satisfies it";
            s.explanation_short = "Every value of " + name + " satisfies this equation";
            c.observed_result = "constant term is zero as well";
        } else {
            result.outcome = SolveOutcome::NoSolution;
            result.detail = "the unknown cancels and leaves a false statement, so nothing satisfies it";
            s.explanation_short = "The unknown cancels and what is left is false";
            c.observed_result = "constant term is not zero";
        }
        derivation.add_check(plan_id, std::move(s), std::move(c));
        // Both readings are answers rather than failures: the check step records how each was
        // reached, so the outcome is as settled as a solved one.
        result.status = DerivationStatus::SolvedAndVerified;
        co_return result;
    }

    // x = -b/a, kept exact.
    int64_t nbn, nbd, sn, sd;
    if (!negate_fraction(bn, bd, &nbn, &nbd) || !mul_fraction(nbn, nbd, ad, an, &sn, &sd)) {
        result.outcome = SolveOutcome::ResourceExceeded;
        result.detail = "the solution did not fit in exact integer arithmetic here";
        result.status = DerivationStatus::ResourceLimitReached;
        co_return result;
    }

    NodeId solution = fraction_node(arena, sn, sd);
    NodeId solved = arena.binary(Kind::Equals, unknown, solution);

    co_await task.checkpoint();
    if (!meter.step())
        co_return result;
    {
        const std::string coefficient = rational_text(Rational{an, ad});
        std::string action;
        if (bn < 0)
            action = "Add " + rational_text(Rational{nbn, nbd}) + " to both sides";
        else if (bn > 0)
            action = "Subtract " + rational_text(Rational{bn, bd}) + " from both sides";
        if (an != ad)
            action += (action.empty() ? "Divide" : ", then divide") +
                      std::string(" both sides by ") + coefficient;
        if (action.empty())
            action = "Read off " + name + " = 0";
        Step s = make_step("Isolate " + name, "eq.divide-both-sides", "Isolate the unknown",
                           "Applying the same operation to both sides preserves the solutions");
        s.goal = "Isolate " + name;
        if (bn != 0)
            s.explanation_detailed = "Remove the constant term by applying its opposite to both sides. ";
        if (an != ad)
            s.explanation_detailed += "The coefficient of " + name + " is " + coefficient +
                                      ", which is non-zero, so dividing by it is reversible.";
        else
            s.explanation_detailed += "The coefficient of " + name + " is 1, so no division is needed.";
        s.verifications.push_back(
            passed("rule-local equality invariant", EvidenceStrength::StructurallyValid,
                   "removing the constant preserves equality, and the coefficient " + coefficient +
                   " was checked to be non-zero"));
        s.proof_obligations.push_back(
            {"obl.eq.same-solutions", "the rewritten equation has the solutions the original had"});
        TransformationPayload p;
        p.before = collected;
        p.after = solved;
        p.concrete_action = action;
        p.reversible = true;
        derivation.add_transformation(plan_id, std::move(s), std::move(p));
    }

    co_await task.checkpoint();
    if (!meter.step())
        co_return result;
    {
        // Substituting the answer back is the check a reader can follow, and it is done here rather
        // than delegated, so the derivation does not rest on the backend agreeing with itself.
        // Walked over the equation as it was typed rather than over the collected coefficients.
        // Reading a and b back would compute a*(-b/a) + b, which is zero whatever a and b are, so
        // the check would pass a miscollection and in fact could never fail at all.
        const std::vector<SymbolValue> values{{name, Rational{sn, sd}}};
        const ChildView sides = arena.children(arena.at(equation));
        Rational lhs_value;
        Rational rhs_value;
        const bool ok = evaluate_rational(arena, sides[0], values, &lhs_value) &&
                        evaluate_rational(arena, sides[1], values, &rhs_value);
        const bool substitutes = ok && rational_equal(lhs_value, rhs_value);
        Step s;
        s.phase = "check";
        s.goal = "Check the answer";
        s.rule_id = "eq.linear.check-by-substitution";
        s.rule_name = "Check by substitution";
        s.explanation_detailed =
            "Put the candidate back where the unknown was in the equation as it was typed and work "
            "both sides out. A solution makes them equal, so anything else means the candidate is "
            "not one and the answer is withheld rather than reported.";
        // The check asserts something, so it is not NoClaim: a step with no claim counts as verified
        // without a record, and a failed substitution recorded that way reads as a checked solution
        // through verified() while only the prose says otherwise. Criterion 11 is exactly that.
        s.claim = ClaimType::SolutionSetPreserved;
        s.explanation_short = "Put the answer back into the original equation";
        s.proof_obligations.push_back(
            {"obl.linear.candidate-satisfies", "the candidate satisfies the original equation"});
        s.verifications.push_back(
            substitutes ? passed("substitution", EvidenceStrength::CandidateChecked,
                                 "both sides came out equal")
                        : failed("substitution", ok ? "the two sides did not come out equal"
                                                    : "the substitution ran out of exact arithmetic"));
        CheckPayload c;
        c.target_claim = name + " = " + print(arena, solution) + " satisfies the equation";
        c.check_method = "substitute the solution into the equation as it was typed";
        c.expected_relation = "both sides equal";
        c.observed_result = substitutes ? "both sides are equal"
                                        : "substitution did not make the sides equal";
        derivation.add_check(plan_id, std::move(s), std::move(c));
        if (!substitutes) {
            // A check that could not be computed is not a check that failed, and reporting the
            // first as the second would call an unknown a disagreement.
            result.outcome = ok ? SolveOutcome::Refused : SolveOutcome::ResourceExceeded;
            result.detail = ok
                                ? "the answer failed its own substitution check, so it is not offered"
                                : "checking the answer exceeded exact arithmetic, so it is not offered";
            result.status =
                ok ? DerivationStatus::VerificationFailed : DerivationStatus::ResourceLimitReached;
            co_return result;
        }
    }

    // A solution the arena never built is not a solution. Reported as solved it reached the bridge
    // as a result with an empty answer, which is the state the fuzzer's rewrite-result check names.
    if (solution == kNoNode || arena.failed()) {
        result.outcome = SolveOutcome::ResourceExceeded;
        result.status = DerivationStatus::ResourceLimitReached;
        result.detail = "the working space ran out before the solution was built";
        co_return result;
    }

    result.outcome = SolveOutcome::Solved;
    result.solution = solution;
    co_return result;
}

}  // namespace

bool linear_solution_precision(const Arena &arena, NodeId equation, NodeId unknown,
                               const std::vector<LinearKnown> &knowns, Rational *value,
                               Precision *precision) {
    if (arena.failed() || equation >= arena.node_count() || unknown >= arena.node_count() ||
        arena.at(equation).kind != Kind::Equals || arena.at(unknown).kind != Kind::Symbol ||
        contains_list(arena, equation)) {
        return false;
    }
    Budget budget;
    Meter meter(budget);
    const ChildView sides = arena.children(equation);
    if (sides.size() != 2)
        return false;
    Linear left = analyse(arena, sides[0], unknown, meter, &knowns);
    Linear right = analyse(arena, sides[1], unknown, meter, &knowns);
    if (!left.valid || !right.valid || meter.stopped())
        return false;

    Rational coefficient;
    Rational constant_value;
    if (!sub_fraction(left.coeff_num, left.coeff_den, right.coeff_num, right.coeff_den,
                      &coefficient.num, &coefficient.den) ||
        !sub_fraction(left.const_num, left.const_den, right.const_num, right.const_den,
                      &constant_value.num, &constant_value.den) ||
        coefficient.num == 0) {
        return false;
    }
    const Precision coefficient_precision =
        precision_sum(coefficient, left.coeff_precision, right.coeff_precision);
    const Precision constant_precision =
        precision_sum(constant_value, left.const_precision, right.const_precision);
    Rational negative_constant;
    if (!negate_fraction(constant_value.num, constant_value.den, &negative_constant.num,
                         &negative_constant.den) ||
        !rational_div(negative_constant, coefficient, value)) {
        return false;
    }
    *precision = precision_product(*value, negative_constant, constant_precision, coefficient,
                                   coefficient_precision);
    return true;
}

LinearForm linear_form(const Arena &arena, NodeId equation, NodeId subject, Meter &meter,
                       Rational *coefficient, Rational *constant) {
    if (arena.failed() || equation >= arena.node_count() || subject >= arena.node_count() ||
        arena.at(equation).kind != Kind::Equals) {
        return LinearForm::NotAnEquation;
    }
    if (contains_list(arena, equation) || contains_list(arena, subject))
        return LinearForm::NotLinear;
    const ChildView sides = arena.children(equation);
    if (sides.size() != 2)
        return LinearForm::NotAnEquation;

    Linear left = analyse(arena, sides[0], subject, meter, nullptr);
    Linear right = analyse(arena, sides[1], subject, meter, nullptr);
    if (meter.stopped())
        return LinearForm::Halted;
    if (!left.valid || !right.valid)
        return left.overflowed || right.overflowed ? LinearForm::Overflowed : LinearForm::NotLinear;

    if (!sub_fraction(left.coeff_num, left.coeff_den, right.coeff_num, right.coeff_den,
                      &coefficient->num, &coefficient->den) ||
        !sub_fraction(left.const_num, left.const_den, right.const_num, right.const_den,
                      &constant->num, &constant->den)) {
        return LinearForm::Overflowed;
    }
    // A zero coefficient means the subject cancelled, so the equation was never in it at all. That
    // is the wrong shape rather than a degenerate answer, because the caller asked to reduce in this
    // subject and there is nothing here to reduce.
    if (coefficient->num == 0)
        return LinearForm::NotLinear;
    return LinearForm::Reduced;
}

namespace {

// The key a remembered run is filed under, so another engine's run on the same equation is not this
// one's.
const char kLinearEngine[] = "algebra.linear-equation.one-unknown";

void record_context(Derivation &derivation, const Budget &budget, NodeId model,
                    DerivationStatus status, NumericMode mode) {
    ContextInputs inputs;
    inputs.application_version = application_version();
    inputs.problem_family_id = "algebra.linear-equation.one-unknown";
    inputs.requested_method = "inverse operations";
    inputs.normalized_problem_model = model;
    inputs.original_expression = derivation.request.original_expression;
    inputs.angle_convention = angle_mode_name(derivation.request.angle_mode);
    inputs.branch_convention = "real domain";
    inputs.detail_projection = "standard";
    inputs.resource_policy = budget_policy(budget);
    inputs.numeric_mode = mode;
    inputs.derivation_status = status;
    derivation.context = make_context(inputs);
}

}  // namespace

Coroutine<SolveResult> solve_linear_steps(TaskContext &task, Arena &arena, Derivation &derivation,
                                        NodeId equation, NodeId unknown, Meter &meter, Budget budget) {
    co_await task.checkpoint();
    const size_t mark = derivation.mark();
    // Read on entry, so the mode a solve ran under is the one its context records.
    const NumericMode mode = derivation.request.numeric_mode;

    // The same equation in the same unknown was solved into this derivation before, or into one
    // sharing its memory, so its checked run is appended instead of derived again. The mode
    // is not part of the key: the run ends before the reporting step below, which is where the mode
    // acts, and a decimal in the equation is read exactly inside the run whatever the mode.
    SolveResult result;
    auto finish = [&]() {
        if (arena.failed()) {
            result.outcome = SolveOutcome::ResourceExceeded;
            result.solution = kNoNode;
            result.status = DerivationStatus::ResourceLimitReached;
            result.detail = status_name(arena.status());
        } else if (meter.stopped()) {
            const bool cancelled = meter.halt() == Halt::Cancelled;
            const bool kept = keep_verified_prefix(derivation, mark, arena);
            result.outcome = cancelled ? SolveOutcome::Cancelled : SolveOutcome::ResourceExceeded;
            result.solution = kNoNode;
            result.detail = halt_name(meter.halt());
            result.status = !cancelled ? DerivationStatus::ResourceLimitReached
                            : kept     ? DerivationStatus::Cancelled
                                       : DerivationStatus::NotRecorded;
        }
        result.cost = meter.cost();
        record_context(derivation, budget, equation, result.status, mode);
        return result;
    };
    if (!arena.failed() && (contains_list(arena, equation) || contains_list(arena, unknown))) {
        result.outcome = SolveOutcome::NotLinear;
        result.detail = "list and matrix equations are not supported";
        result.status = DerivationStatus::Unsupported;
        co_return finish();
    }
    if (co_await Derivation::recall_steps(task, derivation, kLinearEngine, arena, equation, unknown, meter,
                                         &result.solution)) {
        result.outcome = SolveOutcome::Solved;
    } else if (!meter.stopped()) {
        // The rules below work in exact rationals whatever the mode says, so a typed decimal has to
        // be read as one before they see it. Recording that reading is the condition on
        // reinterpreting what the user typed: without the step, solving 0.5x = 1 to 2 is the silent
        // rewrite PRD 13.1 warns about rather than a line anybody can check.
        NodeId subject = equation;
        if (has_decimal(arena, equation)) {
            const char *why = nullptr;
            ModeStep promoted = ModeStep::NothingToDo;
            if (!meter.step())
                why = halt_name(meter.halt());
            else {
                promoted =
                    read_decimals_exactly(arena, derivation, kNoStep, "solve", equation, &subject);
                why = promotion_refusal(promoted);
            }
            if (why != nullptr) {
                derivation.rewind_to(mark);
                // Called unsupported input, a capacity refusal was waved past the backend gate.
                const bool exhausted = promotion_exhausted(promoted);
                result.outcome = exhausted ? SolveOutcome::ResourceExceeded : SolveOutcome::NotLinear;
                result.detail = why;
                result.status = exhausted ? DerivationStatus::ResourceLimitReached
                                          : DerivationStatus::Unsupported;
                co_return finish();
            }
        }

        result = co_await solve_body(task, arena, derivation, subject, unknown, meter);
        if (result.outcome == SolveOutcome::Solved && !meter.stopped())
            derivation.remember(kLinearEngine, arena, equation, unknown, mark, result.solution);
    }

    co_await task.checkpoint();
    if (!arena.failed() && meter.checkpoint() && result.outcome == SolveOutcome::Solved) {
        result.status = derivation.outcome_from(mark);
        if (mode == NumericMode::Decimal &&
            result.status == DerivationStatus::SolvedAndVerified && meter.step()) {
            NodeId reported = kNoNode;
            if (report_in_decimals(arena, derivation, kNoStep, "solve", result.solution,
                                   &reported) == ModeStep::Recorded)
                result.solution = reported;
        }
    }
    co_return finish();
}

SolveResult solve_linear(Arena &arena, Derivation &derivation, NodeId equation, NodeId unknown,
                         const Budget &budget) {
    return solve_linear(arena, derivation, equation, unknown, budget, 262144);
}

SolveResult solve_linear(Arena &arena, Derivation &derivation, NodeId equation, NodeId unknown,
                         const Budget &budget, size_t frame_bytes) {
    const size_t mark = derivation.mark();
    Meter meter(budget);
    std::vector<std::byte> storage(frame_bytes);
    TaskContext context(storage);
    auto task = make_task(context, solve_linear_steps, arena, derivation, equation, unknown, meter, budget);
    while (task.state() == TaskState::Pending)
        task.advance(1024);
    if (task.result())
        return *task.result();
    SolveResult refused;
    refused.outcome = SolveOutcome::ResourceExceeded;
    refused.status = DerivationStatus::ResourceLimitReached;
    refused.detail = "coroutine frame storage exhausted";
    refused.cost = meter.cost();
    keep_verified_prefix(derivation, mark, arena);
    record_context(derivation, budget, equation, refused.status, derivation.request.numeric_mode);
    return refused;
}

}  // namespace nps
