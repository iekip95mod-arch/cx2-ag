#include <fstream>
#include <sstream>
#include <string>

#include "golden/golden.h"
#include "nps/wp/problem_ir.h"
#include "unit/adapter_tests.h"

namespace nps {
namespace {

std::string slurp(const char *path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

wp::IrReadResult corpus(const char *name) {
    return wp::read_problem_ir(slurp((std::string("tests/wp_corpus/") + name).c_str()));
}

std::string fault(const wp::ProblemIR &ir, const wp::SourceDocument &source) {
    return wp::ir_fault_name(wp::validate(ir, source).fault);
}

std::string kinematics_record(const KinematicsProblem &problem) {
    Arena arena;
    Derivation derivation;
    const KinematicsResult result = solve_kinematics(arena, derivation, problem);
    return result.value_text + " " + result.unit_text + "\n" + render_derivation(arena, derivation);
}

std::string density_record(const DensityProblem &problem) {
    Arena arena;
    Derivation derivation;
    const DensityResult result = solve_density(arena, derivation, problem);
    return result.value_text + " " + result.unit_text + "\n" + render_derivation(arena, derivation);
}

void test_equivalence(TestSink &t) {
    const wp::IrReadResult cart = corpus("cart_speed.ir");
    t.equal(wp::ir_read_status_name(cart.status), "ok", "the authored cart problem reads: " + cart.detail);
    wp::IrValidation why;
    const std::optional<wp::CommittedProblem> committed = wp::commit(cart.ir, cart.source, &why);
    t.check(committed.has_value(), "and validates and commits: " + why.detail);
    if (committed) {
        KinematicsProblem from_ir;
        std::string reason;
        t.check(wp::to_kinematics(*committed, &from_ir, &reason), "the committed problem becomes a kinematics problem: " + reason);
        KinematicsProblem direct;
        t.check(parse_kinematics("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s", &direct, &reason),
                "the same problem entered directly parses: " + reason);
        const std::string ir_record = kinematics_record(from_ir);
        t.check(ir_record == kinematics_record(direct) && ir_record.compare(0, 7, "17 m/s\n") == 0,
                "the authored problem reproduces the direct entry's derivation record exactly");
        t.check(committed->ir().unused_information.size() == 1 &&
                    committed->ir().unused_information.front().surface == "red",
                "the colour of the cart is kept as unused information rather than dropped");
        t.check(committed->ir().confirmed_inferred_assumptions.size() == 1 &&
                    !committed->ir().confirmed_inferred_assumptions.front().provenance.explicit_fact,
                "and the constant acceleration is held as an inferred, confirmed assumption");
    }
    const wp::IrReadResult block = corpus("block_density.ir");
    const std::optional<wp::CommittedProblem> density = wp::commit(block.ir, block.source, &why);
    t.check(density.has_value(), "the authored density problem commits: " + why.detail);
    if (density) {
        DensityProblem from_ir;
        std::string reason;
        t.check(wp::to_density(*density, &from_ir, &reason), "and becomes a density problem: " + reason);
        DensityProblem direct;
        direct.unknown = DensityVariable::Density;
        DensityKnown mass, volume;
        mass.variable = DensityVariable::Mass;
        volume.variable = DensityVariable::Volume;
        parse_quantity("2 kg", &mass.quantity, &reason);
        parse_quantity("250 cm^3", &volume.quantity, &reason);
        direct.knowns = {mass, volume};
        const std::string ir_record = density_record(from_ir);
        t.check(ir_record == density_record(direct) && ir_record.find("8000") != std::string::npos,
                "the authored density problem reproduces the structured entry's record: " + ir_record.substr(0, 20));
    }
}

void test_invariants(TestSink &t) {
    const wp::IrReadResult cart = corpus("cart_speed.ir");
    const wp::SourceDocument &source = cart.source;
    t.equal(fault(cart.ir, source), "valid", "the corpus problem is valid as written");
    {
        wp::ProblemIR ir = cart.ir;
        ir.quantities.push_back(ir.quantities.front());
        t.equal(fault(ir, source), "duplicate id", "a repeated id is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.quantities.front().owner_entity_id = "truck";
        t.equal(fault(ir, source), "missing reference", "an owner that is not defined is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.relations.front().operands.push_back("q-x");
        t.equal(fault(ir, source), "missing reference", "a relation naming an undefined quantity is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.knowns.push_back("q-v");
        for (wp::Quantity &q : ir.quantities) {
            if (q.id == "q-v")
                q.value_expression = "17";
        }
        t.equal(fault(ir, source), "known and unknown conflict", "a quantity both known and unknown is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.quantities.front().value_expression.clear();
        t.equal(fault(ir, source), "known and unknown conflict", "a known quantity with no value is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.confirmed_inferred_assumptions.front().confirmation_record_id.clear();
        t.equal(fault(ir, source), "unconfirmed inference", "an inferred assumption with no confirmation is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.quantities[1].unit = "m/s";
        t.equal(fault(ir, source), "dimension mismatch", "an acceleration in metres per second is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.requested_goal = "q-a";
        t.equal(fault(ir, source), "undefined goal", "a goal that is not an unknown is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.requested_method = "bisection";
        t.equal(fault(ir, source), "incompatible method", "a method the family does not offer is refused");
    }
    {
        wp::SourceDocument changed = source;
        changed.original_utf8[2] = 'b';
        t.equal(fault(cart.ir, changed), "source mismatch", "a changed source text no longer matches its hash");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.quantities.front().provenance.supporting_source_spans.front().original_begin += 1;
        t.equal(fault(ir, source), "provenance mismatch", "a span that does not cover its surface text is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.quantities.front().provenance.supporting_source_spans.clear();
        t.equal(fault(ir, source), "missing provenance", "an explicit fact with no span is refused");
    }
    {
        wp::ProblemIR ir = cart.ir;
        wp::Quantity zero = ir.quantities[1];
        zero.id = "q-a-inferred";
        zero.provenance.explicit_fact = false;
        zero.provenance.supporting_source_spans.clear();
        ir.quantities.push_back(zero);
        t.equal(fault(ir, source), "unconfirmed inference", "an inferred quantity with no confirmation is refused");
        ir.quantities.back().provenance.confirmation_record_id = ir.confirmation_record.id;
        t.equal(fault(ir, source), "valid", "and is accepted once the confirmation names it");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.selected_candidate_id = "grammar-1";
        ir.confirmation_record.source_content_hash = ir.source_content_hash;
        ir.confirmation_record.selected_candidate_id = "grammar-1";
        ir.confirmation_record.problem_revision = ir.revision;
        t.equal(fault(ir, source), "valid", "a confirmation bound to this source, candidate and revision is accepted");
        wp::ProblemIR other_hash = ir;
        other_hash.confirmation_record.source_content_hash = wp::source_hash("a different text");
        t.equal(fault(other_hash, source), "confirmation mismatch", "one that approved a different source text is refused");
        wp::ProblemIR other_candidate = ir;
        other_candidate.confirmation_record.selected_candidate_id = "grammar-2";
        t.equal(fault(other_candidate, source), "confirmation mismatch", "and so is one that approved another candidate");
        wp::ProblemIR other_revision = ir;
        other_revision.confirmation_record.problem_revision = ir.revision + 1;
        t.equal(fault(other_revision, source), "confirmation mismatch", "or another revision");
    }
    {
        wp::ProblemIR ir = cart.ir;
        ir.confirmation_record.confirmed = false;
        wp::IrValidation why;
        t.check(fault(ir, source) == "valid" && !wp::commit(ir, source, &why) && why.fault == wp::IrFault::NotConfirmed,
                "a valid but unconfirmed problem does not commit");
    }
}

void test_reading(TestSink &t) {
    const std::string good = slurp("tests/wp_corpus/block_density.ir");
    const auto status = [](const std::string &text) { return std::string(wp::ir_read_status_name(wp::read_problem_ir(text).status)); };
    t.equal(status("not a problem\n"), "not a problem file", "a file without the header is refused");
    std::string newer = good;
    newer.replace(0, 16, "nps-problem-ir 2");
    t.equal(status(newer), "written by a newer schema", "a newer schema is refused by name rather than half read");
    std::string older = good;
    older.replace(0, 16, "nps-problem-ir 0");
    t.equal(status(older), "unsupported schema version", "and so is an unknown older one");
    t.equal(status(good + "solve q-rho\n"), "unknown record", "a record the schema does not define is refused");
    std::string bad_revision = good;
    const size_t at = bad_revision.find("revision=1");
    bad_revision.replace(at, 10, "revision=x");
    t.equal(status(bad_revision), "malformed", "a revision that is not a number is malformed");
    std::string bound = good;
    const std::string line = "confirmation id=c-block by=author confirmed=yes";
    const size_t confirmation = bound.find(line) + line.size();
    bound.insert(confirmation, " hash=abc candidate=grammar-1 revision=1 assumptions=a-one,q-two versions=wp1-lexicon");
    const wp::IrReadResult read_bound = wp::read_problem_ir(bound);
    const wp::ConfirmationRecord &record = read_bound.ir.confirmation_record;
    t.check(read_bound.status == wp::IrReadStatus::Ok && record.source_content_hash == "abc" &&
                record.selected_candidate_id == "grammar-1" && record.problem_revision == 1 &&
                record.material_assumption_ids.size() == 2 && record.material_assumption_ids[1] == "q-two" &&
                record.parser_versions == "wp1-lexicon",
            "a confirmation line reads what it bound: " + read_bound.detail);
    std::string bad_bound = good;
    bad_bound.insert(confirmation, " revision=0");
    t.equal(status(bad_bound), "malformed", "a confirmed revision of zero is malformed");
}

void test_correction(TestSink &t) {
    const wp::IrReadResult cart = corpus("cart_speed.ir");
    wp::IrValidation why;
    const std::optional<wp::CommittedProblem> first = wp::commit(cart.ir, cart.source, &why);
    if (!first) {
        t.check(false, "the corpus problem commits before it can be corrected: " + why.detail);
        return;
    }
    wp::ProblemIR next = wp::correct(*first, "fix-1");
    t.check(next.revision == 2 && !next.confirmation_record.confirmed && next.correction_lineage.size() == 1 &&
                next.correction_lineage.front() == "cart-speed@1 by fix-1",
            "a correction is a new revision with the old one in its lineage and no confirmation");
    t.check(!wp::commit(next, cart.source, &why) && why.fault == wp::IrFault::UnconfirmedInference,
            "so the inferred assumption confirmed under the old revision no longer counts");
    next.confirmed_inferred_assumptions.front().confirmation_record_id = "c-fix";
    next.confirmation_record = {"c-fix", "author", false};
    t.check(!wp::commit(next, cart.source, &why) && why.fault == wp::IrFault::NotConfirmed,
            "and the corrected problem cannot be solved until it is confirmed again");
    next.confirmation_record.confirmed = true;
    t.check(wp::commit(next, cart.source, &why).has_value(), "and commits once it is: " + why.detail);
    t.check(first->ir().revision == 1 && first->ir().confirmation_record.confirmed,
            "while the first revision is left exactly as it was committed");
}

}  // namespace

void run_problem_ir_tests(TestSink &sink) {
    test_equivalence(sink);
    test_invariants(sink);
    test_reading(sink);
    test_correction(sink);
}

}  // namespace nps
