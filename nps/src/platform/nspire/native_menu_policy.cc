#include "nps/platform/nspire/native_menu.h"

namespace nps {

int native_menu_choice(int outcome, int selection, std::size_t caller_item_count) {
    if (outcome == kNativeMenuOutcomeExit || outcome == kNativeMenuOutcomeShutdown ||
        outcome == kNativeMenuOutcomeBook)
        return 0;
    if (selection <= 0 || static_cast<std::size_t>(selection) > caller_item_count)
        return 0;
    return selection;
}

}  // namespace nps
