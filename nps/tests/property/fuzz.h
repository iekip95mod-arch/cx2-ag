#ifndef NPS_TEST_FUZZ_H
#define NPS_TEST_FUZZ_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nps/core/budgets.h"
#include "unit/adapter_tests.h"

namespace nps {

// One generated case. Everything in here is a pure function of the seed, so a report that carries
// the seed carries the input with it and nothing has to be written to disk to reproduce a failure.
struct FuzzCase {
    uint64_t seed = 0;
    const char *profile = "";
    Limits limits;
    std::string input;
    // A second spelling of the same expression. Empty for the profiles that are generated to be
    // refused, because a rejected input has no meaning for two spellings to agree on.
    std::string twin;
};

FuzzCase fuzz_case(uint64_t seed);

enum class Property : uint8_t {
    ParseStatus,
    NodeBudget,
    RoundTrip,
    RewriteBudget,
    RewriteResult,
    CanonicalIdempotent,
    CanonicalRoundTrip,
    TwinParse,
    TwinCanonical,
};

const size_t kPropertyCount = 9;
const char *property_name(Property p);

struct FuzzFailure {
    uint64_t seed = 0;
    Property property = Property::ParseStatus;
    const char *profile = "";
    std::string input;
    std::string detail;
};

struct FuzzReport {
    uint64_t seed = 0;
    size_t cases = 0;
    size_t parsed = 0;
    size_t depth_exceeded = 0;
    size_t size_exceeded = 0;
    size_t input_too_long = 0;
    size_t syntax_error = 0;
    size_t twins = 0;
    size_t fold_limited = 0;
    size_t rewrites = 0;
    size_t failed = 0;
    size_t by_property[kPropertyCount] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    // The first few of each property, so one systematic failure cannot crowd out a rare one and
    // nothing builds a ten thousand entry vector. failed and by_property are the real counts.
    std::vector<FuzzFailure> failures;
};

FuzzReport fuzz_run(uint64_t seed, size_t cases, bool progress = false);

std::string fuzz_escape(const std::string &s);
std::string fuzz_describe(const FuzzFailure &f);

void run_fuzz_tests(TestSink &sink);

}  // namespace nps

#endif
