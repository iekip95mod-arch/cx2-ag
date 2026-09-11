#include <cstdio>
#include <cstdlib>

#include "property/fuzz.h"

// PRD section 20.2 criterion 12: at least 10,000 host generated bounded expressions through the
// parser and the rewrite path with no crash, no hang and no unbounded rewrite loop. Run it with a
// case count and a seed, both optional, because a failure reproduces from its seed alone.
int main(int argc, char **argv) {
    unsigned long long cases = 10000;
    unsigned long long seed = 1;
    if (argc > 1)
        cases = strtoull(argv[1], NULL, 10);
    if (argc > 2)
        seed = strtoull(argv[2], NULL, 10);

    nps::FuzzReport r = nps::fuzz_run(seed, static_cast<size_t>(cases), true);

    printf("nps fuzz: seed %llu, %zu cases, %zu failures\n", seed, r.cases, r.failed);
    printf("  parsed %zu, syntax error %zu, depth exceeded %zu, size exceeded %zu, "
           "input too long %zu\n",
           r.parsed, r.syntax_error, r.depth_exceeded, r.size_exceeded, r.input_too_long);
    printf("  twins compared %zu (%zu skipped at the fold boundary), rewrites run %zu\n", r.twins,
           r.fold_limited, r.rewrites);
    for (size_t i = 0; i < nps::kPropertyCount; ++i) {
        if (r.by_property[i] != 0)
            printf("  %zu failed %s\n", r.by_property[i],
                   nps::property_name(static_cast<nps::Property>(i)));
    }

    for (size_t i = 0; i < r.failures.size(); ++i)
        printf("  FAIL %s\n", nps::fuzz_describe(r.failures[i]).c_str());
    if (r.failed > r.failures.size())
        printf("  ... and %zu more failures not shown\n", r.failed - r.failures.size());

    return r.failed == 0 ? 0 : 1;
}
