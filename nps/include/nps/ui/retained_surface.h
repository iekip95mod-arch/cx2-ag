#ifndef NPS_UI_RETAINED_SURFACE_H
#define NPS_UI_RETAINED_SURFACE_H

#include "nps/ui/retained_menu_private.h"
#include <cstdint>
#include <memory>
#include <span>
#include <lvgl.h>

namespace nps::ui {

class RetainedSurface {
  public:
    // Pixel storage and the initialized LVGL runtime must outlive the surface.
    RetainedSurface(int width, int height, std::span<uint16_t> pixels);
    ~RetainedSurface();
    RetainedSurface(const RetainedSurface &) = delete;
    RetainedSurface &operator=(const RetainedSurface &) = delete;
    bool good() const { return display_ && nps_retained_failure() == NPS_RETAINED_READY; }
    lv_obj_t *root() const;
    bool render();
    std::span<const uint16_t> pixels() const;
    uint64_t revision() const { return revision_; }
    int width() const { return width_; }
    int height() const { return height_; }

  private:
    struct DisplayDeleter {
        void operator()(lv_display_t *display) const noexcept;
    };
    static void flush(lv_display_t *display, const lv_area_t *, uint8_t *);
    int width_ = 0, height_ = 0;
    uint64_t revision_ = 0;
    std::span<uint16_t> pixels_;
    std::unique_ptr<lv_display_t, DisplayDeleter> display_;
};

}
#endif
