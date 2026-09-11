#include "nps/platform/nspire/native_menu.h"

#include <string>

#include "unit/adapter_tests.h"

namespace nps {
namespace {

constexpr int kOutcomeSelection = 1;
constexpr int kOutcomeExe = 30201;

std::string choice(int outcome, int selection, std::size_t caller_item_count) {
    return std::to_string(native_menu_choice(outcome, selection, caller_item_count));
}

}

void run_native_menu_tests(TestSink &sink) {
    sink.equal(choice(kOutcomeSelection, 2, 2), "2", "the OK key on the last item chooses it");
    sink.equal(choice(kOutcomeExe, 1, 2), "1", "a key that chooses reports its item");

    sink.equal(choice(kOutcomeSelection, 3, 2), "0",
               "the doc and home keys land on the trailing entry and dismiss");
    sink.equal(choice(kNativeMenuOutcomeExit, 2, 2), "0", "escape dismisses");
    sink.equal(choice(kNativeMenuOutcomeShutdown, 2, 2), "0", "shutdown dismisses");
    sink.equal(choice(kNativeMenuOutcomeBook, 2, 2), "0", "the right arrow dismisses");
    sink.equal(choice(kOutcomeSelection, 0, 2), "0", "a selection below the first item dismisses");
}

}
