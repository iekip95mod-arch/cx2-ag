#include <charconv>
#include <cstdint>
#include <string>

#include "nps/core/print.h"
#include "nps/physics/relativity.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

int64_t integer_of(const std::string &text) {
    int64_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// The declared givens are exact fractions of c and whole seconds, which the decimal quantity parser
// does not read, so a measured decimal goes through it and everything else is built here.
Quantity exact_quantity(const std::string &text) {
    Quantity value;
    const size_t slash = text.find('/');
    if (slash == std::string::npos) {
        value.value.num = integer_of(text);
        value.value.den = 1;
        return value;
    }
    value.value.num = integer_of(text.substr(0, slash));
    value.value.den = integer_of(text.substr(slash + 1));
    return value;
}

Quantity read_text(const std::string &text) {
    if (text.find('.') == std::string::npos)
        return exact_quantity(text);
    Quantity parsed;
    std::string error;
    parse_quantity(text, &parsed, &error);
    return parsed;
}

Quantity declared(RelativityVariable variable, const char *text) {
    Quantity parsed = read_text(text);
    // The family reads its own declared working units, which the unit table does not carry, so the
    // test attaches them the way a bridge would.
    Unit unit;
    unit.text = relativity_variable_unit(variable);
    unit.dimension = relativity_variable_dimension(variable);
    unit.scale.num = 1;
    unit.scale.den = 1;
    parsed.unit = unit;
    return parsed;
}

RelativityKnown known(RelativityVariable variable, const char *text) {
    RelativityKnown entry;
    entry.variable = variable;
    entry.quantity = declared(variable, text);
    return entry;
}

Quantity boost(const char *text) {
    Quantity parsed = read_text(text);
    Unit unit;
    unit.text = "c";
    unit.scale.num = 1;
    unit.scale.den = 1;
    parsed.unit = unit;
    return parsed;
}

RelativityProblem problem(RelativityRelation relation, const char *beta) {
    RelativityProblem input;
    input.relation = relation;
    input.rest_frame.name = "station";
    input.moving_frame.name = "ship";
    input.boost = boost(beta);
    return input;
}

struct Run {
    RelativityResult result;
    size_t steps = 0;
    std::string rules;
    std::string context_family;
    std::string substituted;
    std::string equation;
    std::string assumptions;
    bool has_unverified = false;
};

Run run(const RelativityProblem &input, const Budget &budget = Budget()) {
    Arena arena;
    Derivation derivation;
    Run out;
    out.result = solve_relativity(arena, derivation, input, budget);
    out.steps = derivation.size();
    out.context_family = derivation.context.problem_family_id;
    for (const std::string &assumption : derivation.context.active_assumptions) {
        out.assumptions += assumption;
        out.assumptions += ";";
    }
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        out.rules += step.rule_id;
        out.rules += ";";
        for (const VerificationRecord &record : step.verifications) {
            if (record.outcome == VerificationOutcome::Failed)
                out.has_unverified = true;
        }
    }
    if (out.result.substituted != kNoNode)
        out.substituted = print(arena, out.result.substituted);
    if (out.result.equation != kNoNode)
        out.equation = print(arena, out.result.equation);
    return out;
}

bool contains_text(const std::string &haystack, const char *needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string reported(const Run &run, RelativityVariable variable) {
    for (const RelativityOutput &output : run.result.outputs) {
        if (output.variable == variable)
            return output.value_text + " " + output.unit_text + " in " + output.frame.name;
    }
    return "absent";
}

struct PollAfter {
    size_t calls = 0;
    size_t stop_at = 0;
};

bool poll_after(void *context) {
    PollAfter *poll = static_cast<PollAfter *>(context);
    ++poll->calls;
    return poll->calls >= poll->stop_at;
}

}  // namespace

void run_relativity_tests(TestSink &t) {
    {
        // beta = 3/5 gives gamma = 5/4 exactly, so a 4 s proper interval reads 5 s in the station.
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run solved = run(input);
        t.equal(relativity_outcome_name(solved.result.outcome), "solved",
                "a proper time gives the dilated time");
        t.equal(solved.result.factor_text, "1.25", "the Lorentz factor is exact at beta = 3/5");
        t.equal(reported(solved, RelativityVariable::DilatedTime), "5 s in station",
                "the dilated time is reported in the rest frame that measured it");
        t.equal(solved.equation, "(dt = (gamma * dt0))", "the model is the dilation relation");
        t.equal(solved.context_family, "physics.relativity.time-dilation",
                "the context names the time-dilation family");
        t.check(!solved.has_unverified, "no recorded verification failed");
        t.check(contains_text(solved.assumptions,
                              "ship moves at beta = 0.6 c along the positive x axis of station"),
                "the context states the frame and sign convention it solved under");
        t.check(solved.rules.find("physics.relativity.apply") <
                    solved.rules.find("physics.relativity.check-invariant"),
                "the relation is applied before the invariant check");
        t.evidence("PHYS-023",
                   contains_text(solved.rules, "physics.relativity.plan") &&
                       contains_text(solved.rules, "physics.relativity.check-frames") &&
                       contains_text(solved.rules, "physics.relativity.check-boost") &&
                       contains_text(solved.rules, "physics.relativity.lorentz-factor") &&
                       contains_text(solved.rules, "physics.relativity.apply") &&
                       contains_text(solved.rules, "physics.relativity.check-invariant"),
                   "the relativity family records its plan, frame, speed, factor, relation and "
                   "invariant steps");
    }
    {
        // A boost along the negative x axis carries the same factor, because gamma reads beta^2.
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "-3/5");
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run solved = run(input);
        t.equal(relativity_outcome_name(solved.result.outcome), "solved",
                "a negative boost is a direction rather than a refusal");
        t.equal(solved.result.factor_text, "1.25", "the factor is unchanged by the boost direction");
        t.check(contains_text(solved.assumptions,
                              "ship moves at beta = -0.6 c along the negative x axis of station"),
                "the context names the negative direction explicitly");
    }
    {
        // 1 - (1/2)^2 = 3/4 is not the square of a fraction, so gamma has no exact value here.
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "1/2");
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "inexact Lorentz factor",
                "a boost with no exact factor is refused rather than approximated");
        t.check(refused.result.outputs.empty(), "an inexact factor reports no value");
        t.check(contains_text(refused.rules, "physics.relativity.lorentz-factor"),
                "the refusal is recorded at the factor step that found it");
    }
    {
        // The neighbouring refusal on the other side of the envelope.
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "1");
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "superluminal speed",
                "a boost at the speed of light is refused");
        t.check(contains_text(refused.rules, "physics.relativity.check-boost"),
                "the speed limit is recorded as its own check");
        t.check(refused.has_unverified, "the speed check records its failure rather than hiding it");
    }
    {
        // A 10 m rod at rest in the ship measures 6 m from the station at beta = 4/5.
        RelativityProblem input = problem(RelativityRelation::LengthContraction, "4/5");
        input.knowns.push_back(known(RelativityVariable::ProperLength, "10"));
        const Run solved = run(input);
        t.equal(relativity_outcome_name(solved.result.outcome), "solved",
                "a proper length gives the contracted length");
        t.equal(reported(solved, RelativityVariable::ContractedLength), "6 m in station",
                "the contracted length is reported in the frame that measured it");
        t.equal(solved.result.factor_text, "5/3", "beta = 0.8 gives the exact fraction gamma = 5/3");
    }
    {
        // A proper length has to be positive, which an event coordinate does not.
        RelativityProblem input = problem(RelativityRelation::LengthContraction, "4/5");
        input.knowns.push_back(known(RelativityVariable::ProperLength, "-10"));
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "unphysical value",
                "a negative proper length is refused");
    }
    {
        // One event, both frames, with the spacetime interval as the independent check.
        RelativityProblem input = problem(RelativityRelation::LorentzTransformation, "3/5");
        input.knowns.push_back(known(RelativityVariable::EventPosition, "299792458"));
        input.knowns.push_back(known(RelativityVariable::EventTime, "1"));
        const Run solved = run(input);
        t.equal(relativity_outcome_name(solved.result.outcome), "solved",
                "an event transforms into the moving frame");
        t.equal(reported(solved, RelativityVariable::TransformedPosition),
                "149896229 m in ship",
                "the transformed position is reported in the moving frame");
        t.equal(reported(solved, RelativityVariable::TransformedTime), "0.5 s in ship",
                "the transformed event time is reported in the moving frame");
        t.equal(solved.equation,
                "[(xprime = (gamma * (x + (-(beta * (c * t)))))), "
                "((c * tprime) = (gamma * ((c * t) + (-(beta * x)))))]",
                "the model records both halves of the transformation with c in each");
        t.equal(solved.substituted,
                "[(149896229 = ((5 * (4^-1)) * (299792458 + (-((3 * (5^-1)) * (299792458 * 1)))))), "
                "((299792458 * (1 * (2^-1))) = ((5 * (4^-1)) * ((299792458 * 1) + "
                "(-((3 * (5^-1)) * 299792458)))))]",
                "the substituted transformation is a true equation on both halves");
        t.check(contains_text(solved.rules, "physics.relativity.check-invariant"),
                "the transformation is checked against the invariant interval");
    }
    {
        // A negative event position is an ordinary coordinate rather than an unphysical value.
        RelativityProblem input = problem(RelativityRelation::LorentzTransformation, "3/5");
        input.knowns.push_back(known(RelativityVariable::EventPosition, "-299792458"));
        input.knowns.push_back(known(RelativityVariable::EventTime, "1"));
        const Run solved = run(input);
        t.equal(relativity_outcome_name(solved.result.outcome), "solved",
                "an event behind the origin transforms like any other");
    }
    {
        // 1/2 c inside a frame moving at 1/2 c is 4/5 c, not c, and needs no exact gamma.
        RelativityProblem input = problem(RelativityRelation::VelocityAddition, "1/2");
        input.knowns.push_back(known(RelativityVariable::ObjectVelocity, "1/2"));
        const Run solved = run(input);
        t.equal(relativity_outcome_name(solved.result.outcome), "solved",
                "velocity addition does not need an exact Lorentz factor");
        t.equal(reported(solved, RelativityVariable::TransformedVelocity), "0.8 c in station",
                "the combined velocity stays below the speed of light");
        t.check(!contains_text(solved.rules, "physics.relativity.lorentz-factor"),
                "no factor step is recorded for a relation that does not use one");
    }
    {
        // Light stays at c in both frames, which is the one case the Galilean sum gets wrong.
        RelativityProblem input = problem(RelativityRelation::VelocityAddition, "3/5");
        input.knowns.push_back(known(RelativityVariable::ObjectVelocity, "1"));
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "superluminal speed",
                "an object given at the speed of light is outside this massive-object envelope");
    }
    {
        // A 938 MeV proton at beta = 3/5 carries gamma E0 and pc = gamma beta E0.
        RelativityProblem input = problem(RelativityRelation::EnergyMomentum, "3/5");
        input.knowns.push_back(known(RelativityVariable::RestEnergy, "938"));
        const Run solved = run(input);
        t.equal(relativity_outcome_name(solved.result.outcome), "solved",
                "a rest energy gives the total energy and momentum");
        t.equal(reported(solved, RelativityVariable::TotalEnergy), "1172.5 MeV in station",
                "the total energy is reported in the frame the particle moves in");
        t.equal(reported(solved, RelativityVariable::MomentumEnergy), "703.5 MeV in station",
                "the momentum energy is reported alongside it");
        t.equal(reported(solved, RelativityVariable::KineticEnergy), "234.5 MeV in station",
                "the kinetic energy is the total energy less the rest energy");
        t.equal(solved.substituted,
                "[((2345 * (2^-1)) = ((5 * (4^-1)) * 938)), "
                "((1407 * (2^-1)) = (((5 * (4^-1)) * (3 * (5^-1))) * 938)), "
                "((469 * (2^-1)) = ((2345 * (2^-1)) + (-938)))]",
                "every reported energy has its own recorded equation on exact values");
    }
    {
        // A measured given rounds once, at the end, and the rounding is compared against the exact
        // value before it is shown.
        RelativityProblem input = problem(RelativityRelation::EnergyMomentum, "3/5");
        input.knowns.push_back(known(RelativityVariable::RestEnergy, "938.0"));
        const Run solved = run(input);
        t.equal(relativity_outcome_name(solved.result.outcome), "solved",
                "a measured rest energy solves");
        t.equal(reported(solved, RelativityVariable::TotalEnergy), "1173 MeV in station",
                "the total energy is reported to the four figures the given carries");
        t.check(contains_text(solved.rules, "physics.relativity.significant-figures"),
                "the rounding is recorded as its own reporting step");
    }
    {
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        input.moving_frame.name.clear();
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "frame undeclared",
                "an unnamed frame is refused before any arithmetic");
    }
    {
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        input.moving_frame = input.rest_frame;
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "frame mismatch",
                "one frame cannot be both sides of its own boost");
    }
    {
        // The frames are the units layer's identity, so a vector's frame is one a boost can name.
        Vector velocity;
        std::string error;
        t.check(parse_vector("3 i + 4 j m/s", &velocity, &error), "a lab vector parses");
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        input.rest_frame = velocity.frame;
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run solved = run(input);
        t.equal(relativity_outcome_name(solved.result.outcome), "solved",
                "a boost measured against a vector's frame solves");
        bool reported_in_vector_frame = false;
        for (const RelativityOutput &output : solved.result.outputs) {
            if (output.variable == RelativityVariable::DilatedTime)
                reported_in_vector_frame = output.frame == velocity.frame;
        }
        t.check(reported_in_vector_frame, "the dilated time is reported in the vector's own frame");

        input.moving_frame = velocity.frame;
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "frame mismatch",
                "a vector's frame on both sides of the boost is one frame");
    }
    {
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "missing known",
                "a relation with no given is refused");
    }
    {
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        input.knowns.push_back(known(RelativityVariable::ProperTime, "5"));
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "duplicate known",
                "the same given twice is refused rather than taking the last one");
    }
    {
        // A proper time handed over in metres has the right shape and the wrong dimension.
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        RelativityKnown wrong = known(RelativityVariable::ProperTime, "4");
        wrong.quantity.unit = Unit();
        wrong.quantity.unit.text = "m";
        wrong.quantity.unit.dimension.length = 1;
        wrong.quantity.unit.scale.num = 1;
        wrong.quantity.unit.scale.den = 1;
        input.knowns.push_back(wrong);
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "dimension mismatch",
                "a given in the wrong dimension is refused");
    }
    {
        // A boost handed over as a speed in metres per second is not the fraction of c this reads.
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        input.boost.unit.text = "m/s";
        input.boost.unit.dimension.length = 1;
        input.boost.unit.dimension.time = -1;
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "dimension mismatch",
                "the boost is read as a fraction of c and refuses another unit");
    }
    {
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        input.knowns.push_back(known(RelativityVariable::EventPosition, "4"));
        const Run refused = run(input);
        t.equal(relativity_outcome_name(refused.result.outcome), "invalid problem",
                "a given that belongs to another relation is refused");
    }
    {
        Budget budget;
        budget.max_steps = 2;
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run stopped = run(input, budget);
        t.equal(relativity_outcome_name(stopped.result.outcome), "resource exceeded",
                "a step budget stops the derivation instead of finishing it");
        t.check(stopped.result.outputs.empty(), "a stopped derivation reports no value");
    }
    {
        PollAfter poll;
        poll.stop_at = 1;
        Budget budget;
        budget.poll = poll_after;
        budget.poll_context = &poll;
        RelativityProblem input = problem(RelativityRelation::TimeDilation, "3/5");
        input.knowns.push_back(known(RelativityVariable::ProperTime, "4"));
        const Run stopped = run(input, budget);
        t.equal(relativity_outcome_name(stopped.result.outcome), "cancelled",
                "cancellation is reported apart from exhaustion");
        t.check(stopped.result.outputs.empty(), "a cancelled derivation reports no value");
    }
    {
        // The shapes answer for themselves, so a variable belongs to exactly the relations that
        // read or report it.
        t.check(relativity_relation_reads(RelativityRelation::TimeDilation,
                                          RelativityVariable::ProperTime) &&
                    relativity_relation_reports(RelativityRelation::TimeDilation,
                                                RelativityVariable::DilatedTime) &&
                    !relativity_relation_reads(RelativityRelation::TimeDilation,
                                               RelativityVariable::RestEnergy),
                "the time-dilation shape names its own givens and results");
        t.check(relativity_variable_in_moving_frame(RelativityVariable::ProperTime) &&
                    !relativity_variable_in_moving_frame(RelativityVariable::DilatedTime),
                "the proper time belongs to the moving frame and the dilated time does not");
        t.equal(relativity_relation_written(RelativityRelation::VelocityAddition),
                "beta = (beta' + beta_boost) / (1 + beta' beta_boost)",
                "each relation states the form it applies");
        t.equal(relativity_outcome_name(RelativityOutcome::InexactLorentzFactor),
                "inexact Lorentz factor", "every outcome has a name of its own");
    }
}

}  // namespace nps
