#ifndef NPS_UI_BITMAP_H
#define NPS_UI_BITMAP_H

#include <cstddef>
#include <cstdint>
#include <span>
#include "nps/ui/canvas.h"

namespace nps::ui {
size_t ti_image_size(int width, int height);
bool encode_ti_image(int width, int height, std::span<const uint16_t> rgb565, std::span<char> encoded);
bool icon_ti_image(Icon icon, std::span<char> encoded);
}

#endif
