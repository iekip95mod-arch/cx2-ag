#include "nps/ui/retained_surface.h"

#include <algorithm>

namespace nps::ui {

RetainedSurface::RetainedSurface(int width, int height, std::span<uint16_t> pixels) {
    if (nps_retained_failure() != NPS_RETAINED_READY) return;
    if (width < 32 || height < 32 || width > 320 || height > 240) return;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixels.size() < count || reinterpret_cast<uintptr_t>(pixels.data()) % LV_DRAW_BUF_ALIGN != 0) return;
    pixels_ = pixels.first(count);
    std::fill(pixels_.begin(), pixels_.end(), 0);
    if (!lv_is_initialized()) lv_init();
    display_.reset(lv_display_create(width, height));
    if (!display_) return;
    width_ = width;
    height_ = height;
    lv_display_set_user_data(display_.get(), this);
    lv_display_set_color_format(display_.get(), LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display_.get(), pixels_.data(), nullptr, static_cast<uint32_t>(count * sizeof(uint16_t)),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display_.get(), flush);
}

RetainedSurface::~RetainedSurface() = default;

void RetainedSurface::DisplayDeleter::operator()(lv_display_t *display) const noexcept {
    if (nps_retained_failure() == NPS_RETAINED_READY) lv_display_delete(display);
}

lv_obj_t *RetainedSurface::root() const {
    return good() ? lv_display_get_screen_active(display_.get()) : nullptr;
}

bool RetainedSurface::render() {
    if (!good()) return false;
    const uint64_t previous = revision_;
    lv_refr_now(display_.get());
    return revision_ != previous;
}

std::span<const uint16_t> RetainedSurface::pixels() const {
    return good() ? pixels_ : std::span<const uint16_t>{};
}

void RetainedSurface::flush(lv_display_t *display, const lv_area_t *, uint8_t *) {
    auto *surface = static_cast<RetainedSurface *>(lv_display_get_user_data(display));
    if (lv_display_flush_is_last(display)) ++surface->revision_;
    lv_display_flush_ready(display);
}

}
