#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

#include "nps/cas/giac_typed.h"
#include "nps/core/parser.h"
#include "giac.h"

namespace {
int interrupt_mask = 7;
std::array<int, 2> mask_values{};
size_t mask_calls = 0, graphics_resets = 0, checks = 0, failures = 0;
bool expire_on_mask = false, reset_while_masked = true;

void check(bool passed, const std::string &what) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cout << "FAIL " << what << '\n';
    }
}

void expire_timeout() {
    giac::caseval_mod = 1;
    giac::caseval_n = 0;
    giac::caseval_begin = 0;
    giac::caseval_maxtime = -1;
}

struct TimeoutState {
    time_t begin = giac::caseval_begin, current = giac::caseval_current;
    double maximum = giac::caseval_maxtime;
    int count = giac::caseval_n, modulus = giac::caseval_mod;
    int initialized = giac::caseval_unitialized;
    bool control = giac::ctrl_c, interrupted = giac::interrupted;

    ~TimeoutState() {
        giac::caseval_begin = begin;
        giac::caseval_current = current;
        giac::caseval_maxtime = maximum;
        giac::caseval_n = count;
        giac::caseval_mod = modulus;
        giac::caseval_unitialized = initialized;
        giac::ctrl_c = control;
        giac::interrupted = interrupted;
    }
};

void clear_timeout() {
    giac::caseval_mod = 0;
    giac::ctrl_c = false;
    giac::interrupted = false;
}
}

int TCT_Local_Control_Interrupts(int mask) {
    const int previous = interrupt_mask;
    if (mask_calls < mask_values.size()) mask_values[mask_calls] = mask;
    ++mask_calls;
    interrupt_mask = mask;
    if (expire_on_mask) {
        expire_on_mask = false;
        expire_timeout();
    }
    return previous;
}

extern "C" void reset_gc() {
    ++graphics_resets;
    reset_while_masked = reset_while_masked && interrupt_mask == -1;
}

extern "C" const char *giac_caseval(const char *expression) {
    giac::vx_var = giac::identificateur("x");
    return giac::caseval(expression);
}

int main() {
    TimeoutState saved;
    clear_timeout();
    nps::TypedGiacBackend backend;
    nps::Arena warm_arena;
    nps::TypedResult warm_result;
    check(backend.typed({nps::Op::Simplify, nps::parse(warm_arena, "1").root}, warm_arena, &warm_result),
          "the public backend initializes before cancellation is armed");

    expire_timeout();
    bool poll_returned = false;
    try {
        giac::control_c();
        poll_returned = true;
    } catch (const std::exception &) {}
    check(poll_returned && giac::ctrl_c && giac::interrupted,
          "actual TIMEOUT polling sets interruption and returns without throwing");

    for (int prior_mask : {0, 7, -1}) {
        for (bool cancel : {false, true}) {
            clear_timeout();
            nps::Arena arena;
            nps::Request request{nps::Op::Simplify, nps::parse(arena, "x+1").root};
            nps::TypedResult reply;
            interrupt_mask = prior_mask;
            mask_calls = graphics_resets = 0;
            mask_values = {};
            reset_while_masked = true;
            expire_on_mask = cancel;
            bool threw = false, accepted = false;
            std::string message;
            try {
                accepted = backend.typed(request, arena, &reply);
            } catch (const std::runtime_error &error) {
                threw = true;
                message = error.what();
            }
            const std::string label = "mask " + std::to_string(prior_mask) + (cancel ? " cancelled" : " normal");
            check(threw == cancel && (cancel ? giac::interrupted &&
                      message.find("Stopped by user interruption") != std::string::npos :
                      accepted && reply.tag == nps::ResultTag::Exact),
                  label + " reaches the expected actual evaluator outcome");
            check(mask_calls == 2 && mask_values[0] == -1 && mask_values[1] == prior_mask &&
                      interrupt_mask == prior_mask,
                  label + " restores exactly the preexisting interrupt mask");
            check(graphics_resets == 1 && reset_while_masked,
                  label + " releases graphics once before unmasking");
            std::cout << label << " threw=" << threw << " mask_calls=" << mask_calls
                      << " resets=" << graphics_resets << " final_mask=" << interrupt_mask << '\n';
        }
    }
    std::cout << "guard cleanup: " << checks << " checks, " << failures << " failed\n";
    return failures ? 1 : 0;
}
