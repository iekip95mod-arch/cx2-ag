#include "nps/ui/retained_menu.h"

#include <array>

namespace nps::ui {

RetainedMenu::RetainedMenu(int width, int height, std::span<uint16_t> pixels, std::string_view title,
                           std::span<const std::string_view> labels, std::span<const std::string_view> descriptions) {
    if (labels.empty() || labels.size() > max_entries ||
        (!descriptions.empty() && descriptions.size() != labels.size())) return;
    std::array<nps_retained_text, max_entries> entries{};
    std::array<nps_retained_text, max_entries> details{};
    for (size_t i = 0; i < labels.size(); ++i) entries[i] = {labels[i].data(), labels[i].size()};
    for (size_t i = 0; i < descriptions.size(); ++i) details[i] = {descriptions[i].data(), descriptions[i].size()};
    nps_retained_menu *created = nullptr;
    status_ = nps_retained_menu_create(width, height, pixels.data(), pixels.size(), {title.data(), title.size()},
                                      entries.data(), descriptions.empty() ? nullptr : details.data(), labels.size(), &created);
    menu_.reset(created);
    if (status_ == NPS_RETAINED_READY)
        pixels_ = pixels.first(static_cast<size_t>(width) * static_cast<size_t>(height));
}

RetainedMenu::~RetainedMenu() = default;

void RetainedMenu::Deleter::operator()(nps_retained_menu *menu) const noexcept {
    nps_retained_menu_destroy(menu);
}

nps_retained_status RetainedMenu::status() const {
    const auto failure = nps_retained_failure();
    return failure == NPS_RETAINED_READY ? status_ : failure;
}

bool RetainedMenu::render() {
    if (status() != NPS_RETAINED_READY) return false;
    uint64_t rendered_revision = revision_;
    size_t pool_used = pool_used_;
    status_ = nps_retained_menu_render(menu_.get(), &rendered_revision, &pool_used);
    if (status_ != NPS_RETAINED_READY) return false;
    const bool changed = revision_ != rendered_revision;
    revision_ = rendered_revision;
    pool_used_ = pool_used;
    return changed;
}

bool RetainedMenu::select(size_t index) {
    if (status() != NPS_RETAINED_READY) return false;
    const auto selected_status = nps_retained_menu_select(menu_.get(), index);
    if (selected_status == NPS_RETAINED_INVALID_INPUT) return false;
    status_ = selected_status;
    if (status_ != NPS_RETAINED_READY) return false;
    selected_ = index;
    return true;
}

bool RetainedMenu::scroll(int pixels) {
    if (status() != NPS_RETAINED_READY) return false;
    size_t selected = selected_;
    status_ = nps_retained_menu_scroll(menu_.get(), pixels, &selected);
    if (status_ != NPS_RETAINED_READY) return false;
    selected_ = selected;
    return true;
}

std::span<const uint16_t> RetainedMenu::pixels() const {
    return status() == NPS_RETAINED_READY && revision_ ? pixels_ : std::span<const uint16_t>{};
}

}
