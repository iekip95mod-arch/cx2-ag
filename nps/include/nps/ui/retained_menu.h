#ifndef NPS_RETAINED_MENU_H
#define NPS_RETAINED_MENU_H

#include "nps/ui/retained_menu_private.h"
#include <memory>
#include <span>
#include <string_view>

namespace nps::ui {

class RetainedMenu {
  public:
    static constexpr size_t max_entries = NPS_RETAINED_MAX_ENTRIES;
    static constexpr size_t max_label_bytes = NPS_RETAINED_MAX_LABEL_BYTES;
    static constexpr size_t pixel_alignment = NPS_RETAINED_PIXEL_ALIGNMENT;
    // Pixel storage must outlive the menu and is publishable only after a successful render.
    RetainedMenu(int width, int height, std::span<uint16_t> pixels, std::string_view title,
                 std::span<const std::string_view> labels, std::span<const std::string_view> descriptions = {});
    ~RetainedMenu();
    RetainedMenu(const RetainedMenu &) = delete;
    RetainedMenu &operator=(const RetainedMenu &) = delete;
    nps_retained_status status() const;
    bool render();
    bool select(size_t index);
    bool scroll(int pixels);
    size_t selected() const { return selected_; }
    uint64_t revision() const { return revision_; }
    size_t pool_used() const { return pool_used_; }
    // Copy or encode this borrowed view before any later renderer operation.
    std::span<const uint16_t> pixels() const;

  private:
    struct Deleter {
        void operator()(nps_retained_menu *menu) const noexcept;
    };
    std::unique_ptr<nps_retained_menu, Deleter> menu_;
    std::span<uint16_t> pixels_;
    nps_retained_status status_ = NPS_RETAINED_INVALID_INPUT;
    uint64_t revision_ = 0;
    size_t selected_ = 0;
    size_t pool_used_ = 0;
};

}
#endif
