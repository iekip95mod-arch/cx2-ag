#include "retained_fixture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible_v<nps::ui::RetainedSurface>);
static_assert(!std::is_move_constructible_v<nps::ui::RetainedSurface>);
static_assert(std::is_nothrow_destructible_v<nps::ui::RetainedSurface>);
static_assert(!std::is_copy_constructible_v<RetainedFixture>);
static_assert(!std::is_move_constructible_v<RetainedFixture>);

namespace {
int failed = 0;
bool count_allocations = false;
size_t allocations = 0;
void check(bool passed, const char *message) {
    if (!passed) {
        ++failed;
        std::cout << "FAIL: " << message << '\n';
    }
}

void deleted_button(lv_event_t *event) {
    auto *deletions = static_cast<size_t *>(lv_event_get_user_data(event));
    ++*deletions;
    check(lv_group_get_count() == 0 && lv_obj_get_group(lv_event_get_target_obj(event)) == nullptr,
          "focus ownership is released before its borrowed controls are destroyed");
}
}

void *operator new(size_t size) {
    if (void *memory = std::malloc(size ? size : 1)) {
        if (count_allocations) ++allocations;
        return memory;
    }
    throw std::bad_alloc();
}
void *operator new[](size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, size_t) noexcept { std::free(memory); }

int main(int argc, char **argv) {
    nps::ui::RetainedSurface invalid(321, 240, {});
    check(!invalid.good() && invalid.pixels().empty() && !invalid.render(), "invalid dimensions refuse without a display");
    std::array<uint16_t, 32 * 32 - 1> short_buffer{};
    short_buffer.fill(0xbeef);
    nps::ui::RetainedSurface too_small(32, 32, short_buffer);
    check(!too_small.good() && too_small.pixels().empty() &&
              std::all_of(short_buffer.begin(), short_buffer.end(), [](uint16_t pixel) { return pixel == 0xbeef; }),
          "an undersized framebuffer refuses before any write or display allocation");
    for (int width : {96, 180, 320}) {
        alignas(LV_DRAW_BUF_ALIGN) std::array<uint16_t, 32 * 32 + 1> storage{};
        storage.fill(0xbeef);
        nps::ui::RetainedSurface unaligned(32, 32, std::span(storage).subspan(1));
        check(!unaligned.good() && unaligned.pixels().empty() &&
                  std::all_of(storage.begin(), storage.end(), [](uint16_t pixel) { return pixel == 0xbeef; }),
              "a misaligned framebuffer refuses before writes or LVGL calls");
        allocations = 0;
        count_allocations = true;
        RetainedFixture fixture(width, 212);
        count_allocations = false;
        check(allocations == 0, "the resident fixture has no C++ heap allocation that could outlive module unload");
        auto &surface = fixture.surface;
        check(surface.good(), "supported viewport initializes");
        if (!surface.good()) continue;
        auto *root = fixture.root;
        auto *heading = fixture.heading;
        auto *body = fixture.body;
        auto *footer = fixture.footer;
        auto &buttons = fixture.buttons;
        check(surface.render() && surface.revision() == 1, "initial frame is fully rendered");
        const auto first_pixels = std::vector<uint16_t>(surface.pixels().begin(), surface.pixels().end());
        lv_area_t heading_area{};
        lv_obj_get_coords(heading, &heading_area);
        bool blended_text = false;
        for (int y = heading_area.y1; y <= heading_area.y2; ++y)
            for (int x = heading_area.x1; x <= heading_area.x2; ++x) {
                const uint16_t pixel = first_pixels[static_cast<size_t>(y) * width + x];
                blended_text = blended_text || (pixel != 0 && pixel != 0xffff);
            }
        check(blended_text, "black text on white has antialiased edge coverage");
        bool unchanged = true;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i) unchanged = !surface.render() && unchanged;
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        check(unchanged && std::equal(first_pixels.begin(), first_pixels.end(), surface.pixels().begin()),
              "unchanged paints reuse pixels without flushing");
        lv_obj_update_layout(root);
        lv_area_t footer_area{};
        lv_obj_get_coords(footer, &footer_area);
        check(footer_area.x1 >= 0 && footer_area.x2 < width && footer_area.y2 < 212,
              "footer remains inside the viewport");
        for (size_t i = 0; i < buttons.size(); ++i) {
            lv_group_focus_obj(buttons[i]);
            lv_obj_scroll_to_view(buttons[i], LV_ANIM_OFF);
            surface.render();
            lv_area_t button_area{}, body_area{};
            lv_obj_get_coords(buttons[i], &button_area);
            lv_obj_get_content_coords(body, &body_area);
            check(button_area.x1 >= body_area.x1 && button_area.x2 <= body_area.x2 &&
                      button_area.y1 >= body_area.y1 && button_area.y2 <= body_area.y2,
                  "every focused control is fully reachable through scrolling");
        }
        lv_mem_monitor_t memory{};
        lv_mem_monitor(&memory);
        check(memory.free_biggest_size > 64 * 1024 && lv_mem_test() == LV_RESULT_OK,
              "the bounded scene retains allocation headroom and intact allocator state");
        std::cout << width << "px, idle 1000 renders " << elapsed << " us, pool used "
                  << memory.total_size - memory.free_size << " bytes, largest free "
                  << memory.free_biggest_size << " bytes\n";
        if (argc == 2) {
            std::ofstream capture(std::string(argv[1]) + "-" + std::to_string(width) + ".ppm", std::ios::binary);
            capture << "P6\n" << width << " 212\n255\n";
            for (uint16_t pixel : surface.pixels()) {
                const std::array<char, 3> rgb = {
                    static_cast<char>(((pixel >> 11) & 31) * 255 / 31),
                    static_cast<char>(((pixel >> 5) & 63) * 255 / 63),
                    static_cast<char>((pixel & 31) * 255 / 31)};
                capture.write(rgb.data(), rgb.size());
            }
            check(capture.good(), "the rendered fixture can be captured for inspection");
        }
    }
    lv_mem_monitor_t before_lifecycles{};
    lv_mem_monitor(&before_lifecycles);
    for (int cycle = 0; cycle < 32; ++cycle) {
        size_t deletions = 0;
        {
            RetainedFixture fixture(cycle % 2 ? 96 : 320, 212);
            for (auto *button : fixture.buttons)
                lv_obj_add_event_cb(button, deleted_button, LV_EVENT_DELETE, &deletions);
            fixture.surface.render();
        }
        lv_mem_monitor_t after_lifecycle{};
        lv_mem_monitor(&after_lifecycle);
        check(deletions == RetainedFixture::titles.size(), "each owned control is destroyed exactly once");
        check(lv_display_get_next(nullptr) == nullptr && lv_group_get_count() == 0,
              "scope exit releases the display and focus group");
        check(after_lifecycle.free_size == before_lifecycles.free_size && lv_mem_test() == LV_RESULT_OK,
              "repeated construction and destruction restore the private pool");
    }
    {
        RetainedFixture refused(321, 240);
        check(!refused.surface.good() && !refused.group, "refused construction owns no display or focus group");
    }
    check(lv_display_get_next(nullptr) == nullptr && lv_group_get_count() == 0,
          "destruction of a refused fixture leaves no native resources");
    lv_deinit();
    return failed ? 1 : 0;
}
