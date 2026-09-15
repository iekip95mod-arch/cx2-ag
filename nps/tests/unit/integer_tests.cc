#include "nps/steps/integer.h"

#include <array>

#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"
#include "../step_invariants.h"

namespace nps {
namespace {

struct Example {
    const char *call;
    const char *answer;
};

const Example kExamples[] = {
    {"iquo(17,5)", "3"}, {"irem(17,5)", "2"},
    {"iquo(0,7)", "0"}, {"irem(1000000000,999999999)", "1"},
    {"factorial(0)", "1"}, {"factorial(1)", "1"}, {"factorial(5)", "120"},
    {"factorial(25)", "15511210043330985984000000"},
    {"factorial(100)", "93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000"},
    {"perm(5,2)", "20"}, {"perm(0,0)", "1"}, {"perm(100,3)", "970200"},
    {"comb(5,2)", "10"}, {"comb(0,0)", "1"}, {"comb(100,0)", "1"},
    {"comb(100,100)", "1"}, {"comb(100,50)", "100891344545564193334812497256"},
    {"is_prime(0)", "0"}, {"is_prime(1)", "0"}, {"is_prime(2)", "2"},
    {"is_prime(17)", "2"}, {"is_prime(49)", "0"}, {"is_prime(561)", "0"},
    {"is_prime(999983)", "2"}, {"is_prime(1000000)", "0"},
    {"nextprime(0)", "2"}, {"nextprime(2)", "3"}, {"nextprime(17)", "19"},
    {"nextprime(113)", "127"},
    {"powmod(2,10,17)", "4"}, {"powmod(0,7,19)", "0"},
    {"powmod(1000000000,1000000,999999999)", "1"},
    {"ifactor(2)", "2"}, {"ifactor(60)", "((2^2) * 3 * 5)"},
    {"ifactor(1024)", "(2^10)"}, {"ifactor(999983)", "999983"},
    {"gcd(48,18)", "6"}, {"gcd(-48,18)", "6"}, {"gcd(48,-18)", "6"},
    {"gcd(-48,-18)", "6"}, {"gcd(0,0)", "0"}, {"gcd(0,-18)", "18"},
    {"gcd(-48,0)", "48"}, {"gcd(42,42)", "42"}, {"gcd(1,-999999999)", "1"},
    {"gcd(-1000000000,1000000000)", "1000000000"}, {"gcd(701408733,433494437)", "1"},
};

bool cancel_now(void *) { return true; }

bool cancel_countdown(void *opaque) {
    size_t &remaining = *static_cast<size_t *>(opaque);
    if (remaining == 0)
        return true;
    --remaining;
    return false;
}

std::string trace(const Arena &arena, const Derivation &derivation) {
    std::string text;
    for (size_t i = 0; i < derivation.size(); ++i) {
        const Step &step = derivation.at(static_cast<StepId>(i));
        text += step.rule_id + ":" + step.explanation_detailed + "\n";
        if (const auto *change = derivation.transformation(static_cast<StepId>(i)))
            text += print(arena, change->before) + "=" + print(arena, change->after) + "\n";
    }
    return text;
}

}

void run_integer_tests(TestSink &t) {
    for (const char *call : {"gcd([1,2],3)", "gcd(0*[1,2],3)", "factorial([[1]])",
                             "iquo(1,sin([1,2]))"}) {
        Arena arena;
        Derivation derivation;
        const ParseResult parsed = parse(arena, call);
        t.check(parsed.ok(), std::string("collection integer input parses: ") + call);
        if (!parsed.ok())
            continue;
        const IntegerResult reply = integer_method(arena, derivation, parsed.root);
        t.check(reply.outcome == IntegerOutcome::UnsupportedForm &&
                    reply.status == DerivationStatus::Unsupported && reply.expression == kNoNode &&
                    derivation.size() == 0 && reply.cost.backend_calls == 0,
                std::string("integer methods retain the scalar literal requirement: ") + call);
    }
    for (const Example &example : kExamples) {
        Arena arena;
        Derivation derivation;
        const NodeId call = parse(arena, example.call).root;
        const IntegerResult result = integer_method(arena, derivation, call);
        t.check(result.outcome == IntegerOutcome::Evaluated, std::string(example.call) + " evaluates");
        t.equal(result.expression == kNoNode ? "" : print(arena, result.expression), example.answer,
                std::string(example.call) + " has the independently calculated answer");
        t.check(result.status == DerivationStatus::SolvedAndVerified && derivation.size() >= 2 &&
                    derivation.all_verified_from(0),
                std::string(example.call) + " records checked reasoning");
        t.check(derivation.outcome_from(0) == result.status && result.expression != kNoNode,
                std::string(example.call) + " reports the record's own verdict over a fully checked range");
        t.check(result.cost.backend_calls == 0 && result.cost.steps == derivation.size(),
                std::string(example.call) + " meters each record without a backend");
        t.check(derivation.context.normalized_problem_model == call &&
                    derivation.context.derivation_status == result.status,
                std::string(example.call) + " binds the context to the request and outcome");
        t.equal(derivation.context.problem_family_envelope_version, "2",
                std::string(example.call) + " records the implemented envelope version");
        invariants::Pass invariants;
        std::vector<std::string> broken;
        invariants.walk(arena, derivation, false, true, &broken);
        const auto *explanations = invariants.find("STEP-021");
        t.check(explanations && explanations->checked > 0 && explanations->broken == 0,
                std::string(example.call) + " explains why each recorded operation applies");
    }

    const char *unsupported[] = {
        "factorial(101)", "factorial(-1)", "factorial(5.0)", "factorial(2+3)",
        "perm(101,2)", "perm(5,6)", "comb(5,6)", "comb(101,2)",
        "iquo(-17,5)", "irem(17,-5)", "iquo(1000000001,5)",
        "powmod(2,0,17)", "powmod(2,-1,17)", "powmod(2,3,1)",
        "powmod(2,1000001,17)", "is_prime(1000001)", "ifactor(1)",
        "ifactor(1000001)", "nextprime(1000000)", "gcd(x,3)",
        "gcd(1000000001,3)", "gcd(-1000000001,3)", "gcd(3,1000000001)",
        "gcd(3,-1000000001)", "gcd(2.0,3)", "gcd(1+1,3)",
        "gcd(-9223372036854775808,3)", "iegcd(45,75)", "iabcuv(21,28,7)",
    };
    for (const char *call : unsupported) {
        Arena arena;
        Derivation derivation;
        const IntegerResult result = integer_method(arena, derivation, parse(arena, call).root);
        t.check(result.outcome == IntegerOutcome::UnsupportedForm && result.expression == kNoNode &&
                    derivation.size() == 0 && result.status == DerivationStatus::Unsupported,
                std::string(call) + " refuses the neighboring form explicitly");
    }
    for (const char *call : {"iquo(17,0)", "irem(17,0)", "factorial()", "powmod(2,3)",
                            "gcd()", "gcd(2)", "gcd(2,3,4)"}) {
        Arena arena;
        Derivation derivation;
        const IntegerResult result = integer_method(arena, derivation, parse(arena, call).root);
        t.check(result.outcome == IntegerOutcome::InvalidInput && result.expression == kNoNode,
                std::string(call) + " has no invented answer");
    }
    {
        Arena arena;
        IntegerDivisionProof proof{arena.integer("17"), arena.integer("1"), arena.integer("5"),
                                   arena.integer("3"), arena.integer("2")};
        t.check(verify_integer_division(arena, proof).outcome == VerificationOutcome::Passed,
                "the production checker accepts 17 = 1 * (5 * 3 + 2)");
        for (NodeId IntegerDivisionProof::*field : {&IntegerDivisionProof::multiplicand,
                &IntegerDivisionProof::multiplier, &IntegerDivisionProof::divisor,
                &IntegerDivisionProof::quotient, &IntegerDivisionProof::remainder}) {
            IntegerDivisionProof corrupt = proof;
            corrupt.*field = arena.integer("99");
            t.check(verify_integer_division(arena, corrupt).outcome == VerificationOutcome::Failed,
                    "corruption of each integer proof field is rejected");
        }
        proof.quotient = arena.integer("2");
        proof.remainder = arena.integer("7");
        t.check(verify_integer_division(arena, proof).outcome == VerificationOutcome::Failed,
                "an identity with a remainder outside its canonical range is rejected");
        proof.divisor = arena.integer("0");
        t.check(verify_integer_division(arena, proof).outcome == VerificationOutcome::Failed,
                "a zero proof divisor is rejected");
        proof.divisor = arena.integer(std::string(400, '9'));
        t.check(verify_integer_division(arena, proof).outcome == VerificationOutcome::Inconclusive,
                "proof verification refuses numbers beyond its bounded arithmetic envelope");
        proof.divisor = kNoNode;
        t.check(verify_integer_division(arena, proof).outcome != VerificationOutcome::Passed,
                "an absent proof field cannot be verified");
    }
    {
        Arena arena;
        IntegerGcdProof proof{arena.integer("48"), arena.integer("18"), arena.integer("6"),
                              arena.integer("-1"), arena.integer("3")};
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Passed,
                "the production gcd checker accepts -48 + 54 = 6 and common divisibility");
        for (NodeId IntegerGcdProof::*field : {&IntegerGcdProof::left, &IntegerGcdProof::right,
                &IntegerGcdProof::gcd, &IntegerGcdProof::left_coefficient,
                &IntegerGcdProof::right_coefficient}) {
            IntegerGcdProof corrupt = proof;
            corrupt.*field = arena.integer("99");
            t.check(verify_integer_gcd(arena, corrupt).outcome == VerificationOutcome::Failed,
                    "corruption of each gcd certificate field is rejected");
        }
        proof.left = parse(arena, "-48").root;
        proof.left_coefficient = arena.integer("1");
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Passed,
                "the gcd checker preserves the parser's signed integer representation");
        proof = {arena.integer("6"), arena.integer("10"), arena.integer("4"),
                 arena.integer("-1"), arena.integer("1")};
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Failed,
                "a Bezout identity without common divisibility is not a gcd proof");
        proof = {arena.integer("48"), arena.integer("18"), arena.integer("3"),
                 arena.integer("-1"), arena.integer("3")};
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Failed,
                "a smaller common divisor without a Bezout identity is not proved greatest");
        proof.gcd = arena.integer("-6");
        proof.left_coefficient = arena.integer("1");
        proof.right_coefficient = arena.integer("-3");
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Failed,
                "a negative common divisor cannot satisfy the nonnegative gcd convention");
        proof = {arena.integer("0"), arena.integer("0"), arena.integer("0"),
                 arena.integer("0"), arena.integer("0")};
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Passed,
                "the zero-pair convention has an exact certificate");
        proof.left = arena.integer("1");
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Failed,
                "a zero combination alone cannot claim gcd zero for nonzero inputs");
        proof.left = arena.integer(std::string(400, '9'));
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Inconclusive,
                "gcd certificate verification bounds its GMP operands");
        proof.left = kNoNode;
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Inconclusive,
                "an absent gcd certificate field cannot pass verification");
        proof.left = arena.integer("0");
        arena.mark_approximate(proof.left);
        t.check(verify_integer_gcd(arena, proof).outcome == VerificationOutcome::Inconclusive,
                "a gcd certificate cannot promote an approximate operand to exact evidence");
    }
    {
        Arena arena;
        Derivation derivation;
        const NodeId call = parse(arena, "factorial(25)").root;
        const size_t nodes = arena.node_count();
        std::array<std::byte, 16384> storage{};
        TaskContext context(storage);
        auto task = make_task(context, integer_steps, arena, derivation, call, Budget());
        t.check(task.state() == TaskState::Pending && derivation.size() == 0 &&
                    arena.node_count() == nodes,
                "begin allocates only the continuation and does no integer work");
        task.advance(1);
        t.check(task.state() == TaskState::Pending && task.result() == nullptr &&
                    derivation.size() == 1,
                "one slice accepts the plan while arithmetic genuinely remains pending");
        task.advance(1);
        t.check(task.state() == TaskState::Pending && derivation.size() == 2,
                "the next single unit computes and records one product");
        task.cancel();
        t.check(task.state() == TaskState::Cancelled && context.live_frames() == 0 &&
                    derivation.context.derivation_status == DerivationStatus::Cancelled &&
                    derivation.all_verified_from(0),
                "external cancellation destroys the frame and preserves a checked prefix");
    }
    {
        Arena arena;
        Derivation derivation;
        const NodeId call = parse(arena, "gcd(-48,18)").root;
        const size_t initial_nodes = arena.node_count();
        std::array<std::byte, 16384> storage{};
        TaskContext context(storage);
        auto task = make_task(context, integer_steps, arena, derivation, call, Budget());
        t.check(derivation.size() == 0 && arena.node_count() == initial_nodes,
                "begin performs no gcd normalization, division or certificate generation");
        task.advance(1);
        task.advance(1);
        t.check(task.state() == TaskState::Pending && derivation.size() == 2 && !task.result(),
                "the plan and signed normalization leave all Euclidean divisions pending");
        task.advance(1);
        const auto *reduction = derivation.transformation(2);
        t.check(task.state() == TaskState::Pending && derivation.size() == 3 && reduction &&
                    print(arena, reduction->before) == "gcd(48, 18)" &&
                    print(arena, reduction->after) == "gcd(18, 12)",
                "one gcd work unit computes and records exactly one actual Euclidean division");
        task.cancel();
        t.check(context.live_frames() == 0 && !task.result() && derivation.all_verified_from(0) &&
                    derivation.context.derivation_status == DerivationStatus::Cancelled,
                "cancelling a gcd releases GMP state and retains only the checked prefix");
        const IntegerResult recovered = integer_method(arena, derivation, call);
        t.check(recovered.outcome == IntegerOutcome::Evaluated && print(arena, recovered.expression) == "6",
                "a cancelled gcd leaves the next request usable");
    }
    for (const char *call : {"iquo(17,5)", "irem(17,5)", "perm(7,3)", "factorial(25)", "comb(20,10)", "is_prime(97)",
                            "nextprime(113)", "powmod(23,101,97)", "ifactor(924)",
                            "gcd(-48,18)", "gcd(0,0)", "gcd(701408733,433494437)"}) {
        Arena baseline_arena;
        Derivation baseline;
        const IntegerResult expected = integer_method(baseline_arena, baseline,
                                                       parse(baseline_arena, call).root);
        for (size_t slice : {size_t{1}, size_t{2}, size_t{7}, size_t{128}}) {
            Arena arena;
            Derivation derivation;
            std::array<std::byte, 16384> storage{};
            TaskContext context(storage);
            auto task = make_task(context, integer_steps, arena, derivation, parse(arena, call).root,
                                  Budget());
            size_t advances = 0;
            while (task.state() == TaskState::Pending && ++advances < 10000)
                task.advance(slice);
            t.check(task.result() && task.result()->outcome == IntegerOutcome::Evaluated &&
                        print(arena, task.result()->expression) ==
                            print(baseline_arena, expected.expression) &&
                        task.result()->cost.rewrites == expected.cost.rewrites,
                    std::string(call) + " has schedule-independent work and answer");
            t.equal(trace(arena, derivation), trace(baseline_arena, baseline),
                    std::string(call) + " has schedule-independent recorded reasoning");
        }
        for (size_t stop = 0; stop <= expected.cost.rewrites; ++stop) {
            Arena arena;
            Derivation derivation;
            Budget budget;
            size_t remaining = stop;
            budget.poll = cancel_countdown;
            budget.poll_context = &remaining;
            const IntegerResult result = integer_method(arena, derivation, parse(arena, call).root,
                                                         budget);
            t.check(result.outcome == IntegerOutcome::Cancelled && result.expression == kNoNode &&
                        (derivation.size() == 0 || derivation.all_verified_from(0)),
                    std::string(call) + " polls cancellation before each bounded work unit");
        }
    }
    for (const char *call : {"factorial(25)", "gcd(701408733,433494437)"}) {
      for (bool step_limit : {false, true}) {
        Arena arena;
        Derivation derivation;
        Budget budget;
        if (step_limit)
            budget.max_steps = 3;
        else
            budget.max_rewrites = 3;
        const IntegerResult result = integer_method(arena, derivation,
                                                     parse(arena, call).root, budget);
        t.check(result.outcome == IntegerOutcome::ResourceExceeded && result.expression == kNoNode &&
                    result.status == DerivationStatus::ResourceLimitReached &&
                    derivation.all_verified_from(0),
                "integer work stops at both cumulative limits without exposing a partial answer");
      }
    }
    {
        Arena arena;
        Derivation derivation;
        Budget budget;
        budget.poll = cancel_now;
        const IntegerResult result = integer_method(arena, derivation,
                                                     parse(arena, "factorial(5)").root, budget);
        t.check(result.outcome == IntegerOutcome::Cancelled && derivation.size() == 0,
                "initial cancellation performs no product and appends no plan");
        const IntegerResult recovered = integer_method(arena, derivation,
                                                        parse(arena, "factorial(5)").root);
        t.check(recovered.outcome == IntegerOutcome::Evaluated,
                "a cancelled request does not poison the next integer request");
    }
    for (const char *call : {"factorial(25)", "gcd(-48,18)"}) {
        Limits limits;
        limits.max_nodes = 6;
        Arena arena(limits);
        Derivation derivation;
        const IntegerResult result = integer_method(arena, derivation,
                                                     parse(arena, call).root);
        t.check(result.outcome == IntegerOutcome::ResourceExceeded && result.expression == kNoNode &&
                    derivation.size() == 0,
                "arena exhaustion discards records that could reference absent nodes");
    }
    for (const char *call : {"factorial(5)", "gcd(-48,18)"}) {
        Arena arena;
        Derivation derivation;
        derivation.request.numeric_mode = NumericMode::Decimal;
        const IntegerResult result = integer_method(arena, derivation,
                                                     parse(arena, call).root);
        t.check(result.outcome == IntegerOutcome::UnsupportedForm && derivation.size() == 0 &&
                    result.expression == kNoNode,
                "the native boundary refuses Decimal mode before recording an exact walkthrough");
    }
    {
        Arena arena;
        Derivation derivation;
        const IntegerResult result = integer_method(arena, derivation,
                                                     parse(arena, "nextprime(999999)").root);
        t.check(result.outcome == IntegerOutcome::UnsupportedForm && result.expression == kNoNode &&
                    derivation.all_verified_from(0),
                "a search reaching its finite ceiling keeps checked exclusions without inventing a prime");
    }
    {
        Arena arena;
        Derivation derivation;
        std::array<std::byte, 1> storage{};
        TaskContext context(storage);
        auto task = make_task(context, integer_steps, arena, derivation,
                              parse(arena, "factorial(5)").root, Budget());
        t.check(task.state() == TaskState::AllocationFailed && derivation.size() == 0,
                "insufficient continuation storage refuses before mathematical work");
    }
    {
        size_t root_bytes = 0;
        {
            Arena arena;
            Derivation derivation;
            std::array<std::byte, 16384> storage{};
            TaskContext context(storage);
            auto task = make_task(context, integer_steps, arena, derivation,
                                  parse(arena, "is_prime(97)").root, Budget());
            root_bytes = context.used_bytes();
        }
        Arena arena;
        Derivation derivation;
        std::array<std::byte, 16384> storage{};
        TaskContext context(std::span<std::byte>(storage.data(), root_bytes));
        auto task = make_task(context, integer_steps, arena, derivation,
                              parse(arena, "is_prime(97)").root, Budget());
        task.advance(64);
        t.check(task.state() == TaskState::AllocationFailed && context.live_frames() == 0 &&
                    derivation.context.derivation_status == DerivationStatus::ResourceLimitReached &&
                    derivation.all_verified_from(0),
                "child allocation refusal cannot become a false primality answer and closes the request");
    }
    t.check(integer_command_arity("powmod") == 3 && integer_command_arity("factorial") == 1 &&
                integer_command_arity("perm") == 2 && integer_command_arity("gcd") == 2 &&
                !integer_command_arity("iegcd") && !integer_command_arity("iabcuv"),
            "command recognition and arity come from the integer method table");
}

}
