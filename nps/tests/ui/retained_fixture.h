#ifndef NPS_TESTS_RETAINED_FIXTURE_H
#define NPS_TESTS_RETAINED_FIXTURE_H

#include "nps/ui/retained_surface.h"
#include <array>

struct RetainedFixture {
    struct GroupDeleter {
        void operator()(lv_group_t *group) const noexcept { lv_group_delete(group); }
    };
    inline static constexpr std::array<const char *, 6> titles = {"First derivative", "Definite integral", "Finite limit",
        "Right-sided limit", "Limit at infinity", "Full answer and domain conditions"};
    alignas(LV_DRAW_BUF_ALIGN) std::array<uint16_t, 320 * 240> pixels{};
    nps::ui::RetainedSurface surface;
    lv_obj_t *root = nullptr, *heading = nullptr, *body = nullptr, *footer = nullptr;
    // Release focus ownership before destroying its borrowed controls.
    std::unique_ptr<lv_group_t, GroupDeleter> group;
    std::array<lv_obj_t *, titles.size()> buttons{};
    RetainedFixture(int width, int height) : surface(width, height, pixels) {
        if (!surface.good()) return;
        root = surface.root();
        lv_obj_set_style_bg_color(root, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_pad_all(root, 6, 0);
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        heading = lv_label_create(root);
        lv_obj_set_style_text_color(heading, lv_color_hex(0x000000), 0);
        lv_obj_set_width(heading, LV_PCT(100));
        lv_label_set_text_static(heading, "Calculus templates");
        body = lv_obj_create(root);
        lv_obj_set_width(body, LV_PCT(100));
        lv_obj_set_flex_grow(body, 1);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(body, 4, 0);
        lv_obj_set_style_bg_color(body, lv_color_hex(0xffffff), 0);
        footer = lv_label_create(root);
        lv_obj_set_width(footer, LV_PCT(100));
        lv_label_set_text_static(footer, "Enter select / Esc back");
        group.reset(lv_group_create());
        for (size_t i = 0; i < titles.size(); ++i) {
            buttons[i] = lv_button_create(body);
            lv_obj_set_width(buttons[i], LV_PCT(100));
            lv_obj_set_height(buttons[i], LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(buttons[i], 6, 0);
            lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0xffffff), 0);
            lv_obj_set_style_bg_color(buttons[i], lv_color_hex(0xe8effa), LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(buttons[i], lv_color_hex(0x253957), 0);
            lv_obj_set_style_border_width(buttons[i], 1, 0);
            lv_obj_set_style_border_color(buttons[i], lv_color_hex(0xffffff), 0);
            lv_obj_set_style_border_color(buttons[i], lv_color_hex(0x375c94), LV_STATE_FOCUSED);
            lv_obj_t *label = lv_label_create(buttons[i]);
            lv_obj_set_width(label, LV_PCT(100));
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_label_set_text_static(label, titles[i]);
            lv_group_add_obj(group.get(), buttons[i]);
        }
    }
    RetainedFixture(const RetainedFixture &) = delete;
    RetainedFixture &operator=(const RetainedFixture &) = delete;
};
#endif
