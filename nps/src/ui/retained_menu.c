#include "nps/ui/retained_menu_private.h"
#include <lvgl.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#if LV_USE_OS != LV_OS_NONE || LV_USE_STDLIB_MALLOC != LV_STDLIB_BUILTIN || LV_MEM_POOL_EXPAND_SIZE != 0 || LV_MEM_ADR != 0 || defined(LV_MEM_POOL_ALLOC)
#error Retained menu containment requires synchronous LVGL and its fixed builtin pool
#endif

_Static_assert(NPS_RETAINED_PIXEL_ALIGNMENT == LV_DRAW_BUF_ALIGN, "Retained pixel alignment must match LVGL");

// Only C frames and library-owned callbacks may run inside this boundary.
static jmp_buf failure_boundary;
static int boundary_active;
static enum nps_retained_status failure_status = NPS_RETAINED_READY;

struct nps_retained_menu {
    lv_display_t *display;
    lv_group_t *group;
    lv_style_t button_style;
    lv_style_t focused_style;
    lv_obj_t *body;
    lv_obj_t *buttons[NPS_RETAINED_MAX_ENTRIES];
    size_t count;
    size_t selected;
    uint64_t revision;
};

void *nps_lv_builtin_malloc_core(size_t size);
void *nps_lv_builtin_realloc_core(void *pointer, size_t size);

#ifdef NPS_UI_TESTING
static size_t allocation_attempts;
static size_t fail_after = SIZE_MAX;
static int assert_next;
void nps_retained_test_fail_after(size_t attempts) {
    allocation_attempts = 0;
    fail_after = attempts;
}
size_t nps_retained_test_attempts(void) { return allocation_attempts; }
void nps_retained_test_assert_next(void) { assert_next = 1; }
#endif

static void fail(enum nps_retained_status reason) {
    failure_status = reason;
    if (boundary_active) longjmp(failure_boundary, 1);
    abort();
}

static int allocation_attempt(void) {
#ifdef NPS_UI_TESTING
    if (assert_next) {
        assert_next = 0;
        nps_retained_assert();
    }
    if (allocation_attempts++ == fail_after) return 0;
#endif
    return 1;
}

void *lv_malloc_core(size_t size) {
    void *allocation = allocation_attempt() ? nps_lv_builtin_malloc_core(size) : NULL;
    if (!allocation) fail(NPS_RETAINED_RESOURCE_EXHAUSTED);
    return allocation;
}

void *lv_realloc_core(void *pointer, size_t size) {
    void *allocation = allocation_attempt() ? nps_lv_builtin_realloc_core(pointer, size) : NULL;
    if (!allocation) fail(NPS_RETAINED_RESOURCE_EXHAUSTED);
    return allocation;
}

enum nps_retained_status nps_retained_failure(void) { return failure_status; }
void nps_retained_assert(void) { fail(NPS_RETAINED_FAILED); }

#define BEGIN_BOUNDARY() \
    if (failure_status != NPS_RETAINED_READY) return failure_status; \
    if (boundary_active) return NPS_RETAINED_UNAVAILABLE; \
    if (setjmp(failure_boundary)) { boundary_active = 0; return failure_status; } \
    boundary_active = 1

static int valid_text(struct nps_retained_text text) {
    if (!text.text || text.length == 0 || text.length > NPS_RETAINED_MAX_LABEL_BYTES) return 0;
    for (size_t i = 0; i < text.length; ++i)
        if (text.text[i] < 32 || text.text[i] > 126) return 0;
    return 1;
}

static void label_text(lv_obj_t *label, struct nps_retained_text text) {
    char terminated[NPS_RETAINED_MAX_LABEL_BYTES + 1];
    memcpy(terminated, text.text, text.length);
    terminated[text.length] = 0;
    lv_label_set_text(label, terminated);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    (void)area;
    (void)pixels;
    struct nps_retained_menu *menu = lv_display_get_user_data(display);
    if (lv_display_flush_is_last(display)) ++menu->revision;
    lv_display_flush_ready(display);
}

static void destroy(struct nps_retained_menu *menu) {
    lv_group_delete(menu->group);
    lv_display_delete(menu->display);
    lv_style_reset(&menu->button_style);
    lv_style_reset(&menu->focused_style);
    lv_free(menu);
}

enum nps_retained_status nps_retained_menu_create(int width, int height, uint16_t *pixels, size_t pixel_count,
    struct nps_retained_text title, const struct nps_retained_text *labels,
    const struct nps_retained_text *descriptions, size_t count,
    struct nps_retained_menu **created) {
    if (!created) return NPS_RETAINED_INVALID_INPUT;
    *created = NULL;
    if (width < 96 || width > 320 || height < 96 || height > 240 || !pixels ||
        (uintptr_t)pixels % LV_DRAW_BUF_ALIGN != 0 ||
        pixel_count < (size_t)width * (size_t)height || !valid_text(title) || !labels || count == 0 || count > NPS_RETAINED_MAX_ENTRIES)
        return NPS_RETAINED_INVALID_INPUT;
    for (size_t i = 0; i < count; ++i)
        if (!valid_text(labels[i]) || (descriptions && descriptions[i].length && !valid_text(descriptions[i])))
            return NPS_RETAINED_INVALID_INPUT;
    BEGIN_BOUNDARY();
    if (!lv_is_initialized()) lv_init();
    if (lv_display_get_next(NULL) || lv_group_get_count()) {
        boundary_active = 0;
        return NPS_RETAINED_UNAVAILABLE;
    }
    struct nps_retained_menu *menu = lv_malloc_zeroed(sizeof(*menu));
    lv_style_init(&menu->button_style);
    lv_style_init(&menu->focused_style);
    lv_style_set_pad_all(&menu->button_style, 6);
    lv_style_set_bg_color(&menu->button_style, lv_color_hex(0xffffff));
    lv_style_set_text_color(&menu->button_style, lv_color_hex(0x253957));
    lv_style_set_border_width(&menu->button_style, 1);
    lv_style_set_border_color(&menu->button_style, lv_color_hex(0xffffff));
    lv_style_set_bg_color(&menu->focused_style, lv_color_hex(0xe8effa));
    lv_style_set_border_color(&menu->focused_style, lv_color_hex(0x375c94));
    menu->display = lv_display_create(width, height);
    lv_display_enable_invalidation(menu->display, false);
    menu->group = lv_group_create();
    menu->count = count;
    lv_display_set_user_data(menu->display, menu);
    lv_display_set_color_format(menu->display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(menu->display, pixels, NULL, width * height * sizeof(*pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(menu->display, flush);
    lv_obj_t *root = lv_display_get_screen_active(menu->display);
    lv_obj_set_style_bg_color(root, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_all(root, 6, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *heading = lv_label_create(root);
    lv_obj_set_style_text_color(heading, lv_color_hex(0x000000), 0);
    lv_obj_set_width(heading, LV_PCT(100));
    label_text(heading, title);
    menu->body = lv_obj_create(root);
    lv_obj_set_width(menu->body, LV_PCT(100));
    lv_obj_set_flex_grow(menu->body, 1);
    lv_obj_set_flex_flow(menu->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(menu->body, LV_DIR_VER);
    lv_obj_set_style_pad_all(menu->body, 4, 0);
    lv_obj_set_style_bg_color(menu->body, lv_color_hex(0xffffff), 0);
    lv_obj_t *footer = lv_label_create(root);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_label_set_text_static(footer, "Enter select / Esc back");
    for (size_t i = 0; i < count; ++i) {
        lv_obj_t *button = lv_button_create(menu->body);
        menu->buttons[i] = button;
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_set_height(button, LV_SIZE_CONTENT);
        lv_obj_add_style(button, &menu->button_style, 0);
        lv_obj_add_style(button, &menu->focused_style, LV_STATE_FOCUSED);
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        label_text(label, labels[i]);
        if (descriptions && descriptions[i].length) {
            lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(button, 3, 0);
            lv_obj_t *description = lv_label_create(button);
            lv_obj_set_width(description, LV_PCT(100));
            lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(description, &lv_font_montserrat_12, 0);
            label_text(description, descriptions[i]);
        }
        lv_group_add_obj(menu->group, button);
    }
    lv_obj_update_layout(root);
    lv_area_t footer_area;
    lv_obj_get_coords(footer, &footer_area);
    if (lv_obj_get_content_height(menu->body) < 24 || footer_area.y2 >= height) {
        destroy(menu);
        boundary_active = 0;
        return NPS_RETAINED_INVALID_INPUT;
    }
    lv_display_enable_invalidation(menu->display, true);
    lv_obj_invalidate(root);
    *created = menu;
    boundary_active = 0;
    return NPS_RETAINED_READY;
}

enum nps_retained_status nps_retained_menu_render(struct nps_retained_menu *menu, uint64_t *revision, size_t *pool_used) {
    BEGIN_BOUNDARY();
    lv_refr_now(menu->display);
    if (menu->revision != *revision) {
        lv_mem_monitor_t memory;
        lv_mem_monitor(&memory);
        *pool_used = memory.total_size - memory.free_size;
    }
    *revision = menu->revision;
    boundary_active = 0;
    return NPS_RETAINED_READY;
}

enum nps_retained_status nps_retained_menu_select(struct nps_retained_menu *menu, size_t index) {
    BEGIN_BOUNDARY();
    if (index >= menu->count) {
        boundary_active = 0;
        return NPS_RETAINED_INVALID_INPUT;
    }
    lv_group_focus_obj(menu->buttons[index]);
    lv_obj_scroll_to_view(menu->buttons[index], LV_ANIM_OFF);
    menu->selected = index;
    boundary_active = 0;
    return NPS_RETAINED_READY;
}

enum nps_retained_status nps_retained_menu_scroll(struct nps_retained_menu *menu, int pixels, size_t *selected) {
    if (!selected) return NPS_RETAINED_INVALID_INPUT;
    BEGIN_BOUNDARY();
    if (pixels < -240) pixels = -240;
    if (pixels > 240) pixels = 240;
    lv_obj_scroll_by_bounded(menu->body, 0, pixels, LV_ANIM_OFF);
    lv_obj_update_layout(menu->body);
    lv_area_t viewport, focused;
    lv_obj_get_content_coords(menu->body, &viewport);
    lv_obj_get_coords(menu->buttons[menu->selected], &focused);
    const int fits = focused.y1 >= viewport.y1 && focused.y2 <= viewport.y2;
    const int fills = focused.y1 <= viewport.y1 && focused.y2 >= viewport.y2;
    if (!fits && !fills) {
        size_t next = menu->selected;
        int32_t best_overlap = 0;
        for (size_t offset = 0; offset < menu->count; ++offset) {
            const size_t index = pixels > 0 ? menu->count - 1 - offset : offset;
            lv_area_t entry;
            lv_obj_get_coords(menu->buttons[index], &entry);
            const int32_t top = LV_MAX(entry.y1, viewport.y1);
            const int32_t bottom = LV_MIN(entry.y2, viewport.y2);
            const int32_t overlap = bottom - top + 1;
            if (entry.y1 >= viewport.y1 && entry.y2 <= viewport.y2) {
                next = index;
                break;
            }
            if (overlap > best_overlap) {
                next = index;
                best_overlap = overlap;
            }
        }
        if (next != menu->selected) {
            lv_obj_t *button = menu->buttons[next];
            lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_group_focus_obj(button);
            lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            menu->selected = next;
        }
    }
    *selected = menu->selected;
    boundary_active = 0;
    return NPS_RETAINED_READY;
}

void nps_retained_menu_destroy(struct nps_retained_menu *menu) {
    if (failure_status != NPS_RETAINED_READY || !menu) return;
    if (boundary_active) fail(NPS_RETAINED_FAILED);
    if (setjmp(failure_boundary)) { boundary_active = 0; return; }
    boundary_active = 1;
    destroy(menu);
    boundary_active = 0;
}
