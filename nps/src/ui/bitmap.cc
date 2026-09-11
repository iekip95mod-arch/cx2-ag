#include "nps/ui/bitmap.h"

#include <array>

namespace nps::ui {
namespace {
void put16(std::span<char> encoded, size_t offset, uint16_t value) {
    encoded[offset] = static_cast<char>(value & 255);
    encoded[offset + 1] = static_cast<char>(value >> 8);
}
void put32(std::span<char> encoded, size_t offset, uint32_t value) {
    put16(encoded, offset, static_cast<uint16_t>(value));
    put16(encoded, offset + 2, static_cast<uint16_t>(value >> 16));
}
}

size_t ti_image_size(int width, int height) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return 0;
    return 20 + static_cast<size_t>(width) * static_cast<size_t>(height) * 2;
}

bool encode_ti_image(int width, int height, std::span<const uint16_t> rgb565, std::span<char> encoded) {
    const size_t bytes = ti_image_size(width, height);
    if (bytes == 0 || encoded.size() != bytes || rgb565.size() != (bytes - 20) / 2) return false;
    put32(encoded, 0, static_cast<uint32_t>(width));
    put32(encoded, 4, static_cast<uint32_t>(height));
    put32(encoded, 8, 0);
    put32(encoded, 12, static_cast<uint32_t>(width) * 2);
    put16(encoded, 16, 16);
    put16(encoded, 18, 1);
    for (size_t i = 0; i < rgb565.size(); ++i) {
        const uint16_t pixel = rgb565[i];
        put16(encoded, 20 + 2 * i, static_cast<uint16_t>(0x8000u | ((pixel & 0xffc0u) >> 1) | (pixel & 0x1fu)));
    }
    return true;
}

bool icon_ti_image(Icon icon, std::span<char> encoded) {
    if (encoded.size() != ti_image_size(16, 16)) return false;
    Canvas canvas(16, 16);
    if (!canvas.icon(icon, 0, 0, {37, 57, 87}, {255, 255, 255})) return false;
    std::array<uint16_t, 256> pixels{};
    for (const Fill &fill : canvas.fills()) {
        const uint16_t color = static_cast<uint16_t>((fill.color.red >> 3) << 11 |
            (fill.color.green >> 3) << 6 | (fill.color.blue >> 3));
        for (int y = fill.bounds.y; y < fill.bounds.y + fill.bounds.height; ++y)
            for (int x = fill.bounds.x; x < fill.bounds.x + fill.bounds.width; ++x)
                pixels[static_cast<size_t>(y) * 16 + static_cast<size_t>(x)] = color;
    }
    return encode_ti_image(16, 16, pixels, encoded);
}
}
