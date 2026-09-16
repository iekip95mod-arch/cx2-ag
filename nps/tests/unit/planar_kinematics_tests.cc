#include <string>
#include <utility>
#include <vector>

#include "nps/physics/planar_kinematics.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

Vector parsed_vector(const char *text, const char *frame = "lab") {
    Vector vector;
    std::string error;
    if (!parse_vector(text, &vector, &error)) {
        vector.rank = 0;
        vector.frame.name = "parse failed: " + error;
        return vector;
    }
    vector.frame.name = frame;
    return vector;
}

Quantity parsed_quantity(const char *text) {
    Quantity quantity;
    std::string error;
    parse_quantity(text, &quantity, &error);
    return quantity;
}

PlanarKinematicsProblem problem(const Vector &velocity, const Vector &acceleration,
                                const char *time = "2 s") {
    PlanarKinematicsProblem input;
    input.body_name = "ball";
    input.initial_velocity = velocity;
    input.acceleration = acceleration;
    input.elapsed_time = parsed_quantity(time);
    return input;
}

struct Run {
    explicit Run(const PlanarKinematicsProblem &problem, const Budget &budget = Budget(),
                 Backend *backend = nullptr)
        : result(solve_planar_kinematics(arena, derivation, problem, budget, backend)) {}

    Arena arena;
    Derivation derivation;
    PlanarKinematicsResult result;
};

// The scripted backends relative_motion_tests.cc drives its own cross-check through. Copied
// rather than shared because that file keeps them in its anonymous namespace.
class SequenceBackend : public Backend {
  public:
    explicit SequenceBackend(std::vector<std::string> replies) : replies_(std::move(replies)) {}

    bool eval(const std::string &command, std::string *out, std::string *error) override {
        commands.push_back(command);
        if (next_ >= replies_.size()) {
            *error = "no scripted reply";
            return false;
        }
        *out = replies_[next_++];
        return true;
    }

    std::vector<std::string> commands;

  private:
    std::vector<std::string> replies_;
    size_t next_ = 0;
};

class FailingBackend : public Backend {
  public:
    bool eval(const std::string &command, std::string *, std::string *error) override {
        commands.push_back(command);
        *error = "backend down";
        return false;
    }

    std::vector<std::string> commands;
};

std::string check_detail(const Derivation &derivation, const char *rule) {
    std::string detail;
    for (size_t index = 0; index < derivation.size(); ++index) {
        const StepId id = static_cast<StepId>(index);
        if (derivation.at(id).rule_id != rule)
            continue;
        for (const VerificationRecord &record : derivation.at(id).verifications)
            detail += record.detail;
    }
    return detail;
}

bool has_rule(const Derivation &derivation, const char *rule) {
    for (size_t index = 0; index < derivation.size(); ++index) {
        if (derivation.at(static_cast<StepId>(index)).rule_id == rule)
            return true;
    }
    return false;
}

bool cancel_now(void *) { return true; }

}  // namespace

void run_planar_kinematics_tests(TestSink &t) {
    {
        Run solved(problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2")));
        t.evidence("PHYS-019", planar_kinematics_outcome_name(solved.result.outcome), "solved",
                   "declared axes and a shared time produce a planar displacement");
        t.equal(solved.result.displacement_text, "(6 i - 12 j) m",
                "each axis integrates v0 t + a t^2 / 2 under the same elapsed time");
        t.equal(solved.result.final_velocity_text, "(3 i - 16 j) m/s",
                "the final velocity is the initial velocity plus a t");
        t.check(solved.result.displacement_stage == MotionStage::Interval &&
                    solved.result.final_velocity_stage == MotionStage::State,
                "the displacement spans the interval while the final velocity is a state");
        t.equal(solved.derivation.context.problem_family_id,
                "physics.kinematics.constant-acceleration.two-dimension",
                "the context identifies the planar constant-acceleration family");
        t.check(has_rule(solved.derivation, "physics.planar-kinematics.component-i") &&
                    has_rule(solved.derivation, "physics.planar-kinematics.component-j"),
                "both axes are decomposed in their own recorded step");
        t.check(has_rule(solved.derivation, "physics.planar-kinematics.check-shared-time"),
                "the shared-time identity is the family's final check");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(20, 0) m/s"), parsed_vector("(0, -10) m/s^2"), "3 s");
        input.projectile = true;
        Run solved(input);
        t.evidence("PHYS-019", planar_kinematics_outcome_name(solved.result.outcome), "solved",
                   "the projectile specialization accepts free fall in the declared axes");
        t.equal(solved.result.displacement_text, "(60 i - 45 j) m",
                "a horizontal launch falls g t^2 / 2 while drifting at its launch speed");
        t.equal(solved.derivation.context.problem_family_id,
                "physics.kinematics.constant-acceleration.projectile.two-dimension",
                "the projectile specialization reports its own family");
        t.check(has_rule(solved.derivation, "physics.planar-kinematics.check-projectile"),
                "the projectile precondition is checked before the reading is offered");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(20, 0) m/s"), parsed_vector("(2, -10) m/s^2"));
        input.projectile = true;
        Run refused(input);
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "not a projectile",
                "a horizontal acceleration is refused as a projectile rather than solved");
        t.check(!refused.result.has_value,
                "the projectile refusal offers no displacement");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2"));
        input.elapsed_time_stage = MotionStage::Event;
        Run refused(input);
        t.evidence("PHYS-028", planar_kinematics_outcome_name(refused.result.outcome),
                   "stage mismatch",
                   "an event quantity cannot be substituted where the interval is required");
        t.check(refused.result.detail.find("interval") != std::string::npos,
                "the stage refusal names the stages it received");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2"));
        input.initial_velocity_stage = MotionStage::Interval;
        Run refused(input);
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "stage mismatch",
                "an interval quantity cannot stand in for the state at the initial event");
    }
    {
        Run refused(problem(parsed_vector("(3, 4) m/s", "lab"),
                            parsed_vector("(0, -10) m/s^2", "deck")));
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "frame mismatch",
                "components in different frames are refused rather than combined");
    }
    {
        Run refused(problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s")));
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "dimension mismatch",
                "a velocity supplied as the acceleration is refused");
    }
    {
        Budget budget;
        budget.poll = cancel_now;
        Run cancelled(problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2")),
                      budget);
        t.equal(planar_kinematics_outcome_name(cancelled.result.outcome), "cancelled",
                "a cancelled planar solve reports cancellation instead of an answer");
        t.check(!cancelled.result.has_value, "a cancelled solve offers no displacement");
    }
    {
        // km/h and minutes make the exact SI conversion visible in the answer.
        Run converted(problem(parsed_vector("(36, 0) km/h"), parsed_vector("(0, -10) m/s^2"),
                              "1 min"));
        t.equal(planar_kinematics_outcome_name(converted.result.outcome), "solved",
                "mixed units convert exactly before either axis is integrated");
        t.equal(converted.result.displacement_text, "(600 i - 18000 j) m",
                "36 km/h is 10 m/s and one minute is sixty seconds");
    }
    {
        const PlanarKinematicsProblem input =
            problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2"));
        Run alone(input);
        // The vertical average-velocity route is (4 + -16) / 2 * 2, so -12 m is the reply that
        // agrees with the local vertical displacement and 5 is the one that cannot.
        SequenceBackend agreeing({"-12"});
        Run checked(input, Budget(), &agreeing);
        t.equal(planar_kinematics_outcome_name(checked.result.outcome), "solved",
                "an agreeing backend leaves the planar solve standing");
        t.check(!agreeing.commands.empty(), "the agreeing backend was actually asked");
        t.equal(checked.result.displacement_text, alone.result.displacement_text,
                "an agreeing backend does not move the displacement it checked");
        t.check(check_detail(checked.derivation, "physics.planar-kinematics.check-shared-time")
                    .find("Giac agrees") != std::string::npos,
                "the shared-time check records that the backend agreed");

        SequenceBackend contradicting({"5"});
        Run disputed(input, Budget(), &contradicting);
        t.equal(planar_kinematics_outcome_name(disputed.result.outcome), "verification failed",
                "a backend that contradicts the vertical displacement withholds the answer");
        t.check(!disputed.result.has_value, "a contradicted cross-check offers no displacement");
        t.check(disputed.result.detail.find("Giac disagrees") != std::string::npos,
                "the refusal names the disagreement rather than the arithmetic");

        FailingBackend broken;
        Run unavailable(input, Budget(), &broken);
        t.equal(planar_kinematics_outcome_name(unavailable.result.outcome), "solved",
                "an unavailable backend does not withdraw the local integration");
        t.check(!broken.commands.empty(), "the unavailable backend was actually asked");
        t.check(check_detail(unavailable.derivation, "physics.planar-kinematics.check-shared-time")
                    .find("Giac returned") != std::string::npos,
                "the shared-time check records which tag came back instead of an exact reply");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2"));
        input.body_name.clear();
        Run refused(input);
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "invalid problem",
                "a body without a name is refused before any axis is integrated");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2"));
        input.body_name.assign(Arena().limits().max_input_bytes + 1, 'b');
        Run refused(input);
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "resource exceeded",
                "identifiers past the input byte limit are refused as a resource halt");
    }
    {
        Run refused(problem(parsed_vector("(3, 4, 5) m/s"), parsed_vector("(0, -10) m/s^2")));
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "rank mismatch",
                "a three-component velocity cannot be integrated in a plane");
    }
    {
        Run refused(problem(parsed_vector("(3, 4) m/s", ""), parsed_vector("(0, -10) m/s^2", "")));
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "frame undeclared",
                "components without a named frame are refused rather than assumed");
    }
    {
        Run refused(problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2"), "-2 s"));
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "invalid problem",
                "a negative elapsed time is refused rather than integrated backwards");
        t.check(!refused.result.has_value, "a negative interval offers no displacement");
    }
    {
        PlanarKinematicsProblem input =
            problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2"));
        input.elapsed_time.value = Rational{4000000000LL, 1};
        Run refused(input);
        t.equal(planar_kinematics_outcome_name(refused.result.outcome), "arithmetic overflow",
                "an elapsed time whose square leaves exact arithmetic is a refusal, not a wrap");
    }
    {
        Run solved(problem(parsed_vector("(3.0, 4.0) m/s"), parsed_vector("(1.0, -2.0) m/s^2"),
                           "2.0 s"));
        t.equal(planar_kinematics_outcome_name(solved.result.outcome), "solved",
                "measured components are integrated exactly before they are rounded");
        t.equal(solved.result.displacement_text, "(8.0 i + 4.0 j) m",
                "the planar displacement rounds once at the shared decimal place");
        t.equal(solved.result.final_velocity_text, "5.0 i m/s",
                "the reported final velocity is rounded at that place as well");
        t.check(solved.result.displacement.x.num == 8 && solved.result.displacement.x.den == 1 &&
                    solved.result.final_velocity.x.num == 5 &&
                    solved.result.final_velocity.x.den == 1,
                "measured reporting does not replace the exact components");
        const std::string rounding =
            check_detail(solved.derivation, "physics.planar-kinematics.significant-figures");
        t.check(rounding.find("(8.0 i + 4.0 j) m is within half a unit in the last place of "
                              "(8 i + 4 j) m") != std::string::npos,
                "the displacement rounding cites both of the values it was between");
        t.check(rounding.find("5.0 i m/s is within half a unit in the last place of 5 i m/s") !=
                    std::string::npos,
                "the final velocity is judged too rather than reported unchecked");
    }
    {
        Run solved(problem(parsed_vector("(3, 4) m/s"), parsed_vector("(0, -10) m/s^2")));
        t.check(solved.result.interpretation.find("ball moves right and down") != std::string::npos,
                "the declared axes give each displacement sign a left-right and up-down reading");
        t.check(solved.result.interpretation.find("(3 i - 16 j) m/s") != std::string::npos,
                "the interpretation names the final velocity the interval ends at");
    }
    {
        Run solved(problem(parsed_vector("(0, 4) m/s"), parsed_vector("(0, -10) m/s^2")));
        t.check(solved.result.interpretation.find("neither left nor right and down") !=
                    std::string::npos,
                "a zero horizontal displacement is read as neither direction rather than as left");
    }
    {
        Run solved(problem(parsed_vector("(3, 40) m/s"), parsed_vector("(0, -10) m/s^2")));
        t.check(solved.result.interpretation.find("ball moves right and up") != std::string::npos,
                "a throw that is still climbing at the final event is read as up rather than down");
        t.check(solved.result.interpretation.find("(3 i + 20 j) m/s") != std::string::npos,
                "the climbing interpretation names the final velocity the interval ends at");
    }
    {
        Run solved(problem(parsed_vector("(3, 10) m/s"), parsed_vector("(0, -10) m/s^2")));
        t.check(solved.result.interpretation.find("right and neither up nor down") !=
                    std::string::npos,
                "a throw that returns to its launch height is read as neither up nor down");
        t.check(solved.result.interpretation.find("(3 i - 10 j) m/s") != std::string::npos,
                "a zero vertical displacement still ends at a downward final velocity");
    }
    {
        Run solved(problem(parsed_vector("(-3, 4) m/s"), parsed_vector("(0, -10) m/s^2")));
        t.check(solved.result.interpretation.find("ball moves left and down") != std::string::npos,
                "a negative horizontal displacement is read as left rather than as right");
        t.check(solved.result.interpretation.find("(-3 i - 16 j) m/s") != std::string::npos,
                "the leftward interpretation names the final velocity the interval ends at");
    }
    {
        // PHYS 2410 Chapters 1-4 test, problem 4(a, b): the same launch as part (c) below, read
        // directly by the planar family since parts (a) and (b) give the elapsed time.
        PlanarKinematicsProblem input;
        input.body_name = "stone";
        input.initial_velocity = parsed_vector("(21.0, 36.4) m/s");
        input.acceleration = parsed_vector("(0, -9.80) m/s^2");
        input.elapsed_time = parsed_quantity("5.50 s");
        input.projectile = true;
        Arena arena;
        Derivation derivation;
        const PlanarKinematicsResult solved = solve_planar_kinematics(arena, derivation, input);
        t.equal(planar_kinematics_outcome_name(solved.outcome), "solved",
                "the planar family solves parts (a) and (b) with the elapsed time given");
        t.equal(solved.displacement_text, "(116 i + 52 j) m",
                "part (a): h = v0y t + a t^2 / 2 reaches the cliff height of 52 m");
        t.equal(solved.final_velocity_text, "(21.0 i - 17.5 j) m/s",
                "part (b): v_fx = v_0x since a_x = 0, and v_fy = v_0y + a t, giving an impact "
                "speed of 27.3 m/s");
    }
    {
        // PHYS 2410 Chapters 1-4 test, problem 4(c): 42.0 m/s at 60.0 degrees decomposes to
        // (21.0, 36.4) m/s, and the apex is reached at 3.71 s and 67.6 m above the launch point.
        PlanarApexProblem input;
        input.body_name = "stone";
        input.initial_velocity = parsed_vector("(21.0, 36.4) m/s");
        input.acceleration = parsed_vector("(0, -9.80) m/s^2");
        Arena arena;
        Derivation derivation;
        const PlanarApexResult solved = solve_planar_apex(arena, derivation, input);
        t.equal(planar_kinematics_outcome_name(solved.outcome), "solved",
                "an upward projectile reaches its apex through two independent routes");
        t.equal(solved.time_to_apex_text, "3.71",
                "the time to the apex comes from v = v0 + a t with the apex velocity zero");
        t.equal(solved.height_text, "67.6",
                "the apex height agrees between the time-to-apex route and v^2 = v0^2 + 2 a x");
        t.check(has_rule(derivation, "physics.planar-kinematics.check-apex-routes"),
                "the two-route agreement is recorded through the family's check vocabulary");
        t.check(check_detail(derivation, "physics.planar-kinematics.check-apex-routes")
                    .find("67.6") != std::string::npos,
                "the recorded check names the height both routes reached");
    }
    {
        // A projectile with no upward component never rises above the launch point, so there is no
        // apex to ask for.
        PlanarApexProblem input;
        input.body_name = "puck";
        input.initial_velocity = parsed_vector("(5, 0) m/s");
        input.acceleration = parsed_vector("(0, -9.80) m/s^2");
        Arena arena;
        Derivation derivation;
        const PlanarApexResult refused = solve_planar_apex(arena, derivation, input);
        t.equal(planar_kinematics_outcome_name(refused.outcome), "no apex above the launch point",
                "a projectile with no upward component is refused rather than given a height");
        t.check(!refused.has_value, "the refusal offers no apex height");
    }
    {
        // A cancelled nested solve_kinematics call must report cancellation rather than being
        // collapsed into a generic verification failure, which would misreport a stopped run as a
        // physics disagreement.
        PlanarApexProblem input;
        input.body_name = "stone";
        input.initial_velocity = parsed_vector("(21.0, 36.4) m/s");
        input.acceleration = parsed_vector("(0, -9.80) m/s^2");
        Budget budget;
        budget.poll = cancel_now;
        Arena arena;
        Derivation derivation;
        const PlanarApexResult cancelled = solve_planar_apex(arena, derivation, input, budget);
        t.equal(planar_kinematics_outcome_name(cancelled.outcome), "cancelled",
                "a cancelled apex solve reports cancellation instead of a verification failure");
        t.check(!cancelled.has_value, "a cancelled apex solve offers no height");
    }
    {
        // Both routes are exact rational arithmetic over the same inputs, so a real problem can
        // never make them disagree; only a defect in the comparison itself could. apex_routes_agree
        // is the exact comparison check-apex-routes records, isolated so it can be driven with
        // literal values no valid problem would ever produce, and watched fail: reverting it in
        // planar_kinematics.cc to `return true;`, rebuilding and rerunning this case fails the
        // second assertion below; restoring the real comparison passes it again.
        Arena arena;
        t.check(apex_routes_agree(arena, arena.integer("67"), arena.integer("67")),
                "two routes that canonicalize to the same value agree");
        t.check(!apex_routes_agree(arena, arena.integer("67"), arena.integer("68")),
                "two routes that canonicalize to different values are caught disagreeing");
    }
}

}  // namespace nps
