#include <iostream>
#include <string>
#include <vector>
#include "giac.h"
#include "nps/cas/giac_typed.h"

namespace {
int mask;
unsigned guards, restores, resets;
}
int TCT_Local_Control_Interrupts(int value) {
    const int prior = mask;
    value == -1 ? ++guards : ++restores;
    mask = value;
    return prior;
}
extern "C" void reset_gc() { ++resets; }
extern "C" const char *giac_caseval(const char *expression) {
    giac::vx_var = giac::identificateur("x");
    return giac::caseval(expression);
}

int main() {
    auto *context = reinterpret_cast<giac::context *>(const_cast<char *>(giac::caseval("caseval contextptr")));
    const auto callback = giac::my_gprintf;
    const bool shell = giac::os_shell;
    const int step = giac::step_infolevel(context), calc = giac::calc_mode(context);
    const int xcas = giac::xcas_mode(context), python = giac::python_compat(context);
    const bool complex = giac::complex_mode(context), approximate = giac::approx_mode(context);
    const bool radians = giac::angle_radian(context);
    giac::my_gprintf = [](unsigned, const std::string &, const giac::vecteur &, const giac::context *) {};
    struct Case { const char *equation; giac::vecteur roots; };
    const std::vector<Case> cases = {
        {"3*x^2-12=0", giac::makevecteur(giac::gen(-2), giac::gen(2))},
        {"3*x*x-12=0", giac::makevecteur(giac::gen(-2), giac::gen(2))},
        {"4*x^2=1", giac::makevecteur(giac::gen(-1)/giac::gen(2), giac::gen(1)/giac::gen(2))},
        {"x^2=0", giac::makevecteur(giac::gen(0))},
        {"2*x+5=13", giac::makevecteur(giac::gen(4))},
        {"3*x=7", giac::makevecteur(giac::gen(7)/giac::gen(3))},
        {"x^2=-4", giac::vecteur()},
    };
    unsigned failed = 0;
    std::cout << "INITIAL shell=" << shell << " step=" << step << " calc=" << calc
              << " xcas=" << xcas << " python=" << python << '\n';
    for (int level : {0, 1, 2}) {
        giac::step_infolevel(level, context);
        for (bool enabled : {true, false}) {
            const std::string acknowledgment = giac::caseval(enabled ? "shell on" : "shell off");
            std::cout << "MODE " << acknowledgment << " step=" << level << '\n';
            for (const Case &sample : cases) {
                const std::string command = "solve(" + std::string(sample.equation) + ",x)";
                const std::string raw = giac_caseval(command.c_str());
                const giac::gen parsed = giac::eval(giac::gen(raw, context), 1, context);
                bool exact = parsed.type == giac::_VECT && parsed._VECTptr->size() == sample.roots.size();
                if (exact) {
                    for (const giac::gen &expected : sample.roots) {
                        unsigned matches = 0;
                        for (const giac::gen &actual : *parsed._VECTptr)
                            matches += giac::is_zero(giac::normal(actual - expected, context));
                        exact = exact && matches == 1;
                    }
                }
                failed += !exact;
                std::cout << command << " raw=" << raw << " subtype=" << static_cast<int>(parsed.subtype)
                          << " expected=" << giac::gen(sample.roots, 0).print(context)
                          << " exact_set=" << exact << '\n';
            }
            std::string report;
            const int disagreements = nps::typed_differential_check(&report);
            const int expected = 0;
            failed += disagreements != expected;
            std::cout << report << "DISAGREEMENTS actual=" << disagreements << " expected=" << expected << '\n';
        }
    }
    giac::step_infolevel(step, context);
    giac::caseval(shell ? "shell on" : "shell off");
    giac::my_gprintf = callback;
    const bool restored = giac::os_shell == shell && giac::step_infolevel(context) == step &&
        giac::calc_mode(context) == calc && giac::xcas_mode(context) == xcas &&
        giac::python_compat(context) == python && giac::complex_mode(context) == complex &&
        giac::approx_mode(context) == approximate && giac::angle_radian(context) == radians &&
        giac::my_gprintf == callback && mask == 0 && guards == restores && guards == resets;
    std::cout << "FINAL failures=" << failed << " context_restored=" << restored
              << " guards=" << guards << " restores=" << restores << " resets=" << resets << '\n';
    return failed || !restored;
}
