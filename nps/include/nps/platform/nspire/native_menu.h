#ifndef NPS_PLATFORM_NSPIRE_NATIVE_MENU_H
#define NPS_PLATFORM_NSPIRE_NATIVE_MENU_H

#include <cstddef>

namespace nps {

// Giac's kdisplay draws the framework's own list menu. It lives behind this narrow function because
// kdisplay.h pulls in the whole of Giac and redefines the COLOR_ names, which nothing calling it
// wants. Returns the one-based index of the chosen item, or zero when the menu was dismissed.
int show_native_menu(const char *title, const char *const *items, std::size_t count);

// doMenu's ways out, repeated from kdisplay.h and k_defs.h because those cannot be included off the
// device. native_menu.cc static_asserts each against the real definition.
constexpr int kNativeMenuOutcomeExit = 0;
constexpr int kNativeMenuOutcomeShutdown = 32109;
constexpr int kNativeMenuOutcomeBook = 31103;

// The caller item a doMenu outcome names, or zero for a dismissal. Split out so the key policy can
// be tested where Giac's headers are not available.
int native_menu_choice(int outcome, int selection, std::size_t caller_item_count);

}  // namespace nps

#endif
