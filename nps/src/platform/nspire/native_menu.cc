#include "nps/platform/nspire/native_menu.h"

#include <string>
#include <vector>

#include "kdisplay.h"

namespace nps {

static_assert(kNativeMenuOutcomeExit == MENU_RETURN_EXIT, "menu exit outcome moved");
static_assert(kNativeMenuOutcomeShutdown == KEY_SHUTDOWN, "shutdown key code moved");
static_assert(kNativeMenuOutcomeBook == KEY_BOOK, "book key code moved");

int show_native_menu(const char *title, const char *const *items, std::size_t count) {
    if (!items || count == 0)
        return 0;

    std::vector<std::string> text(items, items + count);
    // doMenu answers the doc and home keys by moving the selection to the last item and reporting a
    // choice, so a trailing entry of our own is what a way out lands on rather than a caller's.
    text.push_back("Cancel");
    std::vector<giac::MenuItem> entries(text.size());
    std::string heading = title ? title : "";
    // Menu gives every other field a default but leaves this one indeterminate, and it is read
    // whenever the list is empty. An empty list is refused above, so this is only here so that no
    // member of a struct built here is left holding whatever was on the stack.
    std::string nothing;

    for (std::size_t i = 0; i < text.size(); ++i)
        entries[i].text = text[i].data();

    giac::Menu menu;
    menu.title = heading.data();
    menu.nodatamsg = nothing.data();
    menu.items = entries.data();
    menu.numitems = static_cast<int>(text.size());

    // Only the OK key comes back as MENU_RETURN_SELECTION. Two other keys that also choose an item,
    // and the right arrow, come back as the key code itself with the choice sitting in selection
    // (kdisplay.cc, the KEY_CTRL_EXE case). Testing for MENU_RETURN_SELECTION would read those as a
    // dismissal and throw away a choice the student made, so the test is for the ways out instead.
    return native_menu_choice(giac::doMenu(&menu), menu.selection, count);
}

}  // namespace nps
