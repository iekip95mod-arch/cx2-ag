#ifndef NPS_TEST_ADAPTER_TESTS_H
#define NPS_TEST_ADAPTER_TESTS_H

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "nps/steps/derivation.h"

namespace nps {

// Every verification the derivation recorded, outcome and detail together and in order. A harness
// that exposes only a status and a failure count cannot tell inconclusive from not attempted, and
// neither of those reads the sentence, so a detail can revert to a constant with the suite green.
inline std::string verification_transcript(const Derivation &derivation) {
    std::string transcript;
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        for (size_t v = 0; v < step.verifications.size(); ++v) {
            if (!transcript.empty())
                transcript += " | ";
            transcript += verification_outcome_name(step.verifications[v].outcome);
            transcript += ", ";
            transcript += step.verifications[v].detail;
        }
    }
    return transcript;
}

// PRD section 19.9 wants a requirement linked to the test that evidences it, and says a requirement
// with no passing evidence is unmet however well the related examples work. This is that link, and
// it lives on the check itself so a deleted test takes its evidence with it.
struct Evidence {
    std::string requirement;
    std::string group;
    std::string what;
    bool passed = false;
};

struct TestSink {
    int checks = 0;
    std::vector<std::string> failures;
    std::vector<Evidence> evidence_records;
    // Every group that ran, which is not the same as every group that produced evidence. The
    // catalog links a family to its test groups, and telling "this group has no tagged check yet"
    // from "this group does not exist" needs both lists.
    std::vector<std::string> groups_run;
    // The running total as each group opened, so a tally can be taken by subtraction afterwards.
    std::vector<int> group_opened_at;
    // How many times each check sentence ran, keyed by group. A group whose total differs between
    // two platforms parted somewhere inside it, and only a per-sentence tally says where.
    std::map<std::pair<std::string, std::string>, int> label_counts;
    std::string group;

    // Called once per group by the runner, so a test body cannot label its evidence wrongly.
    void begin_group(const char *name) {
        group = name;
        groups_run.push_back(group);
        group_opened_at.push_back(checks);
    }

    void check(bool cond, const std::string &what) {
        ++checks;
        count_label(what);
        if (!cond)
            failures.push_back(what);
    }

    void equal(const std::string &got, const std::string &want, const std::string &what) {
        ++checks;
        count_label(what);
        if (got != want)
            failures.push_back(what + "\n      got  " + got + "\n      want " + want);
    }

    // A check that also stands as evidence for a numbered requirement.
    void evidence(const char *requirement, bool cond, const std::string &what) {
        check(cond, what);
        record_evidence(requirement, cond, what);
    }

    void evidence(const char *requirement, const std::string &got, const std::string &want,
                  const std::string &what) {
        equal(got, want, what);
        record_evidence(requirement, got == want, what);
    }

  private:
    void count_label(const std::string &what) { ++label_counts[{group, what}]; }

    void record_evidence(const char *requirement, bool passed, const std::string &what) {
        Evidence e;
        e.requirement = requirement;
        e.group = group;
        e.what = what;
        e.passed = passed;
        evidence_records.push_back(e);
    }
};

void run_adapter_tests(TestSink &sink);
void run_canonical_tests(TestSink &sink);
void run_derivation_tests(TestSink &sink);
void run_linear_tests(TestSink &sink);
void run_quadratic_tests(TestSink &sink);
void run_rearrange_tests(TestSink &sink);
void run_rewrite_tests(TestSink &sink);
void run_differentiate_tests(TestSink &sink);
void run_integrate_tests(TestSink &sink);
void run_units_tests(TestSink &sink);
void run_density_tests(TestSink &sink);
void run_kinematics_tests(TestSink &sink);
void run_planar_kinematics_tests(TestSink &sink);
void run_relative_motion_tests(TestSink &sink);
void run_unit_conversion_tests(TestSink &sink);
void run_vector_addition_tests(TestSink &sink);
void run_vector_components_tests(TestSink &sink);
void run_work_tests(TestSink &sink);
void run_context_tests(TestSink &sink);
void run_fuzz_tests(TestSink &sink);
void run_golden_tests(TestSink &sink);

}  // namespace nps

#endif
