#include "nps/ui/canvas.h"

#include <algorithm>
#include <EASTL/fixed_vector.h>
#include <etl/array.h>

extern "C" {
void nps_rgb_line(int16_t, int16_t, int16_t, int16_t, uint16_t, void *);
void nps_rgb_circle(int16_t, int16_t, int16_t, uint16_t, void *);
}

namespace {
struct Pixels { etl::array<uint8_t, 64 * 64> colors{}; };
struct Run { uint8_t x, y, width, coverage; };
constexpr uint16_t linear_srgb[] = {0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219, 241, 264, 288, 313, 340, 367, 396, 427, 458, 491, 526, 562, 599, 637, 677, 718, 761, 805, 851, 898, 947, 997, 1048, 1101, 1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863, 1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866, 2961, 3058, 3157, 3258, 3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129, 4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124, 5257, 5392, 5530, 5669, 5810, 5953, 6099, 6246, 6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500, 7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889, 9072, 9258, 9445, 9635, 9828, 10022, 10219, 10417, 10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090, 12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909, 14146, 14387, 14629, 14874, 15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001, 18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481, 21787, 22096, 22407, 22721, 23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325, 25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094, 28452, 28813, 29176, 29542, 29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143, 34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138, 39572, 40009, 40449, 40891, 41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534, 45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359, 48850, 49344, 49841, 50341, 50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567, 57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221, 63795, 64372, 64952, 65535};

uint8_t blend_channel(uint8_t foreground, uint8_t background, unsigned coverage) {
    const unsigned linear = (static_cast<unsigned>(linear_srgb[foreground]) * coverage +
        static_cast<unsigned>(linear_srgb[background]) * (16 - coverage) + 8) / 16;
    const auto high = std::lower_bound(std::begin(linear_srgb), std::end(linear_srgb), linear);
    if (high == std::begin(linear_srgb)) return 0;
    const auto low = high - 1;
    return static_cast<uint8_t>((linear - *low < *high - linear ? low : high) - std::begin(linear_srgb));
}
}

extern "C" void nps_rgb_pixel(int16_t x, int16_t y, uint16_t color, void *buffer) {
    auto &pixels = *static_cast<Pixels *>(buffer);
    for (int dy = -1; dy <= 2; ++dy) {
        for (int dx = -1; dx <= 2; ++dx) {
            if ((dx == -1 || dx == 2) && (dy == -1 || dy == 2)) continue;
            const int column = x + dx, row = y + dy;
            if (column >= 0 && column < 64 && row >= 0 && row < 64)
                pixels.colors[static_cast<size_t>(row) * 64 + static_cast<size_t>(column)] = color != 0;
        }
    }
}

namespace nps::ui {

Canvas::Canvas(int width, int height) : width_(width), height_(height) {
    good_ = width > 0 && width <= 4096 && height > 0 && height <= 4096;
}

bool Canvas::fill(Rect bounds, Color color) {
    if (!good_) return false;
    if (bounds.width <= 0 || bounds.height <= 0) return true;
    const int64_t right = std::min<int64_t>(width_, static_cast<int64_t>(bounds.x) + bounds.width);
    const int64_t bottom = std::min<int64_t>(height_, static_cast<int64_t>(bounds.y) + bounds.height);
    bounds.x = std::max(0, bounds.x);
    bounds.y = std::max(0, bounds.y);
    if (right <= bounds.x || bottom <= bounds.y) return true;
    bounds.width = static_cast<int>(right - bounds.x);
    bounds.height = static_cast<int>(bottom - bounds.y);
    if (fills_.full()) { good_ = false; return false; }
    fills_.push_back({bounds, color});
    return true;
}

namespace {
struct IconMask {
    eastl::fixed_vector<Run, 128, false> runs;
    bool good = true;
};

IconMask rasterize(Icon icon) {
    IconMask mask;
    Pixels pixels;
    const auto line = [&](int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
        nps_rgb_line(x1 * 4 + 1, y1 * 4 + 1, x2 * 4 + 1, y2 * 4 + 1, 1, &pixels);
    };
    switch (icon) {
        case Icon::Check:
            line(3, 8, 6, 11);
            line(6, 11, 12, 4);
            break;
        case Icon::Warning:
            line(7, 1, 1, 13);
            line(1, 13, 14, 13);
            line(14, 13, 7, 1);
            line(7, 5, 7, 8);
            line(7, 11, 7, 11);
            break;
        case Icon::Information:
            nps_rgb_circle(29, 29, 24, 1, &pixels);
            line(7, 4, 7, 4);
            line(7, 7, 7, 10);
            break;
        case Icon::Left:
            line(10, 3, 5, 8);
            line(5, 8, 10, 13);
            break;
        case Icon::Right:
            line(5, 3, 10, 8);
            line(10, 8, 5, 13);
            break;
        default: mask.good = false; return mask;
    }
    etl::array<uint8_t, 16 * 16> coverage{};
    for (unsigned row = 0; row < 64; ++row)
        for (unsigned column = 0; column < 64; ++column)
            coverage[(row / 4) * 16 + column / 4] += pixels.colors[row * 64 + column];
    for (unsigned row = 0; row < 16; ++row) {
        unsigned column = 0;
        while (column < 16) {
            const uint8_t alpha = coverage[row * 16 + column];
            if (!alpha) { ++column; continue; }
            const unsigned first = column;
            while (column < 16 && coverage[row * 16 + column] == alpha) ++column;
            if (mask.runs.size() == mask.runs.capacity()) { mask.good = false; return mask; }
            mask.runs.push_back({static_cast<uint8_t>(first), static_cast<uint8_t>(row),
                            static_cast<uint8_t>(column - first), alpha});
        }
    }
    return mask;
}
}

bool Canvas::icon(Icon icon, int x, int y, Color foreground, Color background) {
    if (!good_) return false;
    if (static_cast<unsigned>(icon) > static_cast<unsigned>(Icon::Right)) {
        good_ = false;
        return false;
    }
    static const IconMask masks[] = {rasterize(Icon::Check), rasterize(Icon::Warning),
        rasterize(Icon::Information), rasterize(Icon::Left), rasterize(Icon::Right)};
    const IconMask &mask = masks[static_cast<unsigned>(icon)];
    if (!mask.good) { good_ = false; return false; }
    etl::array<Color, 17> palette{};
    for (unsigned alpha = 1; alpha <= 16; ++alpha)
        palette[alpha] = {blend_channel(foreground.red, background.red, alpha),
                          blend_channel(foreground.green, background.green, alpha),
                          blend_channel(foreground.blue, background.blue, alpha)};
    if (!fill({x, y, 16, 16}, background)) return false;
    for (const Run &run : mask.runs) {
        const int64_t left = static_cast<int64_t>(x) + run.x;
        const int64_t top = static_cast<int64_t>(y) + run.y;
        if (left < -16 || left >= width_ || top < 0 || top >= height_) continue;
        if (!fill({static_cast<int>(left), static_cast<int>(top), run.width, 1}, palette[run.coverage])) return false;
    }
    return true;
}

bool Canvas::panel(int header_height, int footer_height) {
    if (header_height < 0 || footer_height < 0 || header_height > height_ || footer_height > height_ - header_height) {
        good_ = false;
        return false;
    }
    return fill({0, 0, width_, height_}, {255, 255, 255}) &&
           fill({0, 0, width_, header_height}, {37, 57, 87}) &&
           fill({0, height_ - footer_height, width_, footer_height}, {242, 244, 247}) &&
           (footer_height == 0 || fill({0, height_ - footer_height, width_, 1}, {190, 196, 205}));
}

void Canvas::paint(void *context, void (*fill_rect)(void *, const Fill &)) const {
    if (!good_ || !fill_rect) return;
    for (const Fill &fill : fills_) fill_rect(context, fill);
}

}
