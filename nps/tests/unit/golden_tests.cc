#include <string>
#include <utility>
#include <vector>

#include "nps/core/budgets.h"
#include "nps/steps/differentiate.h"
#include "nps/steps/integrate.h"
#include "nps/steps/integer.h"
#include "nps/physics/catch_up.h"
#include "nps/physics/circular_motion.h"
#include "nps/physics/density.h"
#include "nps/physics/forces.h"
#include "nps/physics/gravitation.h"
#include "nps/physics/oscillation.h"
#include "nps/physics/position_motion.h"
#include "nps/physics/ranking.h"
#include "nps/physics/relativity.h"
#include "nps/physics/kinematics.h"
#include "nps/physics/modern.h"
#include "nps/physics/optics.h"
#include "nps/physics/planar_kinematics.h"
#include "nps/physics/relative_motion.h"
#include "nps/physics/scalar_product.h"
#include "nps/physics/unit_conversion.h"
#include "nps/physics/vector_addition.h"
#include "nps/physics/vector_components.h"
#include "nps/physics/vector_cross.h"
#include "nps/physics/work.h"
#include "nps/steps/linear.h"
#include "nps/steps/quadratic.h"
#include "nps/steps/rearrange.h"
#include "nps/steps/trig.h"
#include "nps/steps/rewrite.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"
#include "golden/golden.h"

namespace nps {
namespace {

class GoldenSequenceBackend : public Backend {
  public:
    explicit GoldenSequenceBackend(std::vector<std::string> replies,
                                   std::vector<std::string> expected_commands = {})
        : replies_(std::move(replies)), expected_commands_(std::move(expected_commands)) {}

    bool eval(const std::string &command, std::string *out, std::string *error) override {
        commands.push_back(command);
        if (next_ >= replies_.size()) {
            *error = "no scripted reply";
            return false;
        }
        if (!expected_commands_.empty() &&
            (next_ >= expected_commands_.size() || command != expected_commands_[next_])) {
            *error = "unexpected backend request";
            return false;
        }
        *out = replies_[next_++];
        return true;
    }

    bool complete() const {
        return !expected_commands_.empty() && commands == expected_commands_ &&
               next_ == replies_.size();
    }

    std::vector<std::string> commands;

  private:
    std::vector<std::string> replies_;
    std::vector<std::string> expected_commands_;
    size_t next_ = 0;
};

// The header the fixture carries above the step record: what was asked and what came back. The
// steps alone would not show that a refusal refused, and PRD section 19.2 wants a golden to check
// the outcome as well as the working.
std::string header(const std::string &problem, const char *name, const char *outcome,
                   const std::string &result, const std::string &detail) {
    std::string out = "problem: ";
    out += problem;
    if (*name) {
        out += "\nvariable: ";
        out += name;
    }
    out += "\noutcome: ";
    out += outcome;
    out += "\n";
    if (!result.empty())
        out += "result: " + result + "\n";
    if (!detail.empty())
        out += "detail: " + detail + "\n";
    return out;
}

std::string solve_record(const std::string &equation, const char *name, const Budget &budget) {
    Arena arena;
    ParseResult parsed = parse(arena, equation);
    if (!parsed.ok())
        return std::string("the fixture's own input did not parse: ") + status_name(parsed.status);

    Derivation derivation;
    NodeId unknown = arena.symbol(name);
    SolveResult result = solve_linear(arena, derivation, parsed.root, unknown, budget);

    std::string answer;
    if (result.solution != kNoNode)
        answer = print(arena, result.solution);
    return header(equation, name, solve_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string integer_record(const char *expression, const Budget &budget) {
    Arena arena;
    ParseResult parsed = parse(arena, expression);
    if (!parsed.ok())
        return std::string("the fixture's own input did not parse: ") + status_name(parsed.status);
    Derivation derivation;
    const IntegerResult result = integer_method(arena, derivation, parsed.root, budget);
    const std::string answer = result.expression == kNoNode ? "" : print(arena, result.expression);
    return header(expression, "", integer_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string quadratic_record(const std::string &equation, const char *name, const Budget &budget,
                             bool by_formula = false) {
    Arena arena;
    ParseResult parsed = parse(arena, equation);
    if (!parsed.ok())
        return std::string("the fixture's own input did not parse: ") + status_name(parsed.status);

    Derivation derivation;
    NodeId unknown = arena.symbol(name);
    QuadraticResult result =
        by_formula ? solve_quadratic(arena, derivation, parsed.root, unknown, budget)
                   : solve_by_square_root(arena, derivation, parsed.root, unknown, budget);

    // Every root rather than the first. A fixture showing one of two would agree with the engine
    // that dropped the other, which is the failure these fixtures exist to catch.
    std::string answer;
    for (size_t i = 0; i < result.solutions.size(); ++i) {
        if (!answer.empty())
            answer += " and ";
        answer += print(arena, result.solutions[i]);
    }
    return header(equation, name, quadratic_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string trig_record(const char *expression, TrigGoal goal, const Budget &budget) {
    Arena arena;
    ParseResult parsed = parse(arena, expression);
    if (!parsed.ok())
        return std::string("the fixture's own input did not parse: ") + status_name(parsed.status);
    Derivation derivation;
    const TrigResult result = trig_rewrite(arena, derivation, parsed.root, goal, budget);
    const std::string answer = result.expression == kNoNode ? "" : print(arena, result.expression);
    return header(expression, "", trig_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string rearrange_record(const char *formula, const char *name, const Budget &budget) {
    Arena arena;
    ParseResult parsed = parse(arena, formula);
    if (!parsed.ok())
        return std::string("the fixture's own input did not parse: ") + status_name(parsed.status);

    Derivation derivation;
    NodeId variable = arena.symbol(name);
    RearrangeResult result = rearrange(arena, derivation, parsed.root, variable, budget);

    std::string answer;
    if (result.formula != kNoNode)
        answer = print(arena, result.formula);
    return header(formula, name, rearrange_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string rewrite_record(const char *expression, RewriteGoal goal, const Budget &budget) {
    Arena arena;
    ParseResult parsed = parse(arena, expression);
    if (!parsed.ok())
        return std::string("the fixture's own input did not parse: ") + status_name(parsed.status);

    Derivation derivation;
    RewriteResult result = rewrite(arena, derivation, parsed.root, goal, budget);

    std::string answer;
    if (result.expression != kNoNode)
        answer = print(arena, result.expression);
    return header(expression, rewrite_goal_name(goal), rewrite_outcome_name(result.outcome), answer,
                  result.detail) +
           render_derivation(arena, derivation);
}

std::string differentiate_record(const char *expression, const char *name, const Budget &budget) {
    Arena arena;
    ParseResult parsed = parse(arena, expression);
    if (!parsed.ok())
        return std::string("the fixture's own input did not parse: ") + status_name(parsed.status);

    Derivation derivation;
    NodeId variable = arena.symbol(name);
    DiffResult result = differentiate(arena, derivation, parsed.root, variable, budget);

    std::string answer;
    if (result.derivative != kNoNode)
        answer = print(arena, result.derivative);
    return header(expression, name, diff_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string integrate_record(const char *expression, const char *name, const Budget &budget,
                             Backend *backend = nullptr) {
    Arena arena;
    ParseResult parsed = parse(arena, expression);
    if (!parsed.ok())
        return std::string("the fixture's own input did not parse: ") + status_name(parsed.status);

    Derivation derivation;
    NodeId variable = arena.symbol(name);
    IntegrateResult result = integrate(arena, derivation, parsed.root, variable, budget, backend);

    std::string answer;
    if (result.antiderivative != kNoNode)
        answer = print(arena, result.antiderivative);
    return header(expression, name, integrate_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string kinematics_record(const char *text, const Budget &budget) {
    KinematicsProblem problem;
    std::string why;
    if (!parse_kinematics(text, &problem, &why))
        return std::string("the fixture's own input did not parse: ") + why;

    Arena arena;
    Derivation derivation;
    KinematicsResult result = solve_kinematics(arena, derivation, problem, budget);

    std::string answer;
    if (result.outcome == KinematicsOutcome::Solved)
        answer = result.value_text + " " + result.unit_text;
    return header(text, problem.unknown.c_str(), kinematics_outcome_name(result.outcome), answer,
                  result.detail) +
           render_derivation(arena, derivation);
}

std::string vector_addition_record(const char *first_text, const char *second_text,
                                   const Budget &budget) {
    VectorAdditionProblem problem;
    std::string why;
    if (!parse_vector(first_text, &problem.first, &why) ||
        !parse_vector(second_text, &problem.second, &why))
        return std::string("the fixture's own input did not parse: ") + why;
    Arena arena;
    Derivation derivation;
    VectorAdditionResult result = solve_vector_addition(arena, derivation, problem, budget);
    const std::string problem_text = std::string(first_text) + " + " + second_text;
    return header(problem_text, "sum", vector_addition_outcome_name(result.outcome),
                  result.value_text, result.detail) +
           render_derivation(arena, derivation);
}

std::string planar_kinematics_record(const char *velocity_text, const char *acceleration_text,
                                     const char *time_text, bool projectile,
                                     const Budget &budget) {
    PlanarKinematicsProblem problem;
    problem.body_name = "ball";
    std::string why;
    if (!parse_vector(velocity_text, &problem.initial_velocity, &why) ||
        !parse_vector(acceleration_text, &problem.acceleration, &why) ||
        !parse_quantity(time_text, &problem.elapsed_time, &why)) {
        return std::string("the fixture's own input did not parse: ") + why;
    }
    problem.initial_velocity.frame.name = "lab";
    problem.acceleration.frame.name = "lab";
    problem.projectile = projectile;
    Arena arena;
    Derivation derivation;
    const PlanarKinematicsResult result =
        solve_planar_kinematics(arena, derivation, problem, budget);
    const std::string problem_text = "ball; v0 " + std::string(velocity_text) + "; a " +
                                     acceleration_text + "; t " + time_text + "; frame = lab";
    std::string answer = result.displacement_text;
    if (!result.final_velocity_text.empty())
        answer += "; " + result.final_velocity_text;
    return header(problem_text, "displacement and final velocity",
                  planar_kinematics_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string vector_cross_record(const char *first_text, const char *second_text,
                                const Budget &budget) {
    VectorCrossProblem problem;
    std::string why;
    if (!parse_vector(first_text, &problem.first, &why) ||
        !parse_vector(second_text, &problem.second, &why))
        return std::string("the fixture's own input did not parse: ") + why;
    Arena arena;
    Derivation derivation;
    VectorCrossResult result = solve_vector_cross(arena, derivation, problem, budget);
    const std::string problem_text = std::string(first_text) + " cross " + second_text;
    return header(problem_text, "cross product", vector_cross_outcome_name(result.outcome),
                  result.value_text, result.detail) +
           render_derivation(arena, derivation);
}

std::string scalar_product_record(const char *first_text, const char *second_text, bool angle,
                                  const Budget &budget, Backend *giac = nullptr) {
    ScalarProductProblem problem;
    std::string why;
    if (!parse_vector(first_text, &problem.first, &why) ||
        !parse_vector(second_text, &problem.second, &why))
        return std::string("the fixture's own input did not parse: ") + why;
    problem.angle = angle;
    Arena arena;
    Derivation derivation;
    const ScalarProductResult result =
        solve_scalar_product(arena, derivation, problem, budget, giac);
    const std::string problem_text = std::string(first_text) + " dot " + second_text;
    std::string answer = result.value_text;
    if (!result.angle_text.empty())
        answer += "; " + result.angle_text;
    if (!result.numeric_angle_text.empty())
        answer += "; " + result.numeric_angle_text;
    return header(problem_text, "scalar product", scalar_product_outcome_name(result.outcome),
                  answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string relative_motion_record(const Budget &budget) {
    RelativeMotionProblem problem;
    problem.subject_name = "drone";
    problem.reference_name = "wind";
    std::string why;
    if (!parse_vector("(36, -18) km/h", &problem.subject_velocity, &why) ||
        !parse_vector("(3, 1) m/s", &problem.reference_velocity, &why)) {
        return "the fixture's own input did not parse: " + why;
    }
    problem.subject_velocity.frame.name = "ground";
    problem.reference_velocity.frame.name = "ground";
    GoldenSequenceBackend backend({"7", "-6"});
    Arena arena;
    Derivation derivation;
    const RelativeMotionResult result =
        solve_relative_motion(arena, derivation, problem, budget, &backend);
    return header("drone (36, -18) km/h; wind (3, 1) m/s; frame = ground",
                  "drone velocity relative to wind", relative_motion_outcome_name(result.outcome),
                  result.value_text +
                      (result.interpretation.empty() ? "" : "; " + result.interpretation),
                  result.detail) +
           render_derivation(arena, derivation);
}

std::string unit_conversion_record(const char *source_text, const char *target_text,
                                   const Budget &budget) {
    UnitConversionProblem problem;
    const UnitConversionParseResult parsed =
        parse_unit_conversion_problem(source_text, target_text, &problem);
    if (!parsed.ok())
        return std::string("the fixture's own input did not parse: ") + parsed.detail;

    Arena arena;
    Derivation derivation;
    const UnitConversionResult result = solve_unit_conversion(arena, derivation, problem, budget);
    const std::string problem_text = std::string(source_text) + " to " + target_text;
    return header(problem_text, "converted quantity", unit_conversion_outcome_name(result.outcome),
                  result.value_text, result.detail) +
           render_derivation(arena, derivation);
}

// The modern family reads its own eV, nm, u and MeV working units, which the unit table does not
// carry, so the fixture attaches the declared unit the way the bridge does.
Quantity modern_declared(ModernVariable variable, const char *text) {
    Quantity parsed;
    std::string why;
    parse_quantity(text, &parsed, &why);
    Unit unit;
    unit.text = modern_variable_unit(variable);
    switch (variable) {
        case ModernVariable::Wavelength: unit.dimension.length = 1; break;
        case ModernVariable::MassDefect: unit.dimension.mass = 1; break;
        default:
            unit.dimension.length = 2;
            unit.dimension.mass = 1;
            unit.dimension.time = -2;
            break;
    }
    unit.scale.num = 1;
    unit.scale.den = 1;
    parsed.unit = unit;
    return parsed;
}

std::string modern_record(ModernRelation relation, ModernVariable unknown,
                          const std::vector<std::pair<ModernVariable, const char *> > &knowns,
                          const Budget &budget) {
    ModernProblem problem;
    problem.relation = relation;
    problem.unknown = unknown;
    std::string problem_text = std::string("find ") + modern_variable_name(unknown);
    for (size_t i = 0; i < knowns.size(); ++i) {
        ModernKnown entry;
        entry.variable = knowns[i].first;
        entry.quantity = modern_declared(knowns[i].first, knowns[i].second);
        problem.knowns.push_back(entry);
        problem_text += std::string("; ") + modern_variable_name(knowns[i].first) + " = " +
                        knowns[i].second + " " + modern_variable_unit(knowns[i].first);
    }
    Arena arena;
    Derivation derivation;
    const ModernResult result = solve_modern(arena, derivation, problem, budget);
    std::string answer;
    if (result.outcome == ModernOutcome::Solved)
        answer = result.value_text + " " + result.unit_text;
    return header(problem_text, modern_variable_name(unknown),
                  modern_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string density_record(DensityVariable unknown, DensityVariable first_variable,
                           const char *first_text, DensityVariable second_variable,
                           const char *second_text, const Budget &budget) {
    DensityKnown first;
    first.variable = first_variable;
    DensityKnown second;
    second.variable = second_variable;
    std::string why;
    if (!parse_quantity(first_text, &first.quantity, &why))
        return std::string("the fixture's own input did not parse: ") + why;
    if (!parse_quantity(second_text, &second.quantity, &why))
        return std::string("the fixture's own input did not parse: ") + why;

    DensityProblem problem;
    problem.unknown = unknown;
    problem.knowns.push_back(first);
    problem.knowns.push_back(second);
    Arena arena;
    Derivation derivation;
    const DensityResult result = solve_density(arena, derivation, problem, budget);
    const std::string problem_text =
        std::string("find ") + density_variable_name(unknown) + "; " +
        density_variable_name(first_variable) + " = " + first_text + "; " +
        density_variable_name(second_variable) + " = " + second_text;
    std::string answer;
    if (result.outcome == DensityOutcome::Solved)
        answer = result.value_text + " " + result.unit_text;
    return header(problem_text, density_variable_name(unknown), density_outcome_name(result.outcome),
                  answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string work_record(const char *force_text, const char *displacement_text,
                        WorkForceProfile force_profile, const Budget &budget) {
    WorkProblem problem;
    std::string why;
    if (!parse_vector(force_text, &problem.force, &why) ||
        !parse_vector(displacement_text, &problem.displacement, &why))
        return std::string("the fixture's own input did not parse: ") + why;
    problem.force.frame.name = "lab";
    problem.displacement.frame.name = "lab";
    problem.force_profile = force_profile;

    Arena arena;
    Derivation derivation;
    const WorkResult result = solve_work(arena, derivation, problem, budget);
    const std::string problem_text =
        std::string("force = ") + force_text + "; displacement = " + displacement_text +
        "; force profile = " + work_force_profile_name(force_profile) + "; frame = lab";
    std::string answer;
    if (result.outcome == WorkOutcome::Solved)
        answer = result.value_text + " " + result.unit_text;
    const std::string detail = result.detail.empty() ? result.interpretation : result.detail;
    return header(problem_text, "W", work_outcome_name(result.outcome), answer, detail) +
           render_derivation(arena, derivation);
}

// The optics problems are typed rather than parsed, so the fixture states the relation, the
// unknown and each given in the spelling the engine reports it back under.
std::string optics_record(OpticsRelation relation, OpticsVariable unknown,
                          const std::vector<std::pair<OpticsVariable, const char *> > &knowns,
                          const Budget &budget) {
    OpticsProblem problem;
    problem.relation = relation;
    problem.unknown = unknown;
    std::string problem_text = std::string(optics_relation_name(relation)) + "; find " +
                               optics_variable_name(unknown);
    std::string why;
    for (size_t i = 0; i < knowns.size(); ++i) {
        OpticsKnown entry;
        entry.variable = knowns[i].first;
        if (!parse_quantity(knowns[i].second, &entry.quantity, &why))
            return std::string("the fixture's own input did not parse: ") + why;
        problem.knowns.push_back(entry);
        problem_text += std::string("; ") + optics_variable_name(knowns[i].first) + " = " +
                        knowns[i].second;
    }
    Arena arena;
    Derivation derivation;
    const OpticsResult result = solve_optics(arena, derivation, problem, budget);
    std::string answer;
    if (result.outcome == OpticsOutcome::Solved) {
        answer = result.value_text;
        // si_unit_text spells a dimensionless SI unit as "1", which lua_module.cc's l_optics
        // already suppresses so the fixture records the same answer the bridge reports.
        const std::string display_unit =
            result.unit_text == "1" ? std::string() : result.unit_text;
        if (!display_unit.empty())
            answer += " " + display_unit;
        if (result.has_magnification)
            answer += ", magnification " + result.magnification_text;
    }
    if (result.has_critical_sine)
        answer += (answer.empty() ? "" : "; ") + std::string("critical sine ") +
                  result.critical_sine_text;
    return header(problem_text, optics_variable_name(unknown), optics_outcome_name(result.outcome),
                  answer, result.detail) +
           render_derivation(arena, derivation);
}

// Gravitation, oscillation and wave all solve through solve_relation, so one record serves three.
std::string relation_record(const RelationModel &model, const RelationProblem &problem,
                            const std::string &problem_text,
                            RelationResult (*solve)(Arena &, Derivation &, const RelationProblem &,
                                                    const Budget &),
                            const Budget &budget) {
    Arena arena;
    Derivation derivation;
    const RelationResult result = solve(arena, derivation, problem, budget);
    std::string answer;
    if (result.outcome == RelationOutcome::Solved) {
        answer = result.value_text;
        if (!result.unit_text.empty() && result.unit_text != "1")
            answer += " " + result.unit_text;
    }
    return header(problem_text, relation_term(model, problem.unknown).name,
                  relation_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string circular_motion_record(const CircularMotionProblem &problem,
                                   const std::string &problem_text, const Budget &budget) {
    Arena arena;
    Derivation derivation;
    const CircularMotionResult result = solve_circular_motion(arena, derivation, problem, budget);
    std::string answer;
    if (result.outcome == RelationOutcome::Solved) {
        answer = result.unknown_result.value_text + " " + result.unknown_result.unit_text;
        // The acceleration comes back beside all three unknowns, so dropping it pins half the answer.
        if (result.acceleration_result.outcome == RelationOutcome::Solved)
            answer += ", centripetal acceleration " + result.acceleration_result.value_text + " " +
                      result.acceleration_result.unit_text;
    }
    return header(problem_text, circular_motion_variable_name(problem.unknown),
                  relation_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

// The unit table carries neither fractions of c nor MeV, and parse_quantity cannot read a fraction.
Quantity relativity_declared(RelativityVariable variable, int64_t num, int64_t den) {
    Quantity value;
    value.value.num = num;
    value.value.den = den;
    value.unit.text = relativity_variable_unit(variable);
    value.unit.dimension = relativity_variable_dimension(variable);
    value.unit.scale.num = 1;
    value.unit.scale.den = 1;
    return value;
}

Quantity relativity_boost(int64_t num, int64_t den) {
    Quantity value;
    value.value.num = num;
    value.value.den = den;
    value.unit.text = "c";
    value.unit.scale.num = 1;
    value.unit.scale.den = 1;
    return value;
}

RelativityProblem relativity_problem(RelativityRelation relation, int64_t beta_num,
                                     int64_t beta_den) {
    RelativityProblem input;
    input.relation = relation;
    input.rest_frame = {"station"};
    input.moving_frame = {"ship"};
    input.boost = relativity_boost(beta_num, beta_den);
    return input;
}

std::string relativity_record(const RelativityProblem &problem, const std::string &problem_text,
                              const Budget &budget) {
    Arena arena;
    Derivation derivation;
    const RelativityResult result = solve_relativity(arena, derivation, problem, budget);
    std::string answer;
    for (size_t i = 0; i < result.outputs.size(); ++i) {
        if (!answer.empty())
            answer += "; ";
        answer += std::string(relativity_variable_symbol(result.outputs[i].variable)) + " = " +
                  result.outputs[i].value_text;
        if (!result.outputs[i].unit_text.empty())
            answer += " " + result.outputs[i].unit_text;
        answer += " in " + result.outputs[i].frame.name;
    }
    if (result.has_factor)
        answer += (answer.empty() ? "" : "; ") + std::string("gamma ") + result.factor_text;
    return header(problem_text, relativity_relation_name(problem.relation),
                  relativity_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string position_motion_record(const PositionMotionProblem &problem,
                                   const std::string &problem_text, const Budget &budget) {
    Arena arena;
    Derivation derivation;
    const PositionMotionResult result =
        solve_position_motion(arena, derivation, problem, budget, nullptr);
    std::string answer;
    if (result.outcome == PositionMotionOutcome::Solved) {
        answer = "average velocity (" + rational_text(result.average_velocity.x) + ", " +
                 rational_text(result.average_velocity.y) + ") " +
                 result.average_velocity.unit.text + "; instantaneous velocity (" +
                 rational_text(result.instantaneous_velocity.x) + ", " +
                 rational_text(result.instantaneous_velocity.y) + ") " +
                 result.instantaneous_velocity.unit.text + "; instantaneous acceleration (" +
                 rational_text(result.instantaneous_acceleration.x) + ", " +
                 rational_text(result.instantaneous_acceleration.y) + ") " +
                 result.instantaneous_acceleration.unit.text;
    }
    return header(problem_text, "", position_motion_outcome_name(result.outcome), answer,
                  result.detail) +
           render_derivation(arena, derivation);
}

std::string ranking_record(const RankingModel &model, const RankingProblem &problem,
                           const std::string &problem_text, const Budget &budget) {
    Derivation derivation;
    const RankingResult result = solve_ranking(derivation, model, problem, budget);
    Arena arena;
    return header(problem_text, model.quantity_name, ranking_outcome_name(result.outcome),
                  result.outcome == RankingOutcome::Solved ? result.detail : std::string(),
                  result.outcome == RankingOutcome::Solved ? std::string() : result.detail) +
           render_derivation(arena, derivation);
}

// Issue 255, whose catalog block claims fixture evidence these fixtures have to exist to support.
std::string forces_record(const ForcesProblem &problem, const std::string &problem_text,
                          const Budget &budget) {
    Arena arena;
    Derivation derivation;
    const ForcesResult result = solve_forces(arena, derivation, problem, budget);
    std::string answer;
    if (result.has_value) {
        answer = result.value_text;
        if (!result.unit_text.empty())
            answer += " " + result.unit_text;
    }
    return header(problem_text, forces_unknown_name(problem.unknown),
                  forces_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

ForcesProblem forces_base(ForcesUnknown unknown) {
    ForcesProblem problem;
    problem.body = "block";
    problem.support = "table";
    std::string why;
    parse_quantity("2 kg", &problem.mass, &why);
    parse_quantity("10 m/s^2", &problem.gravity, &why);
    problem.unknown = unknown;
    return problem;
}

bool make_catch_up_body(const char *name, const char *position, const char *velocity,
                        const char *start, CatchUpBody *body, std::string *why) {
    body->name = name;
    body->frame.name = "track";
    return parse_quantity(position, &body->position_at_start, why) &&
           parse_quantity(velocity, &body->velocity_at_start, why) &&
           parse_quantity(start, &body->start_time, why);
}

std::string catch_up_record(const CatchUpProblem &problem, const std::string &problem_text,
                            const Budget &budget) {
    Arena arena;
    Derivation derivation;
    const CatchUpResult result = solve_catch_up(arena, derivation, problem, budget);
    std::string answer;
    if (result.outcome == CatchUpOutcome::Solved) {
        answer = "time = " + result.event_time_text + " " + result.time_unit_text +
                 ", position = " + result.event_position_text + " " +
                 result.position_unit_text;
    }
    return header(problem_text, "meeting event", catch_up_outcome_name(result.outcome), answer,
                  result.detail) +
           render_derivation(arena, derivation);
}

std::string catch_up_delayed_start_record(const Budget &budget) {
    CatchUpProblem problem;
    std::string why;
    if (!make_catch_up_body("Atlas", "0 m", "2 m/s", "0 s", &problem.first, &why) ||
        !make_catch_up_body("Boreal", "0 m", "4 m/s", "5 s", &problem.second, &why)) {
        return std::string("the fixture's own input did not parse: ") + why;
    }
    return catch_up_record(
        problem,
        "Atlas starts at 0 m with 2 m/s at 0 s, Boreal starts at 0 m with 4 m/s at 5 s, frame = track",
        budget);
}

std::string catch_up_measured_report_record(const Budget &budget) {
    CatchUpProblem problem;
    std::string why;
    if (!make_catch_up_body("Atlas", "0 m", "2.00 m/s", "0 s", &problem.first, &why) ||
        !make_catch_up_body("Boreal", "0 m", "4.00 m/s", "5.00 s", &problem.second, &why)) {
        return std::string("the fixture's own input did not parse: ") + why;
    }
    return catch_up_record(
        problem,
        "Atlas starts at 0 m with 2.00 m/s at 0 s, Boreal starts at 0 m with 4.00 m/s at 5.00 s, frame = track",
        budget);
}

std::string catch_up_before_domain_record(const Budget &budget) {
    CatchUpProblem problem;
    std::string why;
    if (!make_catch_up_body("First", "0 m", "1 m/s", "0 s", &problem.first, &why) ||
        !make_catch_up_body("Second", "10 m", "2 m/s", "5 s", &problem.second, &why)) {
        return std::string("the fixture's own input did not parse: ") + why;
    }
    return catch_up_record(
        problem,
        "First starts at 0 m with 1 m/s at 0 s, Second starts at 10 m with 2 m/s at 5 s, frame = track",
        budget);
}

std::string catch_up_nonlinear_refusal_record(const Budget &budget) {
    CatchUpProblem problem;
    std::string why;
    if (!make_catch_up_body("Atlas", "0 m", "2 m/s", "0 s", &problem.first, &why) ||
        !make_catch_up_body("Boreal", "0 m", "4 m/s", "5 s", &problem.second, &why) ||
        !parse_quantity("1 m/s^2", &problem.first.acceleration, &why)) {
        return std::string("the fixture's own input did not parse: ") + why;
    }
    problem.first.motion = CatchUpMotionModel::ConstantAcceleration;
    return catch_up_record(
        problem,
        "Atlas uses constant acceleration 1 m/s^2, Boreal uses constant velocity, frame = track",
        budget);
}

std::string exact_vector_components_record(const Budget &budget) {
    Arena arena;
    const ParseResult magnitude = parse(arena, "10");
    const ParseResult angle = parse(arena, "30");
    if (!magnitude.ok() || !angle.ok())
        return "the fixture's own input did not parse";

    MagnitudeAngleExpr input;
    input.magnitude = magnitude.root;
    input.angle = angle.root;
    input.angle_unit = AngleUnit::Degrees;
    input.frame.name = "lab";
    input.unit.text = "m/s";
    input.unit.dimension = {1, 0, -1};
    input.unit.scale = {1, 1};
    GoldenSequenceBackend backend(
        {"sqrt(3)/2", "0", "5*sqrt(3)", "0", "1/2", "0", "5", "0"});
    Derivation derivation;
    const VectorComponentsResult result =
        magnitude_angle_to_components(arena, derivation, input, backend, budget);
    std::string answer;
    if (result.has_components)
        answer = "(" + print(arena, result.components.x) + ", " +
                 print(arena, result.components.y) + ") " + result.components.unit.text;
    return header("magnitude = 10 m/s, angle = 30 degrees, frame = lab", "components",
                  vector_components_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string negative_quadrant_polar_record(const Budget &budget) {
    Vector input;
    input.x = {-3, 1};
    input.y = {-4, 1};
    input.rank = 2;
    input.frame.name = "ground";
    input.unit.text = "m";
    input.unit.dimension = {1, 0, 0};
    input.unit.scale = {1, 1};
    GoldenSequenceBackend backend(
        {"0", "atan2(-4,-3)", "0", "atan2(-4,-3)*180/pi", "0"});
    Arena arena;
    Derivation derivation;
    const VectorComponentsResult result = components_to_magnitude_angle(
        arena, derivation, input, AngleUnit::Degrees, backend, budget);
    std::string answer;
    if (result.has_polar)
        answer = "magnitude = " + print(arena, result.polar.magnitude) + " " +
                 result.polar.unit.text + ", angle = " + print(arena, result.polar.angle) +
                 " degrees";
    return header("components = (-3, -4) m, frame = ground", "magnitude and direction",
                  vector_components_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

std::string spherical_polar_record(const Budget &budget) {
    Vector input;
    input.x = {0, 1};
    input.y = {3, 1};
    input.z = {4, 1};
    input.rank = 3;
    input.frame.name = "lab";
    input.unit.text = "m";
    input.unit.dimension = {1, 0, 0};
    input.unit.scale = {1, 1};
    GoldenSequenceBackend backend({"0", "atan2(3,0)", "0", "atan2(3,4)", "0"});
    Arena arena;
    Derivation derivation;
    const VectorComponentsResult result = components_to_magnitude_angle(
        arena, derivation, input, AngleUnit::Radians, backend, budget);
    std::string answer;
    if (result.has_polar)
        answer = "magnitude = " + print(arena, result.polar.magnitude) + " " +
                 result.polar.unit.text + ", polar angle = " +
                 print(arena, result.polar.polar_angle) + " radians from z, azimuth = " +
                 print(arena, result.polar.angle) + " radians";
    return header("components = (0, 3, 4) m, frame = lab", "magnitude and direction",
                  vector_components_outcome_name(result.outcome), answer, result.detail) +
           render_derivation(arena, derivation);
}

// One step is under what any real solve records, so the meter halts before anything landed. What
// survives is decided by whether the halt caught the engine mid-composite with a completed child
// below, so several of these fixtures show no steps and integrate_step_budget_halt shows two: the
// budget is one step and the power rule is one step. PERF-009 is not the reason for either. It
// forbids a partly accepted or mislabeled derivation, and a prefix labelled resource limit reached
// is neither.
Budget one_step() {
    Budget budget;
    budget.max_steps = 1;
    return budget;
}

// Enough for the composite parent and one child, so the halt lands with checked work under an
// unfilled parent. This is the shape STEP-025 is about and the one the engines used to throw away.
Budget two_steps() {
    Budget budget;
    budget.max_steps = 2;
    return budget;
}

std::vector<std::string> lines_of(const std::string &text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t stop = text.find('\n', start);
        if (stop == std::string::npos) {
            if (start < text.size())
                out.push_back(text.substr(start));
            break;
        }
        out.push_back(text.substr(start, stop - start));
        start = stop + 1;
    }
    return out;
}

std::string unindented(const std::string &line) {
    size_t first = 0;
    while (first < line.size() && line[first] == ' ')
        ++first;
    return line.substr(first);
}

// The payload after "<field>: " when the line carries that field, and an empty string otherwise.
// A scan rather than a pattern, and it takes the whole prefix at once so a step whose text happens
// to contain the word cannot be read as a second field.
bool field_of(const std::string &line, const char *name, std::string *out) {
    const std::string prefix = std::string(name) + ": ";
    const std::string bare = unindented(line);
    if (bare.compare(0, prefix.size(), prefix) != 0)
        return false;
    *out = bare.substr(prefix.size());
    return true;
}

// The fixture writer, held to the same discipline the viewer is: every explanation it emits has to
// be a string some step is carrying, and the count of the fuller ones has to be the count of steps
// that filled that field. This is not STEP-016, which is about what a student is shown, and
// render_derivation is reached only from this file. The viewer's own row is in ui_smoke_v4.lua.
void test_golden_writer_projects_the_trace(TestSink &t) {
    Arena arena;
    Derivation derivation;
    ParseResult problem = parse(arena, "2x + 5 = 13");
    solve_linear(arena, derivation, problem.root, arena.symbol("x"));

    const size_t steps_before = derivation.size();
    const std::string rendered = render_derivation(arena, derivation);
    t.check(derivation.size() == steps_before && steps_before > 0,
            "the golden writer renders a derivation of " + integer_text(static_cast<int64_t>(steps_before)) +
                " steps and adds none of its own");
    t.check(rendered == render_derivation(arena, derivation),
            "and rendering it again gives the same text, so nothing was recomputed");

    size_t short_lines = 0;
    size_t detail_lines = 0;
    std::vector<std::string> orphans;
    const std::vector<std::string> lines = lines_of(rendered);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string shown;
        const bool is_short = field_of(lines[i], "explanation", &shown);
        const bool is_detail = !is_short && field_of(lines[i], "detail", &shown);
        if (!is_short && !is_detail)
            continue;
        bool carried = false;
        for (size_t s = 0; s < derivation.size(); ++s) {
            const Step &step = derivation.at(static_cast<StepId>(s));
            if (shown == (is_short ? step.explanation_short : step.explanation_detailed))
                carried = true;
        }
        if (!carried)
            orphans.push_back(shown);
        if (is_short)
            ++short_lines;
        else
            ++detail_lines;
    }

    size_t steps_with_detail = 0;
    for (size_t s = 0; s < derivation.size(); ++s) {
        if (!derivation.at(static_cast<StepId>(s)).explanation_detailed.empty())
            ++steps_with_detail;
    }

    t.check(orphans.empty(),
            orphans.empty()
                ? "every explanation the golden writer emits is one a step is carrying"
                : "the golden writer emitted an explanation no step carries: " + orphans[0]);
    t.check(detail_lines == steps_with_detail,
            "in the golden text the fuller explanation appears exactly for the steps that filled "
            "it, so a step that says nothing more is left saying nothing more");
    t.check(short_lines > 0 && detail_lines > 0,
            "and both the short and the fuller explanation reach the fixture at all");
}

bool cancel_at_once(void *) {
    return true;
}

Budget cancelling() {
    Budget budget;
    budget.poll = cancel_at_once;
    return budget;
}

// One branch, against a problem needing two. derivation.h:246 names the hazard this pins: a split
// whose second case is refused leaves the first claiming an exhaustive set of one.
Budget one_branch() {
    Budget budget;
    budget.max_branches = 1;
    return budget;
}

}  // namespace

void run_golden_tests(TestSink &t) {
    check_golden(t, "linear_worked_example", solve_record("2x + 5 = 13", "x", Budget()));
    check_golden(t, "linear_no_solution", solve_record("x + 1 = x + 2", "x", Budget()));
    check_golden(t, "linear_all_values", solve_record("x + 1 = 1 + x", "x", Budget()));
    check_golden(t, "linear_not_linear", solve_record("x^2 = 4", "x", Budget()));
    check_golden(t, "linear_not_an_equation", solve_record("2x + 5", "x", Budget()));
    check_golden(t, "linear_step_budget_halt", solve_record("2x + 5 = 13", "x", one_step()));
    check_golden(t, "linear_cancelled", solve_record("x = 4", "x", cancelling()));
    check_golden(t, "linear_verification_failed",
                 solve_record("4000000000x + 3999999999 = 0", "x", Budget()));

    // The first branching rule, one fixture per arm of the split so no arm of the STEP-024 gate is
    // left with nothing to judge. Two roots, a repeated root, an empty solution set over the reals,
    // a square with no exact root, an equation with a term of degree one, and a budget that runs out
    // between the two cases of one split.
    check_golden(t, "quadratic_two_roots", quadratic_record("x^2 = 4", "x", Budget()));
    check_golden(t, "quadratic_scaled_two_roots", quadratic_record("3x^2 - 12 = 0", "x", Budget()));
    check_golden(t, "quadratic_fractional_roots", quadratic_record("4x^2 = 9", "x", Budget()));
    check_golden(t, "quadratic_repeated_root", quadratic_record("x^2 = 0", "x", Budget()));
    check_golden(t, "quadratic_no_real_solution", quadratic_record("x^2 + 4 = 0", "x", Budget()));
    check_golden(t, "quadratic_outside_envelope", quadratic_record("x^2 = 5", "x", Budget()));
    check_golden(t, "quadratic_has_a_linear_term",
                 quadratic_record("x^2 + x = 6", "x", Budget()));
    check_golden(t, "quadratic_not_an_equation", quadratic_record("x^2 + 4", "x", Budget()));
    check_golden(t, "quadratic_branch_budget_halt", quadratic_record("x^2 = 4", "x", one_branch()));
    check_golden(t, "quadratic_step_budget_halt", quadratic_record("x^2 = 4", "x", one_step()));
    check_golden(t, "quadratic_cancelled", quadratic_record("x^2 = 4", "x", cancelling()));

    // The formula: two roots, a repeated one, an empty set and a discriminant with no exact root.
    check_golden(t, "quadratic_formula_two_roots",
                 quadratic_record("3x^2 + 10x - 88 = 0", "x", Budget(), true));
    check_golden(t, "quadratic_formula_repeated_root",
                 quadratic_record("x^2 + 2x + 1 = 0", "x", Budget(), true));
    check_golden(t, "quadratic_formula_no_real_solution",
                 quadratic_record("x^2 + x + 1 = 0", "x", Budget(), true));
    check_golden(t, "quadratic_formula_outside_envelope",
                 quadratic_record("x^2 + x - 1 = 0", "x", Budget(), true));

    check_golden(t, "rearrange_kinematics_formula", rearrange_record("v = u + a*t", "t", Budget()));
    check_golden(t, "rearrange_reciprocal", rearrange_record("R = 1/x", "x", Budget()));
    check_golden(t, "rearrange_negated_variable", rearrange_record("y = -x", "x", Budget()));
    check_golden(t, "rearrange_first_power", rearrange_record("y = x^1", "x", Budget()));
    check_golden(t, "rearrange_even_power_refused", rearrange_record("y = x^2", "x", Budget()));
    check_golden(t, "rearrange_repeated_variable", rearrange_record("y = x + x", "x", Budget()));
    check_golden(t, "rearrange_step_budget_halt", rearrange_record("v = u + a*t", "t", one_step()));
    check_golden(t, "trig_angle_sum", trig_record("sin(x+y)", TrigGoal::Expand, Budget()));
    check_golden(t, "trig_triple_angle", trig_record("sin(3x)", TrigGoal::Expand, Budget()));
    check_golden(t, "trig_odd_even", trig_record("sin(-x) + cos(-2x)", TrigGoal::Expand, Budget()));
    check_golden(t, "trig_pythagorean", trig_record("sin(x)^2 + cos(x)^2", TrigGoal::Collect, Budget()));
    check_golden(t, "trig_half_angle_collect", trig_record("2*sin(x)^2 + cos(2x)", TrigGoal::Collect, Budget()));
    check_golden(t, "trig_double_angle_product", trig_record("sin(x)*cos(x)", TrigGoal::Collect, Budget()));
    check_golden(t, "trig_outside_envelope", trig_record("sin(x+1)", TrigGoal::Expand, Budget()));

    check_golden(t, "rewrite_simplify_arithmetic",
                 rewrite_record("2 + 3*4", RewriteGoal::Simplify, Budget()));
    check_golden(t, "rewrite_collect_like_terms",
                 rewrite_record("3x + 5x + -2x", RewriteGoal::Simplify, Budget()));
    check_golden(t, "rewrite_expand_product",
                 rewrite_record("(x + 1)(x + 2)", RewriteGoal::Expand, Budget()));
    check_golden(t, "rewrite_expand_square",
                 rewrite_record("(x + 1)^2", RewriteGoal::Expand, Budget()));
    check_golden(t, "rewrite_factor_quadratic",
                 rewrite_record("x^2 + 3x + 2", RewriteGoal::Factor, Budget()));
    check_golden(t, "rewrite_factor_common_then_pair",
                 rewrite_record("2x^2 + 8x + 6", RewriteGoal::Factor, Budget()));
    check_golden(t, "rewrite_factor_written_zero",
                 rewrite_record("x^2 + 4x + 0", RewriteGoal::Factor, Budget()));
    check_golden(t, "rewrite_difference_of_squares",
                 rewrite_record("x^2 + -4", RewriteGoal::Factor, Budget()));
    check_golden(t, "rewrite_decimal_refused",
                 rewrite_record("1.5 + 2", RewriteGoal::Simplify, Budget()));
    check_golden(t, "rewrite_step_budget_halt",
                 rewrite_record("2 + 3*4", RewriteGoal::Simplify, one_step()));

    check_golden(t, "differentiate_product", differentiate_record("x^2*sin(x)", "x", Budget()));
    check_golden(t, "differentiate_quotient", differentiate_record("sin(x)/x", "x", Budget()));
    check_golden(t, "differentiate_chain", differentiate_record("sin(x^2 + 1)", "x", Budget()));
    check_golden(t, "differentiate_constant_multiple",
                 differentiate_record("3*x^4", "x", Budget()));
    check_golden(t, "differentiate_unsupported", differentiate_record("2^x", "x", Budget()));
    check_golden(t, "differentiate_unsupported_both",
                 differentiate_record("x^x", "x", Budget()));
    // The prefix PRD 808 keeps, pinned as the reader sees it rather than as a step count. What the
    // sum rule completes itself with is the shape a student reads, so it belongs in a fixture.
    check_golden(t, "differentiate_unsupported_partway",
                 differentiate_record("sin(x) + x^x", "x", Budget()));
    check_golden(t, "differentiate_step_budget_halt",
                 differentiate_record("x^2*sin(x)", "x", one_step()));
    // STEP-025 through a composite rule. The product rule and the first factor's derivative land,
    // then the meter stops, and what the reader gets is the product rule completed with the factor
    // that was taken and the other left as a derivative-of. Pinned as text because the argument for
    // keeping it is about what a student reads, not about a step count.
    check_golden(t, "differentiate_halt_inside_product",
                 differentiate_record("x^2*sin(x)", "x", two_steps()));

    check_golden(t, "integrate_power", integrate_record("x^2", "x", Budget()));
    check_golden(t, "integrate_substitution", integrate_record("sin(2x) + 1/x", "x", Budget()));
    {
        GoldenSequenceBackend backend(
            {"0"},
            {"simplify(((-1+(a*(a)^(-1)*ln((b+(a*x))))+(a*(b+(a*x))*(a)^(-1)*"
             "((b+(a*x)))^(-1)))+(-(ln((b+(a*x)))))))"});
        check_golden(t, "integrate_logarithm_affine",
                     integrate_record("ln(a*x+b)", "x", Budget(), &backend));
        t.check(backend.complete(),
                "the affine logarithm golden checks the exact Giac request and reply");
    }
    {
        GoldenSequenceBackend backend(
            {"0"},
            {"simplify(((-1+((1+(-2*t))*((1+(-2*t)))^(-1))+ln((1+(-2*t))))+"
             "(-(ln((1+(-2*t)))))))"});
        check_golden(t, "integrate_logarithm_negative_coefficient",
                     integrate_record("ln(1-2*t)", "t", Budget(), &backend));
        t.check(backend.complete(),
                "the negative-coefficient logarithm golden checks the exact Giac request and reply");
    }
    check_golden(t, "integrate_logarithm_unverified", integrate_record("ln(x)", "x", Budget()));
    {
        GoldenSequenceBackend backend(
            {"0"},
            {"simplify(((2*((x*(2)^(-1)*(sqrt(x))^(-1))+sqrt(x))*(3)^(-1))+"
             "(-(sqrt(x)))))"});
        check_golden(t, "integrate_square_root", integrate_record("sqrt(x)", "x", Budget(), &backend));
        t.check(backend.complete(),
                "the square-root golden checks the exact Giac request and reply");
    }
    check_golden(t, "integrate_unsupported", integrate_record("x*sin(x)", "x", Budget()));
    check_golden(t, "integrate_unsupported_partway",
                 integrate_record("x^2 + tan(x)", "x", Budget()));
    check_golden(t, "integrate_step_budget_halt", integrate_record("x^2", "x", one_step()));
    check_golden(t, "integrate_halt_inside_sum", integrate_record("x^2 + sin(x)", "x", two_steps()));

    check_golden(t, "kinematics_final_velocity",
                 kinematics_record("find v; v0 = 18 km/h; a = 3 m/s^2; t = 4 s", Budget()));
    // M1's archetype 4: no single equation reaches v, so the planner finds v0 first. The whole
    // two-hop record is pinned, because what makes this worth having is the steps rather than the
    // answer, and a route that silently became one hop again would still print 11 m/s.
    check_golden(t, "kinematics_two_hop",
                 kinematics_record("find v; x = 20 m; t = 4 s; a = 3 m/s^2", Budget()));
    check_golden(t, "kinematics_significant_figures",
                 kinematics_record("find t; v = 1.0 m/s; v0 = 0 m/s; a = 3 m/s^2", Budget()));
    // Degree two in t, so the formula, the discriminant, both roots and the selection are pinned.
    check_golden(t, "kinematics_quadratic_solved",
                 kinematics_record("find t; x = 44 m; v0 = 5 m/s; a = 3 m/s^2", Budget()));
    // The same shape with a discriminant of 377, inside the family and outside the envelope.
    check_golden(t, "kinematics_quadratic_refused",
                 kinematics_record("find t; x = 44 m; v0 = 5 m/s; a = 4 m/s^2", Budget()));
    check_golden(t, "kinematics_step_budget_halt",
                 kinematics_record("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s", one_step()));
    check_golden(t, "vector_addition_mixed_units",
                 vector_addition_record("(0.00120, 0.0020) km", "(5, 5) m", Budget()));
    check_golden(t, "vector_addition_three_dimension",
                 vector_addition_record("1 i + 2 j + 3 k m", "4 i - 2 j + 5 k m", Budget()));
    // A horizontal launch in km/h, so the fixture reaches the exact SI conversion and the final
    // rounding as well as the two axis decompositions, which SI inputs and exact integers do not.
    check_golden(t, "planar_kinematics_projectile_mixed_units",
                 planar_kinematics_record("(36.0, 0.0) km/h", "(0.0, -9.80) m/s^2", "4.00 s", true,
                                          Budget()));
    // Issue 226. A non-zero horizontal acceleration, which the projectile specialization refuses.
    check_golden(t, "planar_kinematics_general_two_dimension",
                 planar_kinematics_record("(3.0, 4.0) m/s", "(2.0, -9.80) m/s^2", "2.00 s", false,
                                          Budget()));
    // A torque, because it is the cross product a first course actually meets, and because a
    // centimetre lever arm against a newton force reaches the two rules the unit tests cannot:
    // the SI conversion only runs on a non-SI operand and the final rounding only on a measured one.
    check_golden(t, "vector_cross_torque_mixed_units",
                 vector_cross_record("20.0 i + 0.0 j + 0.0 k cm", "0.0 i + 15.0 j + 0.0 k N",
                                     Budget()));
    // Work done by a force over a displacement, because that is the scalar product a first course
    // actually meets, and because a centimetre displacement against a newton force reaches the two
    // rules the unit tests cannot: the SI conversion only runs on a non-SI operand and the final
    // rounding only on a measured one.
    check_golden(t, "scalar_product_torque_free_mixed_units",
                 scalar_product_record("30.0 i + 40.0 j N", "20.0 i + 15.0 j cm", false, Budget()));
    // The angle branch, on two exact vectors, because the angle is the other question chapter 3
    // asks and it registers a precondition the product plan has no shape for.
    check_golden(t, "scalar_product_obtuse_angle",
                 scalar_product_record("2 i + 0 j m", "-1 i + 1 j m", true, Budget()));
    // The measured angle, because the number is the other half of the same chapter 3 question and
    // it is the only route in this family that reaches a backend at all.
    {
        GoldenSequenceBackend giac({"atan(4/3)", "0", "53.13010235415598"});
        check_golden(t, "scalar_product_measured_angle",
                     scalar_product_record("3 i + 4 j m", "5 i + 0 j m", true, Budget(), &giac));
    }
    check_golden(t, "relative_motion_mixed_units", relative_motion_record(Budget()));
    check_golden(t, "unit_conversion_powered_chain",
                 unit_conversion_record("2.50 cm^3", "m^3", Budget()));
    check_golden(t, "density_volume_mixed_units",
                 density_record(DensityVariable::Volume, DensityVariable::Mass, "500.0 g",
                                DensityVariable::Density, "2.00 g/cm^3", Budget()));
    check_golden(t, "density_mass_cubic_prefix",
                 density_record(DensityVariable::Mass, DensityVariable::Density, "2 g/cm^3",
                                DensityVariable::Volume, "3 cm^3", Budget()));
    check_golden(t, "modern_photon_energy_measured",
                 modern_record(ModernRelation::PhotonWavelength, ModernVariable::PhotonEnergy,
                               {{ModernVariable::Wavelength, "620.0"}}, Budget()));
    check_golden(t, "modern_photoelectric_work_function",
                 modern_record(ModernRelation::Photoelectric, ModernVariable::WorkFunction,
                               {{ModernVariable::PhotonEnergy, "3.50"},
                                {ModernVariable::KineticEnergy, "1.20"}},
                               Budget()));
    check_golden(t, "modern_mass_defect_energy",
                 modern_record(ModernRelation::MassEnergy, ModernVariable::RestEnergy,
                               {{ModernVariable::MassDefect, "0.0304"}}, Budget()));
    check_golden(t, "work_negative_mixed_units",
                 work_record("(2.00, -3.00) N", "(-100, 400) cm",
                             WorkForceProfile::Constant, Budget()));
    check_golden(t, "work_variable_force_refused",
                 work_record("(3, 4) N", "(2, 1) m", WorkForceProfile::Variable, Budget()));
    // Issue 255's three: a horizontal push, an exact 3-4-5 incline, and an angle outside the envelope.
    {
        ForcesProblem push = forces_base(ForcesUnknown::Acceleration);
        std::string why;
        parse_quantity("12 N", &push.applied, &why);
        push.has_applied = true;
        push.friction = FrictionModel::Kinetic;
        push.friction_coefficient = Rational{1, 4};
        push.motion = MotionSense::UpTheAxis;
        push.assume_equilibrium = false;
        check_golden(t, "forces_horizontal_kinetic_friction",
                     forces_record(push, "2 kg block pushed with 12 N across a table, mu_k = 1/4",
                                   Budget()));

        ForcesProblem ramp = forces_base(ForcesUnknown::FrictionForce);
        ramp.support = "ramp";
        ramp.surface = SurfaceKind::Incline;
        ramp.incline_sin = Rational{3, 5};
        ramp.incline_cos = Rational{4, 5};
        ramp.friction = FrictionModel::Static;
        ramp.friction_coefficient = Rational{1, 1};
        check_golden(t, "forces_incline_static_friction",
                     forces_record(ramp, "2 kg block at rest on a 3-4-5 ramp, mu_s = 1",
                                   Budget()));

        ForcesProblem inexact = forces_base(ForcesUnknown::Acceleration);
        inexact.surface = SurfaceKind::Incline;
        inexact.incline_sin = Rational{1, 3};
        inexact.incline_cos = Rational{1, 3};
        check_golden(t, "forces_incline_angle_refused",
                     forces_record(inexact,
                                   "2 kg block on an incline whose sine and cosine are not an "
                                   "exact pair",
                                   Budget()));
    }
    // One fixture per newly catalogued family, so its rule lines have a derivation behind them.
    {
        std::string why;
        Quantity first_mass, second_mass, separation;
        parse_quantity("2 kg", &first_mass, &why);
        parse_quantity("3 kg", &second_mass, &why);
        parse_quantity("1 m", &separation, &why);
        RelationProblem gravity = gravitation_problem(GravitationVariable::Force);
        gravity.knowns.push_back(gravitation_known(GravitationVariable::FirstMass, first_mass));
        gravity.knowns.push_back(gravitation_known(GravitationVariable::SecondMass, second_mass));
        gravity.knowns.push_back(gravitation_known(GravitationVariable::Separation, separation));
        check_golden(t, "gravitation_two_point_masses",
                     relation_record(gravitation_model(), gravity,
                                     "two point masses of 2 kg and 3 kg, 1 m apart; find the "
                                     "gravitational force",
                                     solve_gravitation, Budget()));

        Quantity stiffness, displacement;
        parse_quantity("200 N/m", &stiffness, &why);
        parse_quantity("0.05 m", &displacement, &why);
        RelationProblem spring = oscillation_problem(OscillationVariable::RestoringForce);
        spring.knowns.push_back(oscillation_known(OscillationVariable::Stiffness, stiffness));
        spring.knowns.push_back(oscillation_known(OscillationVariable::Displacement, displacement));
        check_golden(t, "oscillation_restoring_force",
                     relation_record(oscillation_model(), spring,
                                     "a spring of stiffness 200 N/m displaced 0.05 m; find the "
                                     "restoring force",
                                     solve_oscillation, Budget()));

        // A prefixed wavelength reaches the conversion step an all-SI problem never runs.
        Quantity frequency, wavelength;
        parse_quantity("50 s^-1", &frequency, &why);
        parse_quantity("40 cm", &wavelength, &why);
        RelationProblem wave = wave_problem(WaveVariable::Speed);
        wave.knowns.push_back(wave_known(WaveVariable::Frequency, frequency));
        wave.knowns.push_back(wave_known(WaveVariable::Wavelength, wavelength));
        check_golden(t, "wave_speed_mixed_units",
                     relation_record(wave_model(), wave,
                                     "a wave at 50 s^-1 with a 40 cm wavelength; find the speed",
                                     solve_wave, Budget()));

        Quantity speed, radius;
        parse_quantity("1 m/s", &speed, &why);
        parse_quantity("1 m", &radius, &why);
        CircularMotionProblem circle;
        circle.unknown = CircularMotionVariable::Period;
        circle.knowns[0] = CircularMotionKnown{CircularMotionVariable::Speed, speed};
        circle.knowns[1] = CircularMotionKnown{CircularMotionVariable::Radius, radius};
        circle.known_count = 2;
        check_golden(t, "circular_motion_period_and_acceleration",
                     circular_motion_record(circle,
                                            "uniform circular motion at 1 m/s on a 1 m radius; "
                                            "find the period",
                                            Budget()));

        // beta = 3/5 gives an exact rational Lorentz factor, which is what the envelope requires.
        RelativityProblem dilation = relativity_problem(RelativityRelation::TimeDilation, 3, 5);
        dilation.knowns.push_back(RelativityKnown{
            RelativityVariable::ProperTime,
            relativity_declared(RelativityVariable::ProperTime, 4, 1)});
        check_golden(t, "relativity_time_dilation_exact_gamma",
                     relativity_record(dilation,
                                       "a 4 s proper interval on a ship at beta = 3/5; find the "
                                       "time the station reads",
                                       Budget()));

        PositionMotionProblem motion;
        motion.position_x = "3*t";
        motion.position_y = "2*t^2";
        motion.rank = 2;
        parse_quantity("0 s", &motion.interval_start, &why);
        parse_quantity("2 s", &motion.interval_end, &why);
        parse_quantity("2 s", &motion.event_time, &why);
        check_golden(t, "position_motion_vector_derivatives",
                     position_motion_record(motion,
                                            "r(t) = 3t i + 2t^2 j over 0 to 2 s, read at 2 s",
                                            Budget()));

        // The speed and radius unknowns, so their rule lines have a fixture and not only the period's.
        Quantity period;
        parse_quantity("6.28318530717959 s", &period, &why);
        CircularMotionProblem for_speed;
        for_speed.unknown = CircularMotionVariable::Speed;
        for_speed.knowns[0] = CircularMotionKnown{CircularMotionVariable::Radius, radius};
        for_speed.knowns[1] = CircularMotionKnown{CircularMotionVariable::Period, period};
        for_speed.known_count = 2;
        check_golden(t, "circular_motion_speed_from_period",
                     circular_motion_record(for_speed,
                                            "uniform circular motion on a 1 m radius with a "
                                            "6.28318530717959 s period; find the speed",
                                            Budget()));

        CircularMotionProblem for_radius;
        for_radius.unknown = CircularMotionVariable::Radius;
        for_radius.knowns[0] = CircularMotionKnown{CircularMotionVariable::Speed, speed};
        for_radius.knowns[1] = CircularMotionKnown{CircularMotionVariable::Period, period};
        for_radius.known_count = 2;
        check_golden(t, "circular_motion_radius_from_period",
                     circular_motion_record(for_radius,
                                            "uniform circular motion at 1 m/s with a "
                                            "6.28318530717959 s period; find the radius",
                                            Budget()));

        // The four relations beside time dilation, each its own catalog family needing its own record.
        RelativityProblem contraction =
            relativity_problem(RelativityRelation::LengthContraction, 4, 5);
        contraction.knowns.push_back(RelativityKnown{
            RelativityVariable::ProperLength,
            relativity_declared(RelativityVariable::ProperLength, 10, 1)});
        check_golden(t, "relativity_length_contraction_exact_gamma",
                     relativity_record(contraction,
                                       "a 10 m rod at rest in a ship at beta = 4/5; find the "
                                       "length the station measures",
                                       Budget()));

        RelativityProblem lorentz =
            relativity_problem(RelativityRelation::LorentzTransformation, 3, 5);
        lorentz.knowns.push_back(RelativityKnown{
            RelativityVariable::EventPosition,
            relativity_declared(RelativityVariable::EventPosition, 299792458, 1)});
        lorentz.knowns.push_back(
            RelativityKnown{RelativityVariable::EventTime,
                            relativity_declared(RelativityVariable::EventTime, 1, 1)});
        check_golden(t, "relativity_lorentz_transformation_event",
                     relativity_record(lorentz,
                                       "one event at x = 299792458 m and t = 1 s, read from a "
                                       "ship at beta = 3/5",
                                       Budget()));

        RelativityProblem addition =
            relativity_problem(RelativityRelation::VelocityAddition, 1, 2);
        addition.knowns.push_back(RelativityKnown{
            RelativityVariable::ObjectVelocity,
            relativity_declared(RelativityVariable::ObjectVelocity, 1, 2)});
        check_golden(t, "relativity_velocity_addition_half_c",
                     relativity_record(addition,
                                       "an object at 1/2 c inside a ship at 1/2 c; find its speed "
                                       "in the station",
                                       Budget()));

        RelativityProblem energy = relativity_problem(RelativityRelation::EnergyMomentum, 3, 5);
        energy.knowns.push_back(RelativityKnown{
            RelativityVariable::RestEnergy,
            relativity_declared(RelativityVariable::RestEnergy, 938, 1)});
        check_golden(t, "relativity_energy_momentum_proton",
                     relativity_record(energy,
                                       "a 938 MeV proton at beta = 3/5; find its total energy, "
                                       "momentum and kinetic energy",
                                       Budget()));

        RankingModel ranking;
        ranking.quantity_name = "average speed";
        ranking.criteria.push_back(
            RankingCriterion{"distance covered", RankingDirection::Increasing});
        RankingProblem paths;
        RankingSituation path1;
        path1.name = "path 1";
        path1.values.push_back(ranking_known(3));
        RankingSituation path2;
        path2.name = "path 2";
        path2.values.push_back(ranking_known(1));
        RankingSituation path3;
        path3.name = "path 3";
        path3.values.push_back(ranking_known(3));
        paths.situations.push_back(path1);
        paths.situations.push_back(path2);
        paths.situations.push_back(path3);
        check_golden(t, "ranking_average_speed_with_a_tie",
                     ranking_record(ranking, paths,
                                    "rank three paths by average speed over the same interval",
                                    Budget()));
    }
    check_golden(t, "optics_refraction_transmitted_sine",
                 optics_record(OpticsRelation::Refraction, OpticsVariable::SineTransmitted,
                               {{OpticsVariable::IndexIncident, "2"},
                                {OpticsVariable::SineIncident, "0.3"},
                                {OpticsVariable::IndexTransmitted, "1"}},
                               Budget()));
    check_golden(t, "optics_refraction_total_internal",
                 optics_record(OpticsRelation::Refraction, OpticsVariable::SineTransmitted,
                               {{OpticsVariable::IndexIncident, "2"},
                                {OpticsVariable::SineIncident, "0.8"},
                                {OpticsVariable::IndexTransmitted, "1"}},
                               Budget()));
    check_golden(t, "optics_thin_lens_real_image",
                 optics_record(OpticsRelation::ThinLens, OpticsVariable::ImageDistance,
                               {{OpticsVariable::FocalLength, "10 cm"},
                                {OpticsVariable::ObjectDistance, "15 cm"}},
                               Budget()));
    check_golden(t, "optics_spherical_mirror_image",
                 optics_record(OpticsRelation::SphericalMirror, OpticsVariable::ImageDistance,
                               {{OpticsVariable::FocalLength, "20 cm"},
                                {OpticsVariable::ObjectDistance, "30 cm"}},
                               Budget()));
    check_golden(t, "optics_double_slit_wavelength",
                 optics_record(OpticsRelation::DoubleSlit, OpticsVariable::Wavelength,
                               {{OpticsVariable::SlitSpacing, "1 mm"},
                                {OpticsVariable::SineFringe, "0.0012"},
                                {OpticsVariable::FringeOrder, "2"}},
                               Budget()));
    check_golden(t, "optics_single_slit_minimum",
                 optics_record(OpticsRelation::SingleSlit, OpticsVariable::SineFringe,
                               {{OpticsVariable::SlitSpacing, "0.1 mm"},
                                {OpticsVariable::Wavelength, "0.0000006 m"},
                                {OpticsVariable::FringeOrder, "1"}},
                               Budget()));
    check_golden(t, "vector_components_exact", exact_vector_components_record(Budget()));
    check_golden(t, "vector_components_negative_quadrant",
                 negative_quadrant_polar_record(Budget()));
    check_golden(t, "vector_components_spherical", spherical_polar_record(Budget()));
    check_golden(t, "catch_up_delayed_start", catch_up_delayed_start_record(Budget()));
    check_golden(t, "catch_up_measured_report", catch_up_measured_report_record(Budget()));
    check_golden(t, "catch_up_before_shared_domain", catch_up_before_domain_record(Budget()));
    check_golden(t, "catch_up_nonlinear_refused", catch_up_nonlinear_refusal_record(Budget()));

    check_golden(t, "integer_quotient", integer_record("iquo(17,5)", Budget()));
    check_golden(t, "integer_remainder", integer_record("irem(17,5)", Budget()));
    check_golden(t, "integer_factorial", integer_record("factorial(5)", Budget()));
    check_golden(t, "integer_permutation", integer_record("perm(5,3)", Budget()));
    check_golden(t, "integer_combination", integer_record("comb(8,3)", Budget()));
    check_golden(t, "integer_prime", integer_record("is_prime(17)", Budget()));
    check_golden(t, "integer_composite", integer_record("is_prime(25)", Budget()));
    check_golden(t, "integer_next_prime", integer_record("nextprime(24)", Budget()));
    check_golden(t, "integer_modular_power", integer_record("powmod(7,13,11)", Budget()));
    check_golden(t, "integer_factorization", integer_record("ifactor(60)", Budget()));
    check_golden(t, "integer_gcd", integer_record("gcd(-48,18)", Budget()));
    check_golden(t, "integer_gcd_zero", integer_record("gcd(0,0)", Budget()));
    check_golden(t, "integer_gcd_refused", integer_record("gcd(1000000001,18)", Budget()));
    check_golden(t, "integer_refused", integer_record("factorial(101)", Budget()));
    check_golden(t, "integer_invalid", integer_record("iquo(17,0)", Budget()));
    check_golden(t, "integer_step_budget", integer_record("factorial(5)", one_step()));
    check_golden(t, "integer_cancelled", integer_record("factorial(5)", cancelling()));

    test_golden_writer_projects_the_trace(t);

    // Last, because every record above has to have been built for the pass to have seen it.
    check_golden_invariants(t);
}

}  // namespace nps
