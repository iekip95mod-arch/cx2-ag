#include "nps/steps/integer.h"

#include <array>

#include "nps/core/context.h"
#include "nps/core/print.h"
#include "nps/core/rational.h"

namespace nps {
namespace {

enum class MethodKind { Quotient, Remainder, Factorial, Permutation, Combination, Prime, NextPrime,
                        ModularPower, Factorization, Gcd };

struct Method {
    const char *name;
    size_t arity;
    MethodKind kind;
    const char *rule;
    const char *obligation;
    const char *evidence;
    const char *strategy;
};

const Method kMethods[] = {
    {"iquo", 2, MethodKind::Quotient, "int.division", "obl.int.division-identity",
     "exact integer division identity", "Divide into equal groups and check the remainder"},
    {"irem", 2, MethodKind::Remainder, "int.division", "obl.int.division-identity",
     "exact integer division identity", "Divide into equal groups and retain what is left"},
    {"factorial", 1, MethodKind::Factorial, "int.factorial-product", "obl.int.factorial-product",
     "factorial recurrence", "Multiply the consecutive integers from 1 to n"},
    {"perm", 2, MethodKind::Permutation, "int.permutation-product", "obl.int.permutation-product",
     "falling product recurrence", "Multiply the number of choices for each ordered position"},
    {"comb", 2, MethodKind::Combination, "int.combination-product", "obl.int.combination-product",
     "binomial recurrence", "Build each binomial coefficient by an exact product and division"},
    {"is_prime", 1, MethodKind::Prime, "int.prime-conclusion", "obl.int.prime-decision",
     "complete trial division", "Test 2 and every odd divisor through the square root"},
    {"nextprime", 1, MethodKind::NextPrime, "int.next-prime", "obl.int.next-prime",
     "consecutive candidate exclusion", "Test consecutive larger integers until the first prime"},
    {"powmod", 3, MethodKind::ModularPower, "int.modular-power", "obl.int.modular-power",
     "binary modular recurrence", "Square and multiply while reducing every product modulo m"},
    {"ifactor", 1, MethodKind::Factorization, "int.factor-product", "obl.int.factor-product",
      "prime factor reconstruction", "Remove prime divisors in order and check their product"},
    {"gcd", 2, MethodKind::Gcd, "int.gcd-conclusion", "obl.int.gcd-certificate",
     "common divisibility and Bezout identity", "Reduce by Euclidean division and certify the greatest common divisor"},
};

const Method kTrial = {"trial division", 2, MethodKind::Prime, "int.trial-division",
                       "obl.int.trial-division", "exact integer division identity",
                        "Check the quotient and remainder for this candidate divisor"};

const Method kGcdSign = {"gcd", 2, MethodKind::Gcd, "int.gcd-sign", "obl.int.gcd-sign",
                        "integer magnitude normalization", "Replace signed integers by their magnitudes"};
const Method kGcdRemainder = {"gcd", 2, MethodKind::Gcd, "int.gcd-remainder", "obl.int.gcd-remainder",
                             "exact Euclidean reduction", "Replace the pair by divisor and remainder"};

const Method *method_named(std::string_view name) {
    for (const Method &method : kMethods) {
        if (name == method.name)
            return &method;
    }
    return nullptr;
}

bool proof_integer(const Arena &arena, NodeId id, mpz_ptr integer) {
    const bool negative = arena.at(id).kind == Kind::Neg;
    if (negative) {
        if (arena.is_approximate(id) || arena.children(id).size() != 1)
            return false;
        id = arena.children(id)[0];
    }
    if (arena.at(id).kind != Kind::Integer || arena.is_approximate(id))
        return false;
    const std::string &digits = arena.text(id);
    if (digits.empty() || digits.size() > 310 || mpz_set_str(integer, digits.c_str(), 10) != 0)
        return false;
    if (negative)
        mpz_neg(integer, integer);
    return mpz_sizeinbase(integer, 2) <= 1024;
}

NodeId integer_node(Arena &arena, mpz_srcptr integer) {
    std::array<char, 312> digits{};
    if (mpz_sizeinbase(integer, 2) > 1024) {
        arena.fail(Status::SizeExceeded);
        return kNoNode;
    }
    mpz_get_str(digits.data(), 10, integer);
    return arena.integer(digits.data());
}

NodeId integer_node(Arena &arena, uint64_t integer) {
    return arena.integer(integer_text(static_cast<int64_t>(integer)));
}

VerificationRecord checked(const Method &method, const std::string &detail) {
    VerificationRecord verification;
    verification.method = method.evidence;
    verification.outcome = VerificationOutcome::Passed;
    verification.strength = EvidenceStrength::StructurallyValid;
    verification.detail = detail;
    verification.evidence_id = method.obligation;
    return verification;
}

struct Run {
    TaskContext &task;
    Arena &arena;
    Derivation &derivation;
    NodeId expression;
    Meter meter;
    size_t mark;
    const Method *method = nullptr;
    std::array<int64_t, 3> arguments{};
    StepId plan = kNoStep;
    IntegerOutcome failure = IntegerOutcome::UnsupportedForm;
    std::string detail;
    bool failed = false;
    bool finished = false;

    Run(TaskContext &continuation, Arena &storage, Derivation &records, NodeId call,
        const Budget &budget)
        : task(continuation), arena(storage), derivation(records), expression(call), meter(budget),
          mark(records.mark()) {}

    ~Run() {
        // Destroying a suspended coroutine closes its request without an answer.
        if (!finished) {
            const bool cancelled = task.stop_state() == TaskState::Cancelled ||
                                   task.stop_state() == TaskState::Pending;
            finish(cancelled ? IntegerOutcome::Cancelled : IntegerOutcome::ResourceExceeded,
                   kNoNode, cancelled ? "cancelled" : "continuation storage unavailable");
        }
    }

    bool refuse(IntegerOutcome outcome, std::string reason) {
        failed = true;
        failure = outcome;
        detail = std::move(reason);
        return false;
    }

    bool spend() {
        if (arena.failed())
            return refuse(IntegerOutcome::ResourceExceeded, status_name(arena.status()));
        if (!meter.checkpoint() || !meter.rewrite())
            return refuse(meter.halt() == Halt::Cancelled ? IntegerOutcome::Cancelled
                                                         : IntegerOutcome::ResourceExceeded,
                          halt_name(meter.halt()));
        return true;
    }

    bool accept() {
        if (arena.failed())
            return refuse(IntegerOutcome::ResourceExceeded, status_name(arena.status()));
        if (meter.stopped())
            return refuse(meter.halt() == Halt::Cancelled ? IntegerOutcome::Cancelled
                                                         : IntegerOutcome::ResourceExceeded,
                          halt_name(meter.halt()));
        if (derivation.request.numeric_mode != NumericMode::Exact)
            return refuse(IntegerOutcome::UnsupportedForm, "integer walkthroughs require Exact mode");
        if (arena.at(expression).kind != Kind::Call)
            return refuse(IntegerOutcome::UnsupportedForm, "this is not an integer method call");
        method = method_named(arena.text(expression));
        if (!method)
            return refuse(IntegerOutcome::UnsupportedForm, "this integer method is not recorded");
        const ChildView arguments_view = arena.children(expression);
        if (arguments_view.size() != method->arity)
            return refuse(IntegerOutcome::InvalidInput, std::string(method->name) + " requires " +
                          std::to_string(method->arity) + " integer arguments");
        for (size_t i = 0; i < arguments_view.size(); ++i) {
            int64_t integer = 0;
            if (!small_integer(arena, arguments_view[i], &integer) ||
                (integer < 0 && method->kind != MethodKind::Gcd) ||
                arena.is_approximate(arguments_view[i]))
                return refuse(IntegerOutcome::UnsupportedForm,
                              method->kind == MethodKind::Gcd ? "gcd records exact integer literals only"
                                  : "this method records nonnegative exact integer literals only");
            arguments[i] = integer;
        }
        const uint64_t first = arguments[0];
        const uint64_t second = arguments[1];
        switch (method->kind) {
            case MethodKind::Gcd:
                if (magnitude(arguments[0]) > 1000000000 || magnitude(arguments[1]) > 1000000000)
                    return refuse(IntegerOutcome::UnsupportedForm,
                                  "gcd walkthroughs support integer literals from -1000000000 through 1000000000");
                break;
            case MethodKind::Quotient:
            case MethodKind::Remainder:
                if (second == 0)
                    return refuse(IntegerOutcome::InvalidInput, "the divisor must be positive");
                if (first > 1000000000 || second > 1000000000)
                    return refuse(IntegerOutcome::UnsupportedForm,
                                  "division walkthroughs support integer literals through 1000000000");
                break;
            case MethodKind::Factorial:
                if (first > 100)
                    return refuse(IntegerOutcome::UnsupportedForm,
                                  "factorial walkthroughs support integers from 0 through 100");
                break;
            case MethodKind::Permutation:
            case MethodKind::Combination:
                if (first > 100 || second > first)
                    return refuse(IntegerOutcome::UnsupportedForm,
                                  "these walkthroughs require 0 <= k <= n <= 100");
                break;
            case MethodKind::Prime:
            case MethodKind::Factorization:
                if (first > 1000000 || (method->kind == MethodKind::Factorization && first < 2))
                    return refuse(IntegerOutcome::UnsupportedForm,
                                  method->kind == MethodKind::Prime
                                      ? "primality walkthroughs support integers from 0 through 1000000"
                                      : "factorization walkthroughs support integers from 2 through 1000000");
                break;
            case MethodKind::NextPrime:
                if (first >= 1000000)
                    return refuse(IntegerOutcome::UnsupportedForm,
                                  "next-prime walkthroughs search above n and at most 1000000");
                break;
            case MethodKind::ModularPower:
                if (first > 1000000000 || second == 0 || second > 1000000 || arguments[2] < 2 ||
                    arguments[2] > 1000000000)
                    return refuse(IntegerOutcome::UnsupportedForm,
                                  "powmod walkthroughs require 0 <= base <= 1000000000, "
                                  "1 <= exponent <= 1000000 and 2 <= modulus <= 1000000000");
                break;
        }
        if (!meter.step())
            return refuse(IntegerOutcome::ResourceExceeded, halt_name(meter.halt()));
        Step step;
        step.phase = "integer";
        step.goal = print(arena, expression);
        step.rule_id = "plan.integer-method";
        step.rule_name = method->strategy;
        step.explanation_short = method->strategy;
        step.explanation_detailed = step.goal + ": " + method->strategy;
        PlanPayload strategy;
        strategy.strategy_id = step.rule_id;
        strategy.selected_strategy = method->strategy;
        strategy.matched_problem_facts.push_back(step.goal);
        strategy.selection_rationale = "The literal arguments fit this exact finite method";
        register_strategy_precondition(strategy, step, "pre.int.literal-envelope",
                                       method->kind == MethodKind::Gcd
                                           ? "signed integer literals fit the method's stated bounds"
                                           : "nonnegative integer literals fit the method's stated bounds",
                                       "integer envelope validation", EvidenceStrength::StructurallyValid,
                                       VerificationOutcome::Passed, step.goal);
        plan = derivation.add_plan(kNoStep, std::move(step), std::move(strategy));
        return true;
    }

    Step envelope(const Method &rule, std::string explanation, ClaimType claim) {
        Step step;
        step.phase = "integer";
        step.goal = print(arena, expression);
        step.rule_id = rule.rule;
        step.rule_name = rule.strategy;
        step.explanation_short = explanation;
        step.explanation_detailed = std::move(explanation) + ". ";
        if (&rule == &kTrial) {
            step.explanation_detailed += "An exact integer divisor leaves remainder zero. "
                "A nonzero remainder excludes this divisor, so the search must continue";
        } else {
            switch (rule.kind) {
                case MethodKind::Gcd:
                    if (&rule == &kGcdSign)
                        step.explanation_detailed += "Changing an integer's sign does not change its "
                            "positive divisors. The gcd is therefore unchanged by taking both magnitudes";
                    else if (&rule == &kGcdRemainder)
                        step.explanation_detailed += "When a = q*b + r, every common divisor of a and b "
                            "divides r, and every common divisor of b and r divides a. "
                            "The positive divisor strictly decreases, so this process terminates";
                    else
                        step.explanation_detailed += "A positive common divisor g is greatest if a*u+b*v=g: "
                            "every common divisor divides this integer combination and therefore divides g. "
                            "When both inputs are zero, gcd(0,0) is defined as zero";
                    break;
                case MethodKind::Quotient:
                case MethodKind::Remainder:
                    step.explanation_detailed += "For a nonnegative dividend and positive divisor, "
                        "the identity dividend = divisor * quotient + remainder with "
                        "0 <= remainder < divisor determines one quotient and one remainder";
                    break;
                case MethodKind::Factorial:
                    step.explanation_detailed += "The argument " + std::to_string(arguments[0]) +
                        " is a nonnegative integer. Its factorial counts orderings by multiplying "
                        "the choices for each position, using n! = n * (n-1)! and 0! = 1";
                    break;
                case MethodKind::Permutation:
                    step.explanation_detailed += "There are " + std::to_string(arguments[0]) +
                        " distinct objects and " + std::to_string(arguments[1]) +
                        " ordered positions. Each selected object removes one choice, giving "
                        "n * (n-1) * ... * (n-k+1), with one empty ordering when k is zero";
                    break;
                case MethodKind::Combination:
                    step.explanation_detailed += "Here 0 <= k <= n. Dividing the ordered choices "
                        "by their i possible positions counts each unordered selection once. "
                        "The recurrence C(n-k+i,i) = C(n-k+i-1,i-1) * (n-k+i) / i starts at 1. "
                        "Choosing a subset or its complement has the same count";
                    break;
                case MethodKind::Prime:
                    step.explanation_detailed += "Primes are integers at least 2. If an integer is "
                        "composite, one of a pair of proper factors is at most its square root. "
                        "Testing 2 excludes all even factors, then every odd candidate is tested";
                    break;
                case MethodKind::NextPrime:
                    step.explanation_detailed += "The requested prime must be strictly greater than " +
                        std::to_string(arguments[0]) + ". Testing candidates in increasing order "
                        "both proves the selected candidate prime and rules out every earlier candidate";
                    break;
                case MethodKind::ModularPower:
                    step.explanation_detailed += "The exponent is positive and the modulus is at least 2. "
                        "Replacing either factor by its residue preserves a product modulo m. "
                        "For an even exponent, b^(2j) = (b*b)^j. An odd exponent contributes "
                        "one extra b to the accumulated residue before halving";
                    break;
                case MethodKind::Factorization:
                    step.explanation_detailed += "A smallest divisor greater than 1 must be prime, "
                        "since a composite divisor would have a smaller prime factor. Removing each "
                        "copy preserves removed product * remaining integer. Once the trial divisor "
                        "exceeds the remaining square root, the remaining integer is prime";
                    break;
            }
        }
        step.claim = claim;
        step.proof_obligations.push_back({rule.obligation, rule.strategy});
        return step;
    }

    bool transformation(const Method &rule, NodeId before, NodeId after, std::string explanation,
                        VerificationRecord verification) {
        if (verification.outcome != VerificationOutcome::Passed)
            return refuse(IntegerOutcome::VerificationFailed, verification.detail);
        if (arena.failed() || before == kNoNode || after == kNoNode)
            return refuse(IntegerOutcome::ResourceExceeded, "integer expression storage exhausted");
        if (!meter.step())
            return refuse(IntegerOutcome::ResourceExceeded, halt_name(meter.halt()));
        Step step = envelope(rule, explanation, ClaimType::EquivalentExpression);
        verification.method = rule.evidence;
        verification.evidence_id = rule.obligation;
        step.verifications.push_back(std::move(verification));
        TransformationPayload change;
        change.before = before;
        change.after = after;
        change.concrete_action = std::move(explanation);
        change.reversible = true;
        derivation.add_transformation(plan, std::move(step), std::move(change));
        return true;
    }

    bool division_check(uint64_t dividend, uint64_t divisor, uint64_t quotient, uint64_t remainder) {
        const IntegerDivisionProof proof{integer_node(arena, dividend), integer_node(arena, 1),
                                         integer_node(arena, divisor), integer_node(arena, quotient),
                                         integer_node(arena, remainder)};
        VerificationRecord verification = verify_integer_division(arena, proof);
        if (arena.failed())
            return refuse(IntegerOutcome::ResourceExceeded, "integer expression storage exhausted");
        if (verification.outcome != VerificationOutcome::Passed)
            return refuse(IntegerOutcome::VerificationFailed, verification.detail);
        if (!meter.step())
            return refuse(IntegerOutcome::ResourceExceeded, halt_name(meter.halt()));
        const std::string equation = std::to_string(dividend) + " = " + std::to_string(divisor) +
                                    " * " + std::to_string(quotient) + " + " +
                                    std::to_string(remainder);
        const std::string explanation = equation + (remainder == 0 ? ", so the divisor fits exactly"
                                                                  : ", so this is not a divisor");
        Step step = envelope(kTrial, explanation, ClaimType::Definition);
        verification.evidence_id = kTrial.obligation;
        step.verifications.push_back(std::move(verification));
        CheckPayload check;
        check.target_claim = "whether " + std::to_string(divisor) + " divides " +
                             std::to_string(dividend);
        check.check_method = kTrial.evidence;
        check.expected_relation = "dividend = divisor * quotient + remainder, 0 <= remainder < divisor";
        check.observed_result = equation;
        derivation.add_check(plan, std::move(step), std::move(check));
        return true;
    }

    IntegerResult finish(IntegerOutcome outcome, NodeId answer, std::string reason) {
        finished = true;
        IntegerResult result;
        result.outcome = outcome;
        result.expression = outcome == IntegerOutcome::Evaluated ? answer : kNoNode;
        result.detail = std::move(reason);
        if (arena.failed()) {
            result.outcome = IntegerOutcome::ResourceExceeded;
            result.expression = kNoNode;
            result.detail = status_name(arena.status());
        }
        switch (result.outcome) {
            case IntegerOutcome::Evaluated:
                result.status = derivation.outcome_from(mark);
                if (result.status != DerivationStatus::SolvedAndVerified) {
                    result.outcome = IntegerOutcome::VerificationFailed;
                    result.expression = kNoNode;
                }
                break;
            case IntegerOutcome::UnsupportedForm: result.status = DerivationStatus::Unsupported; break;
            case IntegerOutcome::InvalidInput: result.status = DerivationStatus::InvalidInput; break;
            case IntegerOutcome::VerificationFailed: result.status = DerivationStatus::VerificationFailed; break;
            case IntegerOutcome::Cancelled: result.status = DerivationStatus::Cancelled; break;
            case IntegerOutcome::ResourceExceeded: result.status = DerivationStatus::ResourceLimitReached; break;
        }
        if (result.outcome != IntegerOutcome::Evaluated)
            keep_verified_prefix(derivation, mark, arena);
        result.cost = meter.cost();
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id = "number.integer-method.literal";
        context.requested_method = method ? method->name : "integer method";
        context.normalized_problem_model = expression;
        context.original_expression = derivation.request.original_expression;
        context.normalized_expression = print(arena, expression);
        context.numeric_mode = derivation.request.numeric_mode;
        context.angle_convention = "not applicable";
        context.branch_convention = method && method->kind == MethodKind::Gcd
            ? "signed integers with nonnegative gcd, gcd(0,0)=0" : "nonnegative integers";
        context.detail_projection = "standard";
        context.resource_policy = budget_policy(meter.budget());
        context.derivation_status = result.status;
        derivation.context = make_context(context);
        derivation.context.problem_family_envelope_version = "2";
        return result;
    }

    IntegerResult stopped() { return finish(failure, kNoNode, detail); }
};

Coroutine<bool> prime_trial(TaskContext &task, Run &run, uint64_t candidate) {
    bool prime = candidate >= 2;
    uint64_t divisor = 2;
    while (prime && divisor <= candidate / divisor) {
        co_await task.checkpoint();
        if (!run.spend())
            co_return false;
        const uint64_t quotient = candidate / divisor;
        const uint64_t remainder = candidate % divisor;
        if (!run.division_check(candidate, divisor, quotient, remainder))
            co_return false;
        if (remainder == 0)
            prime = false;
        else
            divisor = divisor == 2 ? 3 : divisor + 2;
    }
    co_await task.checkpoint();
    if (!run.spend())
        co_return false;
    const Method &method = *method_named("is_prime");
    const NodeId before = run.arena.call("is_prime", {integer_node(run.arena, candidate)});
    const NodeId after = integer_node(run.arena, prime ? 2 : 0);
    const std::string explanation = candidate < 2
        ? std::to_string(candidate) + " is below 2, so it is not prime (result 0)"
        : prime ? "No divisor among 2 and the odd integers through sqrt(" + std::to_string(candidate) +
                  ") fits. A composite has a factor at most its square root, so " +
                  std::to_string(candidate) + " is prime (result 2)"
                : std::to_string(divisor) + " is a proper divisor of " + std::to_string(candidate) +
                  ", so it is composite (result 0)";
    if (!run.transformation(method, before, after, explanation, checked(method, explanation)))
        co_return false;
    co_return prime;
}

}

std::optional<size_t> integer_command_arity(std::string_view name) {
    const Method *method = method_named(name);
    return method ? std::optional<size_t>(method->arity) : std::nullopt;
}

const char *integer_outcome_name(IntegerOutcome outcome) {
    switch (outcome) {
        case IntegerOutcome::Evaluated: return "evaluated";
        case IntegerOutcome::UnsupportedForm: return "unsupported form";
        case IntegerOutcome::InvalidInput: return "invalid input";
        case IntegerOutcome::VerificationFailed: return "verification failed";
        case IntegerOutcome::Cancelled: return "cancelled";
        case IntegerOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

VerificationRecord verify_integer_division(const Arena &arena, const IntegerDivisionProof &proof) {
    VerificationRecord verification;
    verification.method = "exact integer division identity";
    detail::Mpz multiplicand, multiplier, divisor, quotient, remainder, product, reconstruction;
    if (!proof_integer(arena, proof.multiplicand, multiplicand.get()) ||
        !proof_integer(arena, proof.multiplier, multiplier.get()) ||
        !proof_integer(arena, proof.divisor, divisor.get()) ||
        !proof_integer(arena, proof.quotient, quotient.get()) ||
        !proof_integer(arena, proof.remainder, remainder.get())) {
        verification.outcome = VerificationOutcome::Inconclusive;
        verification.detail = "the division proof requires five exact integer literals of at most 1024 bits";
        return verification;
    }
    const bool domain = mpz_sgn(multiplicand.get()) >= 0 && mpz_sgn(multiplier.get()) >= 0 &&
                        mpz_sgn(divisor.get()) > 0 && mpz_sgn(quotient.get()) >= 0 &&
                        mpz_sgn(remainder.get()) >= 0 && mpz_cmp(remainder.get(), divisor.get()) < 0;
    mpz_mul(product.get(), multiplicand.get(), multiplier.get());
    mpz_mul(reconstruction.get(), divisor.get(), quotient.get());
    mpz_add(reconstruction.get(), reconstruction.get(), remainder.get());
    const bool agrees = domain && mpz_cmp(product.get(), reconstruction.get()) == 0;
    verification.outcome = agrees ? VerificationOutcome::Passed : VerificationOutcome::Failed;
    verification.strength = strength_for(verification.outcome, EvidenceStrength::StructurallyValid);
    verification.detail = print(arena, proof.multiplicand) + " * " + print(arena, proof.multiplier) +
                          " = " + print(arena, proof.divisor) + " * " + print(arena, proof.quotient) +
                          " + " + print(arena, proof.remainder) +
                          (agrees ? ", with 0 <= remainder < divisor" : " fails the exact division identity or remainder bound");
    return verification;
}

VerificationRecord verify_integer_gcd(const Arena &arena, const IntegerGcdProof &proof) {
    VerificationRecord verification;
    verification.method = "common divisibility and Bezout identity";
    verification.evidence_id = "obl.int.gcd-certificate";
    detail::Mpz left, right, gcd, left_coefficient, right_coefficient, combination;
    if (!proof_integer(arena, proof.left, left.get()) ||
        !proof_integer(arena, proof.right, right.get()) ||
        !proof_integer(arena, proof.gcd, gcd.get()) ||
        !proof_integer(arena, proof.left_coefficient, left_coefficient.get()) ||
        !proof_integer(arena, proof.right_coefficient, right_coefficient.get())) {
        verification.outcome = VerificationOutcome::Inconclusive;
        verification.detail = "the gcd proof requires five exact integer literals of at most 1024 bits";
        return verification;
    }
    mpz_mul(combination.get(), left.get(), left_coefficient.get());
    mpz_addmul(combination.get(), right.get(), right_coefficient.get());
    const bool common = mpz_sgn(gcd.get()) == 0
        ? mpz_sgn(left.get()) == 0 && mpz_sgn(right.get()) == 0
        : mpz_sgn(gcd.get()) > 0 && mpz_divisible_p(left.get(), gcd.get()) &&
          mpz_divisible_p(right.get(), gcd.get());
    const bool agrees = common && mpz_cmp(combination.get(), gcd.get()) == 0;
    verification.outcome = agrees ? VerificationOutcome::Passed : VerificationOutcome::Failed;
    verification.strength = strength_for(verification.outcome, EvidenceStrength::StructurallyValid);
    verification.detail = print(arena, proof.left) + " * " + print(arena, proof.left_coefficient) +
        " + " + print(arena, proof.right) + " * " + print(arena, proof.right_coefficient) +
        " = " + print(arena, proof.gcd) + (agrees ? ", with a nonnegative common divisor"
            : " fails the gcd sign, common divisibility or Bezout identity");
    return verification;
}

Coroutine<IntegerResult> integer_steps(TaskContext &task, Arena &arena, Derivation &derivation,
                                       NodeId expression, Budget budget) {
    co_await task.checkpoint();
    Run run(task, arena, derivation, expression, budget);
    if (!run.accept())
        co_return run.stopped();
    const Method &method = *run.method;
    const uint64_t first = magnitude(run.arguments[0]);
    const uint64_t second = magnitude(run.arguments[1]);
    NodeId answer = kNoNode;
    std::string conclusion;

    if (method.kind == MethodKind::Gcd) {
        co_await task.checkpoint();
        if (!run.spend())
            co_return run.stopped();
        detail::Mpz previous, current, quotient, remainder;
        mpz_set_ui(previous.get(), static_cast<unsigned long>(first));
        mpz_set_ui(current.get(), static_cast<unsigned long>(second));
        NodeId pair = arena.call("gcd", {integer_node(arena, previous.get()), integer_node(arena, current.get())});
        const std::string normalization = "Signs do not change common divisors: " + print(arena, expression) +
                                          " = " + print(arena, pair);
        if (!run.transformation(kGcdSign, expression, pair, normalization, checked(kGcdSign, normalization)))
            co_return run.stopped();
        while (mpz_sgn(current.get()) != 0) {
            co_await task.checkpoint();
            if (!run.spend())
                co_return run.stopped();
            mpz_fdiv_qr(quotient.get(), remainder.get(), previous.get(), current.get());
            const IntegerDivisionProof division{integer_node(arena, previous.get()), integer_node(arena, 1),
                integer_node(arena, current.get()), integer_node(arena, quotient.get()),
                integer_node(arena, remainder.get())};
            const NodeId reduced = arena.call("gcd", {division.divisor, division.remainder});
            const std::string explanation = print(arena, division.multiplicand) + " = " +
                print(arena, division.quotient) + " * " + print(arena, division.divisor) + " + " +
                print(arena, division.remainder) + ", so " + print(arena, pair) + " = " + print(arena, reduced);
            if (!run.transformation(kGcdRemainder, pair, reduced, explanation,
                                    verify_integer_division(arena, division)))
                co_return run.stopped();
            mpz_swap(previous.get(), current.get());
            mpz_swap(current.get(), remainder.get());
            pair = reduced;
        }
        co_await task.checkpoint();
        if (!run.spend())
            co_return run.stopped();
        detail::Mpz left, right, gcd, left_coefficient, right_coefficient;
        detail::mpz_set_i64(left.get(), run.arguments[0]);
        detail::mpz_set_i64(right.get(), run.arguments[1]);
        mpz_gcdext(gcd.get(), left_coefficient.get(), right_coefficient.get(), left.get(), right.get());
        const IntegerGcdProof proof{integer_node(arena, left.get()), integer_node(arena, right.get()),
            integer_node(arena, gcd.get()), integer_node(arena, left_coefficient.get()),
            integer_node(arena, right_coefficient.get())};
        VerificationRecord verification = verify_integer_gcd(arena, proof);
        if (mpz_cmp(previous.get(), gcd.get()) != 0) {
            verification.outcome = VerificationOutcome::Failed;
            verification.strength = strength_for(verification.outcome, EvidenceStrength::StructurallyValid);
            verification.detail = "the certificate disagrees with the terminal Euclidean remainder";
        }
        answer = proof.gcd;
        conclusion = mpz_sgn(gcd.get()) == 0
            ? "Both inputs are zero, so gcd(0,0) = 0 by convention"
            : "The last nonzero remainder is " + print(arena, answer) + ". It divides both inputs, and " +
                print(arena, proof.left) + " * " + print(arena, proof.left_coefficient) + " + " +
                print(arena, proof.right) + " * " + print(arena, proof.right_coefficient) + " = " +
                print(arena, answer) + " certifies that every common divisor divides it";
        if (!run.transformation(method, expression, answer, conclusion, std::move(verification)))
            co_return run.stopped();
    } else if (method.kind == MethodKind::Quotient || method.kind == MethodKind::Remainder) {
        co_await task.checkpoint();
        if (!run.spend())
            co_return run.stopped();
        const uint64_t quotient = first / second;
        const uint64_t remainder = first % second;
        IntegerDivisionProof proof{integer_node(arena, first), integer_node(arena, 1),
                                   integer_node(arena, second), integer_node(arena, quotient),
                                   integer_node(arena, remainder)};
        answer = method.kind == MethodKind::Quotient ? proof.quotient : proof.remainder;
        conclusion = std::to_string(first) + " = " + std::to_string(second) + " * " +
                     std::to_string(quotient) + " + " + std::to_string(remainder) +
                     ", with 0 <= " + std::to_string(remainder) + " < " + std::to_string(second) +
                     ". The " + (method.kind == MethodKind::Quotient ? "quotient is " : "remainder is ") +
                     print(arena, answer);
        if (!run.transformation(method, expression, answer, conclusion, verify_integer_division(arena, proof)))
            co_return run.stopped();
    } else if (method.kind == MethodKind::Factorial || method.kind == MethodKind::Permutation ||
               method.kind == MethodKind::Combination) {
        detail::Mpz accumulated, numerator, following;
        mpz_set_ui(accumulated.get(), 1);
        const uint64_t count = method.kind == MethodKind::Factorial ? first
                               : method.kind == MethodKind::Combination ? std::min(second, first - second)
                                                                        : second;
        for (uint64_t index = 1; index <= count; ++index) {
            co_await task.checkpoint();
            if (!run.spend())
                co_return run.stopped();
            const uint64_t factor = method.kind == MethodKind::Factorial ? index
                                    : method.kind == MethodKind::Permutation ? first - index + 1
                                                                            : first - count + index;
            const uint64_t divisor = method.kind == MethodKind::Combination ? index : 1;
            mpz_mul_ui(numerator.get(), accumulated.get(), static_cast<unsigned long>(factor));
            mpz_tdiv_q_ui(following.get(), numerator.get(), static_cast<unsigned long>(divisor));
            IntegerDivisionProof proof{integer_node(arena, accumulated.get()), integer_node(arena, factor),
                                       integer_node(arena, divisor), integer_node(arena, following.get()),
                                       integer_node(arena, uint64_t{0})};
            VerificationRecord verification = verify_integer_division(arena, proof);
            const NodeId product = arena.binary(Kind::Mul, proof.multiplicand, proof.multiplier);
            const NodeId before = divisor == 1 ? product
                : arena.binary(Kind::Mul, product, arena.binary(Kind::Pow, proof.divisor,
                                  arena.unary(Kind::Neg, integer_node(arena, 1))));
            const std::string explanation = "Product " + std::to_string(index) + " of " +
                std::to_string(count) + ": " + print(arena, proof.multiplicand) + " * " +
                std::to_string(factor) + (divisor == 1 ? "" : " / " + std::to_string(divisor)) +
                " = " + print(arena, proof.quotient) +
                (method.kind == MethodKind::Combination ? ". C(n-k+i,i) = C(n-k+i-1,i-1) * (n-k+i) / i" : "");
            if (!run.transformation(method, before, proof.quotient, explanation, std::move(verification)))
                co_return run.stopped();
            mpz_swap(accumulated.get(), following.get());
        }
        co_await task.checkpoint();
        if (!run.spend())
            co_return run.stopped();
        answer = integer_node(arena, accumulated.get());
        conclusion = count == 0 ? "There are no factors to choose, so the empty product is 1"
                               : "All " + std::to_string(count) + " consecutive product steps are checked";
        if (method.kind == MethodKind::Combination && count != second)
            conclusion += ". Choosing k objects or their n-k complement gives the same count";
        conclusion += ". " + print(arena, expression) + " = " + print(arena, answer);
        if (!run.transformation(method, expression, answer, conclusion, checked(method, conclusion)))
            co_return run.stopped();
    } else if (method.kind == MethodKind::Prime) {
        const bool prime = co_await prime_trial(task, run, first);
        if (run.failed)
            co_return run.stopped();
        answer = integer_node(arena, prime ? 2 : 0);
        conclusion = prime ? "trial division proves this integer prime" : "this integer is not prime";
    } else if (method.kind == MethodKind::NextPrime) {
        uint64_t candidate = std::max<uint64_t>(2, first + 1);
        while (candidate <= 1000000) {
            const bool prime = co_await prime_trial(task, run, candidate);
            if (run.failed)
                co_return run.stopped();
            if (prime)
                break;
            ++candidate;
        }
        if (candidate > 1000000)
            co_return run.finish(IntegerOutcome::UnsupportedForm, kNoNode,
                                 "the next prime lies beyond the supported search ceiling 1000000");
        co_await task.checkpoint();
        if (!run.spend())
            co_return run.stopped();
        answer = integer_node(arena, candidate);
        const uint64_t lowest_candidate = std::max<uint64_t>(2, first + 1);
        conclusion = std::to_string(candidate) + " is prime. ";
        conclusion += candidate == lowest_candidate ? "There is no intervening candidate"
            : "Every integer from " + std::to_string(lowest_candidate) + " through " +
              std::to_string(candidate - 1) + " was excluded";
        conclusion += ", and integers below 2 are not prime";
        if (!run.transformation(method, expression, answer, conclusion, checked(method, conclusion)))
            co_return run.stopped();
    } else if (method.kind == MethodKind::ModularPower) {
        const uint64_t modulus = run.arguments[2];
        uint64_t base = first % modulus;
        uint64_t exponent = second;
        uint64_t residue = 1;
        co_await task.checkpoint();
        if (!run.spend())
            co_return run.stopped();
        IntegerDivisionProof reduction{integer_node(arena, first), integer_node(arena, 1),
                                       integer_node(arena, modulus), integer_node(arena, first / modulus),
                                       integer_node(arena, base)};
        const NodeId reduced = arena.call("powmod", {reduction.remainder, integer_node(arena, exponent),
                                                     reduction.divisor});
        const std::string reduction_text = "Reduce the base: " + std::to_string(first) + " mod " +
                                           std::to_string(modulus) + " = " + std::to_string(base);
        if (!run.transformation(method, expression, reduced, reduction_text,
                                verify_integer_division(arena, reduction)))
            co_return run.stopped();
        while (exponent != 0) {
            if (exponent % 2 != 0) {
                co_await task.checkpoint();
                if (!run.spend())
                    co_return run.stopped();
                const uint64_t product = residue * base;
                IntegerDivisionProof proof{integer_node(arena, residue), integer_node(arena, base),
                                           integer_node(arena, modulus), integer_node(arena, product / modulus),
                                           integer_node(arena, product % modulus)};
                const NodeId before = arena.call("irem", {arena.binary(Kind::Mul, proof.multiplicand,
                                                          proof.multiplier), proof.divisor});
                const std::string explanation = "Exponent " + std::to_string(exponent) +
                    " is odd. Multiply the accumulated residue: " + std::to_string(residue) + " * " +
                    std::to_string(base) + " mod " + std::to_string(modulus) + " = " +
                    std::to_string(product % modulus);
                if (!run.transformation(method, before, proof.remainder, explanation,
                                        verify_integer_division(arena, proof)))
                    co_return run.stopped();
                residue = product % modulus;
            }
            exponent /= 2;
            if (exponent != 0) {
                co_await task.checkpoint();
                if (!run.spend())
                    co_return run.stopped();
                const uint64_t square = base * base;
                IntegerDivisionProof proof{integer_node(arena, base), integer_node(arena, base),
                                           integer_node(arena, modulus), integer_node(arena, square / modulus),
                                           integer_node(arena, square % modulus)};
                const NodeId before = arena.call("irem", {arena.binary(Kind::Mul, proof.multiplicand,
                                                          proof.multiplier), proof.divisor});
                const std::string explanation = "Halve the remaining exponent to " + std::to_string(exponent) +
                    " and square the base: " + std::to_string(base) + "^2 mod " +
                    std::to_string(modulus) + " = " + std::to_string(square % modulus);
                if (!run.transformation(method, before, proof.remainder, explanation,
                                        verify_integer_division(arena, proof)))
                    co_return run.stopped();
                base = square % modulus;
            }
        }
        co_await task.checkpoint();
        if (!run.spend())
            co_return run.stopped();
        answer = integer_node(arena, residue);
        conclusion = "The exponent is now zero. Each checked square and multiply preserved "
                     "accumulated residue * base^remaining exponent modulo " + std::to_string(modulus) +
                     ", so the least nonnegative residue is " + std::to_string(residue);
        if (!run.transformation(method, expression, answer, conclusion, checked(method, conclusion)))
            co_return run.stopped();
    } else {
        uint64_t remaining = first;
        uint64_t divisor = 2;
        uint64_t reconstructed = 1;
        std::vector<std::pair<uint64_t, uint64_t>> factors;
        while (divisor <= remaining / divisor) {
            co_await task.checkpoint();
            if (!run.spend())
                co_return run.stopped();
            const uint64_t quotient = remaining / divisor;
            const uint64_t remainder = remaining % divisor;
            if (!run.division_check(remaining, divisor, quotient, remainder))
                co_return run.stopped();
            if (remainder == 0) {
                if (!factors.empty() && factors.back().first == divisor)
                    ++factors.back().second;
                else
                    factors.push_back({divisor, 1});
                reconstructed *= divisor;
                remaining = quotient;
            } else {
                divisor = divisor == 2 ? 3 : divisor + 2;
            }
        }
        co_await task.checkpoint();
        if (!run.spend())
            co_return run.stopped();
        if (remaining > 1) {
            if (!factors.empty() && factors.back().first == remaining)
                ++factors.back().second;
            else
                factors.push_back({remaining, 1});
        }
        IntegerDivisionProof proof{integer_node(arena, reconstructed), integer_node(arena, remaining),
                                   integer_node(arena, 1), integer_node(arena, first), integer_node(arena, uint64_t{0})};
        VerificationRecord verification = verify_integer_division(arena, proof);
        std::vector<NodeId> powers;
        for (const auto &factor : factors) {
            const NodeId prime = integer_node(arena, factor.first);
            powers.push_back(factor.second == 1 ? prime
                : arena.binary(Kind::Pow, prime, integer_node(arena, factor.second)));
        }
        answer = powers.size() == 1 ? powers[0] : arena.nary(Kind::Mul, powers);
        conclusion = "Each removed divisor was the smallest possible remaining factor, hence prime. "
                     "No divisor through the square root remains, so the final factor is prime. " +
                     print(arena, answer) + " = " + std::to_string(first);
        if (!run.transformation(method, expression, answer, conclusion, std::move(verification)))
            co_return run.stopped();
    }
    co_return run.finish(IntegerOutcome::Evaluated, answer, conclusion);
}

IntegerResult integer_method(Arena &arena, Derivation &derivation, NodeId expression,
                              const Budget &budget) {
    std::array<std::byte, 16384> storage{};
    TaskContext context(storage);
    auto task = make_task(context, integer_steps, arena, derivation, expression, budget);
    while (task.state() == TaskState::Pending)
        task.advance(64);
    if (const IntegerResult *result = task.result())
        return *result;
    IntegerResult result;
    result.outcome = task.state() == TaskState::Cancelled ? IntegerOutcome::Cancelled
                                                         : IntegerOutcome::ResourceExceeded;
    result.status = result.outcome == IntegerOutcome::Cancelled ? DerivationStatus::Cancelled
                                                               : DerivationStatus::ResourceLimitReached;
    result.detail = "integer continuation could not complete";
    return result;
}

}
