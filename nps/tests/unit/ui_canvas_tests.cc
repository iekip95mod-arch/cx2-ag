#include "nps/ui/canvas.h"
#include "nps/ui/bitmap.h"
#include "unit/adapter_tests.h"

#include <climits>
#include <algorithm>
#include <array>

namespace nps {
void run_ui_canvas_tests(TestSink &t) {
    {
        const std::array<uint16_t, 6> pixels = {0xf800, 0x07e0, 0x001f, 0xffff, 0x0000, 0x0821};
        std::array<char, 32> encoded{};
        t.check(ui::encode_ti_image(3, 2, pixels, encoded), "a bounded RGB565 image encodes into TI bitmap storage");
        const std::array<uint8_t, 32> expected = {3, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0,
            6, 0, 0, 0, 16, 0, 1, 0, 0, 252, 224, 131, 31, 128, 255, 255, 0, 128, 1, 132};
        bool same = true;
        for (size_t i = 0; i < expected.size(); ++i)
            same = same && static_cast<uint8_t>(encoded[i]) == expected[i];
        t.check(same, "TI image fields and opaque primaries match the published little-endian ARGB1555 format");
        const auto intact = encoded;
        t.check(!ui::encode_ti_image(3, 3, pixels, encoded) && encoded == intact &&
                    ui::ti_image_size(INT_MAX, INT_MAX) == 0 && ui::ti_image_size(0, 1) == 0,
                "invalid bitmap dimensions refuse before changing the destination");
        for (unsigned icon = 0; icon < 5; ++icon) {
            std::array<char, 532> image{};
            t.check(ui::icon_ti_image(static_cast<ui::Icon>(icon), image) &&
                        static_cast<uint8_t>(image[0]) == 16 && static_cast<uint8_t>(image[4]) == 16,
                    "every native symbol has a retained TI image representation");
        }
    }
    ui::Canvas canvas(320, 240);
    t.check(canvas.panel(18, 18) && canvas.fills().size() == 4, "native panel batches its background and chrome");
    const ui::Fill &footer = canvas.fills()[2];
    t.check(footer.bounds.y == 222 && footer.bounds.height == 18, "native footer stays within the canvas");
    t.check(canvas.fill({INT_MIN, 0, INT_MAX, 20}, {}) && canvas.fills().size() == 4,
            "offscreen rectangle clipping cannot overflow");
    t.check(canvas.fill({-5, -5, 10, 10}, {}) && canvas.fills().back().bounds.width == 5 &&
                canvas.fills().back().bounds.height == 5, "native rectangles clip at both viewport edges");
    for (ui::Icon icon : {ui::Icon::Check, ui::Icon::Warning, ui::Icon::Information, ui::Icon::Left, ui::Icon::Right}) {
        ui::Canvas clipped(16, 16);
        t.check(clipped.icon(icon, 0, 0, {0, 0, 0}, {255, 255, 255}) && clipped.fills().size() > 5,
                "nRGBlib rasterizes a complete bounded icon");
        bool inside = true;
        bool antialiased = false;
        bool linear_light = true;
        const std::array<uint8_t, 17> srgb_coverage = {0, 71, 99, 120, 137, 152, 165, 177,
            188, 198, 207, 216, 225, 233, 240, 248, 255};
        for (const ui::Fill &fill : clipped.fills())
        {
            inside = inside && fill.bounds.x >= 0 && fill.bounds.y >= 0 &&
                fill.bounds.x + fill.bounds.width <= 16 && fill.bounds.y + fill.bounds.height <= 16;
            antialiased = antialiased || (fill.color.red > 0 && fill.color.red < 255 &&
                fill.color.red == fill.color.green && fill.color.green == fill.color.blue);
            linear_light = linear_light && std::find(srgb_coverage.begin(), srgb_coverage.end(),
                fill.color.red) != srgb_coverage.end();
        }
        t.check(inside, "every raster run stays inside its viewport");
        t.check(antialiased, "every native symbol has grayscale edge coverage without color fringes");
        t.check(linear_light, "symbol edges blend linear-light coverage before encoding sRGB");
    }
    ui::Canvas full(320, 240);
    for (unsigned i = 0; i < 256; ++i) full.fill({0, 0, 1, 1}, {});
    t.check(full.good() && full.fills().size() == 256 && !full.fill({0, 0, 1, 1}, {}),
            "display list capacity refuses before either fixed container can overflow");
    unsigned painted = 0;
    full.paint(&painted, [](void *counter, const ui::Fill &) { ++*static_cast<unsigned *>(counter); });
    t.check(painted == 0, "an incomplete native frame is never painted");
    ui::Canvas invalid(320, 240);
    t.check(!invalid.panel(200, 100), "overlapping header and footer are refused");
}
}
