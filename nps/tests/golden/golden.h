#ifndef NPS_TEST_GOLDEN_H
#define NPS_TEST_GOLDEN_H

#include <string>

#include "nps/core/ast.h"
#include "nps/steps/derivation.h"
#include "unit/adapter_tests.h"

namespace nps {

// PRD section 19.2 wants golden tests to compare semantic step records rather than screen text.
// This is that record: every field the derivation model carries, in tree order, with expressions
// printed through the canonical printer and nesting shown by indentation. Nothing here comes from a
// renderer, so a change to how a step is displayed cannot move a fixture, and a change to which rule
// fired or what it claimed cannot avoid moving one.
//
// A field that is empty is left out rather than written blank, which is derivation.h's own rule: a
// record does not carry a field its kind has no answer for. So a golden shows the fields that were
// filled, and a step that stops filling one loses a line.
std::string render_derivation(const Arena &arena, const Derivation &derivation);

// Compares a record against test/golden/<name>.txt and reports a line diff on mismatch.
//
// NPS_REGOLD=1 or the CMake regold target rewrites fixtures and fails checks to require review.
//
// NPS_GOLDEN_DIR overrides the directory for a binary not run from the project root.
void check_golden(TestSink &sink, const std::string &name, const std::string &record);

// The step invariants of tests/step_invariants.h, over the fixtures rather than over the acceptance
// corpus. They are the same claims and a different population: the fixtures reach density, work,
// vectors, relative motion and catch-up, which the corpus has no family for. A claim that held over
// a thousand corpus cases and was never read against those five is a gap rather than a result.
//
// The pass rides along inside render_derivation, which every fixture already calls with its arena
// and its derivation, so there is nothing to add at the 37 sites. This reports what it collected.
void check_golden_invariants(TestSink &sink);

void run_golden_tests(TestSink &sink);

}  // namespace nps

#endif
