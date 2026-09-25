#include "nps/steps/system.h"

#include <array>

#include "nps/core/canonical.h"
#include "nps/core/context.h"
#include "nps/core/evaluate.h"
#include "nps/core/matrix.h"
#include "nps/core/print.h"
#include "nps/steps/matrix_form.h"
#include "nps/steps/matrix_row.h"

namespace nps {
namespace {

constexpr size_t kColumns = kSystemMaxUnknowns + 1;
constexpr size_t kCells = kSystemMaxEquations * kColumns;

// The coefficient of each unknown, then the constant term, in the last slot the system uses.
struct Affine {
    std::array<Rational, kColumns> terms{};
};

bool is_zero(const Rational &value) {
    return value.num == 0;
}

bool constant_only(const Affine &value, size_t unknowns) {
    for (size_t i = 0; i < unknowns; ++i) {
        if (!is_zero(value.terms[i]))
            return false;
    }
    return true;
}

bool mentions_unknown(const Arena &arena, NodeId id, std::span<const NodeId> unknowns) {
    return arena.any_node(id, [&arena, unknowns](NodeId current) {
        if (arena.at(current).kind != Kind::Symbol)
            return false;
        for (NodeId unknown : unknowns) {
            if (arena.text(unknown) == arena.text(current))
                return true;
        }
        return false;
    });
}

LinearRowRead read_affine(const Arena &arena, NodeId id, std::span<const NodeId> unknowns,
                          Meter &meter, size_t depth, Affine *out) {
    const size_t n = unknowns.size();
    *out = Affine();
    if (!meter.rewrite())
        return LinearRowRead::Halted;
    if (depth > Limits().max_depth)
        return LinearRowRead::Overflowed;
    if (arena.is_approximate(id))
        return LinearRowRead::Inexact;
    const Node &node = arena.at(id);
    const ChildView children = arena.children(id);
    switch (node.kind) {
        case Kind::Integer:
            if (!node.small_valid)
                return LinearRowRead::Overflowed;
            out->terms[n] = Rational{node.small, 1};
            return LinearRowRead::Read;
        case Kind::Decimal:
            return LinearRowRead::Inexact;
        case Kind::Symbol:
            for (size_t i = 0; i < n; ++i) {
                if (arena.text(unknowns[i]) == arena.text(id)) {
                    out->terms[i] = Rational{1, 1};
                    return LinearRowRead::Read;
                }
            }
            return LinearRowRead::OtherSymbol;
        case Kind::Neg: {
            if (children.size() != 1)
                return LinearRowRead::NotLinear;
            const LinearRowRead inner = read_affine(arena, children[0], unknowns, meter, depth + 1, out);
            if (inner != LinearRowRead::Read)
                return inner;
            for (size_t i = 0; i <= n; ++i) {
                if (!rational_mul(out->terms[i], Rational{-1, 1}, &out->terms[i]))
                    return LinearRowRead::Overflowed;
            }
            return LinearRowRead::Read;
        }
        case Kind::Add: {
            for (NodeId child : children) {
                Affine part;
                const LinearRowRead read = read_affine(arena, child, unknowns, meter, depth + 1, &part);
                if (read != LinearRowRead::Read)
                    return read;
                for (size_t i = 0; i <= n; ++i) {
                    if (!rational_add(out->terms[i], part.terms[i], &out->terms[i]))
                        return LinearRowRead::Overflowed;
                }
            }
            return LinearRowRead::Read;
        }
        case Kind::Mul: {
            Rational scale{1, 1};
            Affine carrier;
            bool have_carrier = false;
            for (NodeId child : children) {
                Affine part;
                const LinearRowRead read = read_affine(arena, child, unknowns, meter, depth + 1, &part);
                if (read != LinearRowRead::Read)
                    return read;
                if (constant_only(part, n)) {
                    if (!rational_mul(scale, part.terms[n], &scale))
                        return LinearRowRead::Overflowed;
                } else if (have_carrier) {
                    return LinearRowRead::NotLinear;
                } else {
                    carrier = part;
                    have_carrier = true;
                }
            }
            if (!have_carrier) {
                out->terms[n] = scale;
                return LinearRowRead::Read;
            }
            for (size_t i = 0; i <= n; ++i) {
                if (!rational_mul(carrier.terms[i], scale, &out->terms[i]))
                    return LinearRowRead::Overflowed;
            }
            return LinearRowRead::Read;
        }
        case Kind::Pow: {
            if (children.size() != 2)
                return LinearRowRead::NotLinear;
            int64_t exponent = 0;
            if (arena.is_approximate(children[1]) || !small_integer(arena, children[1], &exponent))
                return mentions_unknown(arena, id, unknowns) ? LinearRowRead::NotLinear
                                                             : LinearRowRead::Inexact;
            Affine base;
            const LinearRowRead read = read_affine(arena, children[0], unknowns, meter, depth + 1, &base);
            if (read != LinearRowRead::Read)
                return read;
            if (exponent == 1) {
                *out = base;
                return LinearRowRead::Read;
            }
            if (!constant_only(base, n))
                return LinearRowRead::NotLinear;
            if (is_zero(base.terms[n]) && exponent < 0)
                return LinearRowRead::Inexact;
            if (!rational_power(base.terms[n], exponent, &out->terms[n]))
                return LinearRowRead::Overflowed;
            return LinearRowRead::Read;
        }
        default:
            return mentions_unknown(arena, id, unknowns) ? LinearRowRead::NotLinear
                                                         : LinearRowRead::Inexact;
    }
}

struct Run {
    Arena &arena;
    Derivation &derivation;
    NodeId equations;
    std::vector<NodeId> unknowns;
    std::vector<NodeId> rows;
    Meter meter;
    size_t mark;
    StepId plan = kNoStep;
    std::array<Rational, kCells> cells{};
    NodeId matrix = kNoNode;
    bool failed = false;
    SystemOutcome failure = SystemOutcome::OutsideEnvelope;
    std::string detail;

    Run(Arena &a, Derivation &d, NodeId list, const Budget &budget)
        : arena(a), derivation(d), equations(list), meter(budget), mark(d.mark()) {}

    size_t n() const { return unknowns.size(); }
    size_t m() const { return rows.size(); }
    size_t columns() const { return n() + 1; }
    Rational &cell(size_t row, size_t column) { return cells[row * columns() + column]; }

    bool refuse(SystemOutcome outcome, std::string reason) {
        failed = true;
        failure = outcome;
        detail = std::move(reason);
        return false;
    }

    bool running() {
        if (failed)
            return false;
        if (arena.failed())
            return refuse(SystemOutcome::ResourceExceeded, status_name(arena.status()));
        if (meter.stopped() || !meter.checkpoint())
            return refuse(meter.halt() == Halt::Cancelled ? SystemOutcome::Cancelled
                                                          : SystemOutcome::ResourceExceeded,
                          halt_name(meter.halt()));
        return true;
    }

    bool out_of_arithmetic() {
        return refuse(SystemOutcome::ResourceExceeded, "the exact rational arithmetic ran out of room");
    }

    NodeId matrix_node() {
        std::vector<NodeId> built;
        for (size_t row = 0; row < m(); ++row) {
            std::vector<NodeId> entries;
            for (size_t column = 0; column < columns(); ++column)
                entries.push_back(canonical_rational(arena, cell(row, column)));
            built.push_back(arena.list(entries));
        }
        return arena.list(built);
    }

    std::string unknown_name(size_t i) const { return arena.text(unknowns[i]); }

    std::vector<SymbolValue> assignment(std::span<const Rational> values) const {
        std::vector<SymbolValue> out;
        for (size_t i = 0; i < n(); ++i)
            out.push_back({unknown_name(i), values[i]});
        return out;
    }

    // Both sides of equation row as it was typed, at one assignment of every unknown.
    bool evaluate_sides(size_t row, const std::vector<SymbolValue> &values, Rational *left,
                        Rational *right) const {
        const ChildView sides = arena.children(rows[row]);
        return evaluate_rational(arena, sides[0], values, left) &&
               evaluate_rational(arena, sides[1], values, right);
    }

    bool apply(const MatrixRowOperation &operation) {
        if (!running())
            return false;
        std::array<Rational, kCells> next = cells;
        const size_t width = columns();
        if (const auto *swap = std::get_if<MatrixRowSwap>(&operation)) {
            for (size_t column = 0; column < width; ++column)
                std::swap(next[swap->first * width + column], next[swap->second * width + column]);
        } else if (const auto *scale = std::get_if<MatrixRowScale>(&operation)) {
            for (size_t column = 0; column < width; ++column) {
                Rational &entry = next[scale->row * width + column];
                if (!rational_mul(entry, scale->factor, &entry))
                    return out_of_arithmetic();
            }
        } else {
            const auto &addition = std::get<MatrixRowAddMultiple>(operation);
            for (size_t column = 0; column < width; ++column) {
                Rational term;
                Rational &entry = next[addition.target * width + column];
                if (!rational_mul(next[addition.source * width + column], addition.factor, &term) ||
                    !rational_add(entry, term, &entry))
                    return out_of_arithmetic();
            }
        }
        const std::array<Rational, kCells> previous = cells;
        cells = next;
        const NodeId after = matrix_node();
        if (!running())
            return false;
        const MatrixRowRecord record = record_matrix_row(arena, derivation, meter, matrix, after, operation, plan);
        if (!running())
            return false;
        const VerificationOutcome outcome = record.check.verification.outcome;
        if (outcome != VerificationOutcome::Passed) {
            cells = previous;
            return refuse(outcome == VerificationOutcome::Failed ? SystemOutcome::VerificationFailed
                                                                 : SystemOutcome::ResourceExceeded,
                          record.check.verification.detail);
        }
        if (record.check.changed && record.step == kNoStep) {
            return refuse(SystemOutcome::ResourceExceeded, "the verified row operation could not be recorded");
        }
        matrix = after;
        return true;
    }

    SystemResult finish(SystemOutcome outcome, std::string reason,
                        std::vector<NodeId> solutions = {}) {
        SystemResult result;
        result.outcome = outcome;
        result.detail = std::move(reason);
        if (arena.failed()) {
            result.outcome = SystemOutcome::ResourceExceeded;
            result.detail = status_name(arena.status());
        }
        switch (result.outcome) {
            case SystemOutcome::Solved:
            case SystemOutcome::NoSolution:
            case SystemOutcome::Family:
                result.status = derivation.outcome_from(mark);
                result.solutions = std::move(solutions);
                if (!result.solutions.empty())
                    result.expression = arena.list(result.solutions);
                break;
            case SystemOutcome::NotLinear:
            case SystemOutcome::OutsideEnvelope: result.status = DerivationStatus::Unsupported; break;
            case SystemOutcome::InvalidInput: result.status = DerivationStatus::InvalidInput; break;
            case SystemOutcome::VerificationFailed: result.status = DerivationStatus::VerificationFailed; break;
            case SystemOutcome::Cancelled: result.status = DerivationStatus::Cancelled; break;
            case SystemOutcome::ResourceExceeded: result.status = DerivationStatus::ResourceLimitReached; break;
        }
        if (result.outcome != SystemOutcome::Solved && result.outcome != SystemOutcome::NoSolution &&
            result.outcome != SystemOutcome::Family)
            keep_verified_prefix(derivation, mark, arena);
        result.cost = meter.cost();
        ContextInputs context;
        context.application_version = application_version();
        context.problem_family_id = "algebra.linear-system.elimination";
        context.requested_method = "elimination";
        context.normalized_problem_model = equations;
        context.original_expression = derivation.request.original_expression;
        context.normalized_expression = equations < arena.node_count() ? print(arena, equations) : std::string();
        context.numeric_mode = derivation.request.numeric_mode;
        context.angle_convention = "not applicable";
        context.branch_convention = "exact rational row operations";
        context.detail_projection = "standard";
        context.resource_policy = budget_policy(meter.budget());
        context.derivation_status = result.status;
        derivation.context = make_context(context);
        derivation.context.problem_family_envelope_version = "1";
        return result;
    }

    SystemResult stopped() { return finish(failure, detail); }

    // The offered answer goes back into every equation as it was typed. A family is sampled at
    // several values of its free unknowns, which corroborates it and does not prove it.
    SystemResult conclude(const std::vector<NodeId> &solutions, const std::vector<bool> &is_free) {
        if (!meter.step()) {
            running();
            return stopped();
        }
        bool family = false;
        for (bool free : is_free)
            family = family || free;
        const size_t samples = family ? 3 : 1;
        VerificationOutcome checked = VerificationOutcome::Passed;
        std::string observed = "every equation holds";
        for (size_t sample = 0; sample < samples && checked == VerificationOutcome::Passed; ++sample) {
            std::array<Rational, kSystemMaxUnknowns> values{};
            std::vector<SymbolValue> free_values;
            for (size_t i = 0; i < n(); ++i) {
                if (!is_free[i])
                    continue;
                values[i] = Rational{static_cast<int64_t>(sample * 3 + i + 1), 1};
                free_values.push_back({unknown_name(i), values[i]});
            }
            for (size_t i = 0; i < n() && checked == VerificationOutcome::Passed; ++i) {
                if (!is_free[i] &&
                    !evaluate_rational(arena, arena.children(solutions[i])[1], free_values, &values[i])) {
                    checked = VerificationOutcome::Inconclusive;
                    observed = "the value offered for " + unknown_name(i) + " could not be evaluated exactly";
                }
            }
            const std::vector<SymbolValue> assignment = this->assignment(values);
            for (size_t row = 0; row < m() && checked == VerificationOutcome::Passed; ++row) {
                Rational left, right;
                if (!evaluate_sides(row, assignment, &left, &right)) {
                    checked = VerificationOutcome::Inconclusive;
                    observed = "equation " + std::to_string(row + 1) + " could not be evaluated exactly";
                } else if (!rational_equal(left, right)) {
                    checked = VerificationOutcome::Failed;
                    observed = "equation " + std::to_string(row + 1) + " does not hold";
                }
            }
        }
        const EvidenceStrength passing = family ? EvidenceStrength::NumericallyCorroborated : EvidenceStrength::CandidateChecked;
        Step substitute;
        substitute.phase = "Check";
        substitute.goal = "Check the answer in every original equation";
        substitute.rule_id = family ? "system.check-family-by-sampling" : "system.check-by-substitution";
        substitute.rule_name = family ? "Check the family at several values" : "Check by substitution";
        substitute.claim = ClaimType::SolutionSetPreserved;
        substitute.explanation_short = family ? "Pick values for the free unknowns and check every equation."
                                              : "Put the values back into every equation as it was typed.";
        substitute.explanation_detailed = family
            ? "Three choices of the free unknowns each have to satisfy every original equation. Agreement at sample values supports the family and is weaker than a proof."
            : "Both sides of each original equation have to come out equal.";
        substitute.proof_obligations.push_back({"obl.system.candidate-satisfies",
            "the solution satisfies every equation of the system as typed"});
        substitute.verifications.push_back({family ? "substitution at sampled free values" : "substitution",
            checked, strength_for(checked, passing), observed, "obl.system.candidate-satisfies"});
        CheckPayload check;
        check.target_claim = print(arena, arena.list(solutions)) + " satisfies every equation";
        check.check_method = family ? "substitute sampled values of the free unknowns" : "substitute the solution into every equation as typed";
        check.expected_relation = "both sides of every equation equal";
        check.observed_result = observed;
        derivation.add_check(plan, std::move(substitute), std::move(check));
        if (!running())
            return stopped();
        if (checked != VerificationOutcome::Passed)
            return finish(checked == VerificationOutcome::Failed ? SystemOutcome::VerificationFailed
                                                                 : SystemOutcome::ResourceExceeded,
                          checked == VerificationOutcome::Failed ? "the answer failed its substitution check, so it is not offered"
                                                                 : "checking the answer exceeded exact arithmetic, so it is not offered");
        return finish(family ? SystemOutcome::Family : SystemOutcome::Solved,
                      family ? "infinitely many solutions, one for each value of the free unknowns"
                             : "every equation holds at the solution",
                      solutions);
    }
};

const char *read_refusal(LinearRowRead read) {
    switch (read) {
        case LinearRowRead::NotLinear: return "is not linear in the unknowns";
        case LinearRowRead::OtherSymbol: return "has a symbol that is not one of the unknowns";
        case LinearRowRead::Inexact: return "has a coefficient that is not an exact rational";
        case LinearRowRead::Overflowed: return "has a coefficient beyond exact arithmetic";
        case LinearRowRead::Halted:
        case LinearRowRead::Read: break;
    }
    return "could not be read";
}

}  // namespace

const char *system_outcome_name(SystemOutcome outcome) {
    switch (outcome) {
        case SystemOutcome::Solved: return "solved";
        case SystemOutcome::NoSolution: return "no solution";
        case SystemOutcome::Family: return "solution family";
        case SystemOutcome::NotLinear: return "not linear";
        case SystemOutcome::InvalidInput: return "invalid input";
        case SystemOutcome::OutsideEnvelope: return "outside envelope";
        case SystemOutcome::VerificationFailed: return "verification failed";
        case SystemOutcome::Cancelled: return "cancelled";
        case SystemOutcome::ResourceExceeded: return "resource exceeded";
    }
    return "unknown";
}

LinearRowRead read_linear_row(const Arena &arena, NodeId equation, std::span<const NodeId> unknowns,
                              Meter &meter, std::span<Rational> row) {
    const size_t n = unknowns.size();
    if (arena.failed() || equation >= arena.node_count() || n > kSystemMaxUnknowns ||
        row.size() < n + 1 || arena.at(equation).kind != Kind::Equals ||
        arena.children(equation).size() != 2)
        return LinearRowRead::NotLinear;
    const ChildView sides = arena.children(equation);
    Affine left;
    Affine right;
    LinearRowRead read = read_affine(arena, sides[0], unknowns, meter, 0, &left);
    if (read == LinearRowRead::Read)
        read = read_affine(arena, sides[1], unknowns, meter, 0, &right);
    if (read != LinearRowRead::Read)
        return read;
    for (size_t i = 0; i < n; ++i) {
        if (!rational_sub(left.terms[i], right.terms[i], &row[i]))
            return LinearRowRead::Overflowed;
    }
    if (!rational_sub(right.terms[n], left.terms[n], &row[n]))
        return LinearRowRead::Overflowed;
    return LinearRowRead::Read;
}

SystemResult solve_linear_system(Arena &arena, Derivation &derivation, NodeId equations,
                                 NodeId unknowns, const Budget &budget) {
    Run run(arena, derivation, equations, budget);
    if (!run.running())
        return run.stopped();
    if (equations >= arena.node_count() || unknowns >= arena.node_count())
        return run.finish(SystemOutcome::InvalidInput, "enter a list of equations and a list of unknowns");
    if (arena.at(equations).kind != Kind::List || arena.children(equations).empty())
        return run.finish(SystemOutcome::InvalidInput, "the equations have to be a nonempty list");
    if (arena.at(unknowns).kind != Kind::List || arena.children(unknowns).empty())
        return run.finish(SystemOutcome::InvalidInput, "the unknowns have to be a nonempty list");
    for (NodeId item : arena.children(equations)) {
        if (arena.at(item).kind != Kind::Equals || arena.children(item).size() != 2)
            return run.finish(SystemOutcome::InvalidInput, "every item in the system has to be an equation");
        run.rows.push_back(item);
    }
    for (NodeId name : arena.children(unknowns)) {
        if (arena.at(name).kind != Kind::Symbol)
            return run.finish(SystemOutcome::InvalidInput, "every unknown has to be a single identifier");
        for (NodeId seen : run.unknowns) {
            if (arena.text(seen) == arena.text(name))
                return run.finish(SystemOutcome::InvalidInput, "the unknown " + arena.text(name) + " is named twice");
        }
        run.unknowns.push_back(name);
    }
    if (run.m() > kSystemMaxEquations || run.n() > kSystemMaxUnknowns)
        return run.finish(SystemOutcome::OutsideEnvelope,
                          "the augmented matrix has to fit 4 equations and 5 unknowns");
    if (derivation.request.numeric_mode != NumericMode::Exact)
        return run.finish(SystemOutcome::OutsideEnvelope, "linear system walkthroughs require Exact mode");

    for (size_t row = 0; row < run.m(); ++row) {
        const LinearRowRead read = read_linear_row(
            arena, run.rows[row], run.unknowns, run.meter,
            std::span<Rational>(run.cells.data() + row * run.columns(), run.columns()));
        if (read == LinearRowRead::Halted) {
            run.running();
            return run.stopped();
        }
        if (read != LinearRowRead::Read) {
            const SystemOutcome outcome = read == LinearRowRead::NotLinear ? SystemOutcome::NotLinear :
                read == LinearRowRead::Overflowed ? SystemOutcome::ResourceExceeded : SystemOutcome::OutsideEnvelope;
            return run.finish(outcome, "equation " + std::to_string(row + 1) + " " + read_refusal(read));
        }
    }
    run.matrix = run.matrix_node();
    if (!run.running())
        return run.stopped();

    if (!run.meter.step()) {
        run.running();
        return run.stopped();
    }
    Step step;
    step.phase = "Plan";
    step.goal = "Solve " + print(arena, equations) + " for " + print(arena, unknowns);
    step.rule_id = "plan.system-elimination";
    step.rule_name = "Eliminate on the augmented matrix";
    step.explanation_short = "Write the system as an augmented matrix and reduce it by row operations.";
    step.explanation_detailed = "Each row is one equation and each column one unknown, with the right-hand sides last. "
        "Row operations change the equations without changing their common solutions.";
    PlanPayload payload;
    payload.strategy_id = step.rule_id;
    payload.selected_strategy = step.rule_name;
    payload.matched_problem_facts.push_back(print(arena, equations));
    payload.selection_rationale = "Every equation is linear in the unknowns with exact rational coefficients.";
    register_strategy_precondition(payload, step, "pre.system.linear-rational",
        "every equation is linear in the unknowns with exact rational coefficients, in at most 4 equations and 5 unknowns",
        "exact linear system analysis", EvidenceStrength::StructurallyValid, VerificationOutcome::Passed,
        step.goal);
    run.plan = derivation.add_plan(kNoStep, std::move(step), std::move(payload));
    if (!run.running())
        return run.stopped();

    // The rows were read by this engine, so they are checked against an independent evaluation of
    // every equation as typed, at the origin, each unit point and one point off every axis.
    if (!run.meter.step()) {
        run.running();
        return run.stopped();
    }
    VerificationRecord represented{"exact evaluation at affinely independent points", VerificationOutcome::Passed,
        EvidenceStrength::StructurallyValid,
        "every equation as typed agrees with its matrix row at the origin, each unit point and a point off every axis",
        "obl.system.rows-represent"};
    for (size_t point = 0; point <= run.n() + 1 && represented.outcome == VerificationOutcome::Passed; ++point) {
        std::array<Rational, kSystemMaxUnknowns> values{};
        for (size_t i = 0; i < run.n(); ++i) {
            const int64_t off_axis = static_cast<int64_t>(2 * i + 3);
            values[i] = Rational{point == run.n() + 1 ? off_axis : point == i + 1 ? 1 : 0, 1};
        }
        const std::vector<SymbolValue> assignment = run.assignment(values);
        for (size_t row = 0; row < run.m(); ++row) {
            Rational left, right, typed, recorded{0, 1};
            bool ok = run.evaluate_sides(row, assignment, &left, &right) && rational_sub(left, right, &typed);
            for (size_t i = 0; ok && i < run.n(); ++i) {
                Rational term;
                ok = rational_mul(run.cell(row, i), values[i], &term) && rational_add(recorded, term, &recorded);
            }
            ok = ok && rational_sub(recorded, run.cell(row, run.n()), &recorded);
            if (!ok) {
                represented.outcome = VerificationOutcome::Inconclusive;
                represented.strength = strength_for(VerificationOutcome::Inconclusive, EvidenceStrength::StructurallyValid);
                represented.detail = "an equation could not be evaluated in exact arithmetic";
                break;
            }
            if (!rational_equal(typed, recorded)) {
                represented.outcome = VerificationOutcome::Failed;
                represented.strength = EvidenceStrength::Failed;
                represented.detail = "equation " + std::to_string(row + 1) + " differs from its matrix row";
                break;
            }
        }
    }
    Step write;
    write.phase = "Set up";
    write.goal = "Write the system as an augmented matrix";
    write.rule_id = "system.augmented-matrix";
    write.rule_name = "Write the augmented matrix";
    write.claim = ClaimType::SolutionSetPreserved;
    write.explanation_short = "Put each equation's coefficients in a row, with its right-hand side last.";
    write.explanation_detailed = "Move every unknown to the left and every number to the right first. "
        "An unknown missing from an equation has coefficient zero.";
    write.proof_obligations.push_back({"obl.system.rows-represent",
        "each matrix row has the coefficients and right-hand side of its equation"});
    write.verifications.push_back(represented);
    TransformationPayload change;
    change.before = equations;
    change.after = run.matrix;
    change.reversible = true;
    change.concrete_action = "Write the augmented matrix " + print(arena, run.matrix) + ".";
    derivation.add_transformation(run.plan, std::move(write), std::move(change));
    if (!run.running())
        return run.stopped();
    if (represented.outcome != VerificationOutcome::Passed)
        return run.finish(represented.outcome == VerificationOutcome::Failed ? SystemOutcome::VerificationFailed
                                                                             : SystemOutcome::ResourceExceeded,
                          represented.detail);

    // Gauss-Jordan over every column, the right-hand side included, so an inconsistent row reduces
    // to a leading one in the last column and the final matrix is in reduced row echelon form.
    std::vector<size_t> pivots;
    size_t pivot_row = 0;
    for (size_t column = 0; column < run.columns() && pivot_row < run.m(); ++column) {
        size_t found = pivot_row;
        while (found < run.m() && is_zero(run.cell(found, column)))
            ++found;
        if (found == run.m())
            continue;
        if (found != pivot_row && !run.apply(MatrixRowSwap{found, pivot_row}))
            return run.stopped();
        const Rational leading = run.cell(pivot_row, column);
        if (!rational_equal(leading, Rational{1, 1})) {
            Rational inverse;
            if (!rational_div(Rational{1, 1}, leading, &inverse)) {
                run.out_of_arithmetic();
                return run.stopped();
            }
            if (!run.apply(MatrixRowScale{pivot_row, inverse}))
                return run.stopped();
        }
        for (size_t row = 0; row < run.m(); ++row) {
            if (row == pivot_row || is_zero(run.cell(row, column)))
                continue;
            Rational factor;
            if (!rational_mul(run.cell(row, column), Rational{-1, 1}, &factor)) {
                run.out_of_arithmetic();
                return run.stopped();
            }
            if (!run.apply(MatrixRowAddMultiple{row, pivot_row, factor}))
                return run.stopped();
        }
        pivots.push_back(column);
        ++pivot_row;
    }

    if (!run.meter.step()) {
        run.running();
        return run.stopped();
    }
    VerificationRecord form = verify_matrix_form(arena, run.matrix, MatrixForm::ReducedEchelon);
    if (form.outcome != VerificationOutcome::Passed)
        return run.finish(form.outcome == VerificationOutcome::Failed ? SystemOutcome::VerificationFailed
                                                                      : SystemOutcome::ResourceExceeded,
                          form.detail);
    Step conclusion;
    conclusion.phase = "Check the final matrix";
    conclusion.goal = "Verify the reduced augmented matrix";
    conclusion.rule_id = "matrix.rref-conclusion";
    conclusion.rule_name = "Check reduced row echelon form";
    conclusion.claim = ClaimType::RowEquivalent;
    conclusion.explanation_short = "Write the final matrix: " + print(arena, run.matrix);
    conclusion.explanation_detailed = form.detail + ". Every recorded row operation was checked exactly.";
    conclusion.proof_obligations.push_back({"obl.matrix.trace-complete", "the final matrix ends a complete verified row-operation trace"});
    conclusion.verifications.push_back({"exact row trace continuity", VerificationOutcome::Passed,
        EvidenceStrength::StructurallyValid, "each row operation starts at the preceding verified matrix",
        "obl.matrix.trace-complete"});
    form.evidence_id = "obl.matrix.rref-form";
    conclusion.proof_obligations.push_back({form.evidence_id, "the final matrix satisfies exact reduced row echelon form"});
    conclusion.verifications.push_back(form);
    CheckPayload reduced;
    reduced.target_claim = "The final matrix is row equivalent to the augmented matrix and is reduced";
    reduced.check_method = form.method;
    reduced.expected_relation = "reduced row echelon form";
    reduced.observed_result = print(arena, run.matrix);
    derivation.add_check(run.plan, std::move(conclusion), std::move(reduced));
    if (!run.running())
        return run.stopped();

    if (!pivots.empty() && pivots.back() == run.n()) {
        if (!run.meter.step()) {
            run.running();
            return run.stopped();
        }
        const size_t row = pivots.size() - 1;
        bool zero_coefficients = true;
        for (size_t i = 0; i < run.n(); ++i)
            zero_coefficients = zero_coefficients && is_zero(run.cell(row, i));
        const bool contradiction = zero_coefficients && !is_zero(run.cell(row, run.n()));
        Step inconsistent;
        inconsistent.phase = "Read the solution";
        inconsistent.goal = "Read the row that has no unknown left in it";
        inconsistent.rule_id = "system.inconsistent-row";
        inconsistent.rule_name = "Read an inconsistent row";
        inconsistent.claim = ClaimType::SolutionSetPreserved;
        inconsistent.explanation_short = "Row " + std::to_string(row + 1) + " says 0 = " +
            print(arena, canonical_rational(arena, run.cell(row, run.n()))) + ", which no values make true.";
        inconsistent.explanation_detailed = "Row operations keep the solutions of the system, so a system with a false row has none.";
        inconsistent.proof_obligations.push_back({"obl.system.contradiction",
            "a row of the reduced matrix has every coefficient zero and a nonzero right-hand side"});
        inconsistent.verifications.push_back({"exact reduced row reading",
            contradiction ? VerificationOutcome::Passed : VerificationOutcome::Failed,
            contradiction ? EvidenceStrength::StructurallyValid : EvidenceStrength::Failed,
            contradiction ? "every coefficient in the row is zero and its right-hand side is not"
                          : "the row still has a coefficient or reads zero equals zero",
            "obl.system.contradiction"});
        CheckPayload none;
        none.target_claim = "The system has no solution";
        none.check_method = "exact reduced row reading";
        none.expected_relation = "a row reads zero equals a nonzero number";
        none.observed_result = print(arena, arena.children(run.matrix)[row]);
        derivation.add_check(run.plan, std::move(inconsistent), std::move(none));
        if (!run.running())
            return run.stopped();
        if (!contradiction)
            return run.finish(SystemOutcome::VerificationFailed, "the reduced matrix does not show a contradiction");
        return run.finish(SystemOutcome::NoSolution, "the reduced matrix has a row reading 0 = 1, so no values satisfy every equation");
    }

    // Each pivot row reads its unknown as the right-hand side minus the free unknowns it still has.
    std::vector<bool> is_pivot(run.n(), false);
    for (size_t column : pivots)
        is_pivot[column] = true;
    std::vector<NodeId> solutions(run.n(), kNoNode);
    for (size_t row = 0; row < pivots.size(); ++row) {
        std::vector<NodeId> terms;
        if (!is_zero(run.cell(row, run.n())) || pivots.size() == run.n())
            terms.push_back(canonical_rational(arena, run.cell(row, run.n())));
        for (size_t free = 0; free < run.n(); ++free) {
            if (is_pivot[free] || is_zero(run.cell(row, free)))
                continue;
            Rational coefficient;
            if (!rational_mul(run.cell(row, free), Rational{-1, 1}, &coefficient)) {
                run.out_of_arithmetic();
                return run.stopped();
            }
            terms.push_back(arena.binary(Kind::Mul, canonical_rational(arena, coefficient), run.unknowns[free]));
        }
        const NodeId value = terms.empty() ? arena.integer("0") : terms.size() == 1 ? terms[0] : arena.nary(Kind::Add, terms);
        solutions[pivots[row]] = arena.binary(Kind::Equals, run.unknowns[pivots[row]], value);
    }
    for (size_t free = 0; free < run.n(); ++free) {
        if (!is_pivot[free])
            solutions[free] = arena.binary(Kind::Equals, run.unknowns[free], run.unknowns[free]);
    }
    if (!run.running())
        return run.stopped();
    const bool family = pivots.size() < run.n();

    if (!run.meter.step()) {
        run.running();
        return run.stopped();
    }
    Step reading;
    reading.phase = "Read the solution";
    reading.goal = family ? "Write each leading unknown in terms of the free unknowns" : "Read each unknown from its row";
    reading.rule_id = "system.read-solution";
    reading.rule_name = "Read the solution from the reduced matrix";
    reading.claim = ClaimType::SolutionSetPreserved;
    reading.explanation_short = family ? "Each row gives a leading unknown. The unknowns with no leading one are free."
                                       : "Each row now says one unknown equals a number.";
    reading.explanation_detailed = "A reduced row with a leading one in an unknown's column reads that unknown "
        "as the right-hand side minus the other terms in the row.";
    reading.proof_obligations.push_back({"obl.system.rows-read",
        "each solution equation is the reduced row of its leading unknown"});
    reading.verifications.push_back({"exact reduced row reading", VerificationOutcome::Passed,
        EvidenceStrength::StructurallyValid,
        family ? "every leading unknown is written from its row and every other unknown is free"
               : "every unknown has a row with a leading one and a number on the right",
        "obl.system.rows-read"});
    TransformationPayload read_off;
    read_off.before = run.matrix;
    read_off.after = arena.list(solutions);
    read_off.reversible = true;
    read_off.concrete_action = "Read " + print(arena, read_off.after) + ".";
    derivation.add_transformation(run.plan, std::move(reading), std::move(read_off));
    if (!run.running())
        return run.stopped();

    std::vector<bool> is_free(run.n(), false);
    for (size_t i = 0; i < run.n(); ++i)
        is_free[i] = !is_pivot[i];
    return run.conclude(solutions, is_free);
}

}  // namespace nps
