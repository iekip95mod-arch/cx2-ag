#include <iostream>
#include "nps/cas/giac_typed.h"
#include "nps/core/parser.h"
#include "nps/steps/matrix.h"
#include "giac.h"

int mask = 7;
unsigned masks = 0, restores = 0, resets = 0;
bool expire_on_mask = false;
int TCT_Local_Control_Interrupts(int next) {
    const int prior = mask;
    next == -1 ? ++masks : ++restores;
    mask = next;
    if (expire_on_mask) {
        expire_on_mask = false;
        giac::caseval_mod = 1;
        giac::caseval_n = 0;
        giac::caseval_begin = 0;
        giac::caseval_maxtime = -1;
    }
    return prior;
}
extern "C" void reset_gc() { ++resets; }
extern "C" const char *giac_caseval(const char *command) {
    giac::vx_var = giac::identificateur("x");
    return giac::caseval(command);
}

int main() {
    nps::TypedGiacBackend backend;
    std::string value, error;
    backend.eval("0", &value, &error);
    unsigned failures = 0;
    for (int scenario = 0; scenario < 6; ++scenario) {
        giac::caseval_mod = 0;
        giac::ctrl_c = scenario == 1;
        giac::interrupted = scenario == 2;
        expire_on_mask = scenario == 3;
        masks = restores = resets = 0;
        mask = 7;
        nps::Arena arena;
        const auto input = nps::parse(arena, "[[2,4],[0,3]]").root;
        nps::Adapter adapter(arena, backend);
        nps::Derivation derivation;
        nps::Budget budget;
        if (scenario == 4) budget.poll = [](void *) { return true; };
        const auto result = nps::matrix_method(arena, adapter, derivation, input,
                                             nps::MatrixForm::ReducedEchelon, budget);
        const bool balanced = mask == 7 && masks == restores && masks == resets;
        const bool normal = scenario == 0 || scenario == 5;
        const bool correct = normal ? result.outcome == nps::MatrixOutcome::Reduced &&
            result.status == nps::DerivationStatus::SolvedAndVerified && result.expression != nps::kNoNode :
            result.outcome == nps::MatrixOutcome::Cancelled &&
            result.status == nps::DerivationStatus::Cancelled && result.expression == nps::kNoNode;
        failures += !correct || !balanced;
        std::cout << "scenario=" << scenario << " outcome=" << nps::matrix_outcome_name(result.outcome)
                  << " status=" << nps::derivation_status_name(result.status)
                  << " final=" << (result.expression != nps::kNoNode)
                  << " steps=" << derivation.size() << " ctrl=" << giac::ctrl_c
                  << " interrupted=" << giac::interrupted << " guards=" << masks
                  << " restores=" << restores << " resets=" << resets << " balanced=" << balanced
                  << " detail=" << result.detail << '\n';
    }
    giac::caseval_mod = 0;
    giac::ctrl_c = false;
    giac::interrupted = false;
    std::cout << "FAILURES=" << failures << '\n';
    return failures ? 1 : 0;
}
