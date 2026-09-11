#include "nps/ui/retained_menu.h"
#include <lvgl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace {
int failures = 0;
void check(bool passed, const char *message) {
    if (!passed) { ++failures; std::cout << "FAIL: " << message << '\n'; }
}
struct ScopeExit {
    bool &destroyed;
    ~ScopeExit() { destroyed = true; }
};
constexpr std::array<std::string_view, 24> labels = {
    "Fraction", "Power", "Square root", "Cube root", "First derivative", "Indefinite integral",
    "Definite integral", "Equation", "Metres", "Seconds", "Kilograms", "Metres per second",
    "Metres per second squared", "Newtons", "Joules", "Limit at a point", "Limit from the left",
    "Limit from the right", "Limit at positive infinity", "Limit at negative infinity",
    "Full answer", "Domain conditions", "Verification", "Return to mathematical entry"};
constexpr auto descriptions = [] {
    std::array<std::string_view, labels.size()> text{};
    text.fill("Fill the expression and variable, then change the bounds. Scroll to read every instruction.");
    return text;
}();
}

int main(int argc, char **argv) {
    using nps::ui::RetainedMenu;
    alignas(RetainedMenu::pixel_alignment) std::array<uint16_t, 320 * 240> pixels{};
    if (argc == 2 && (std::string_view(argv[1]) == "snapshots" || std::string_view(argv[1]) == "snapshots-guidance")) {
        const auto details = std::string_view(argv[1]) == "snapshots-guidance"
            ? std::span<const std::string_view>(descriptions) : std::span<const std::string_view>{};
        for (int width : {96, 180, 320}) {
            for (int height : {96, 120, 212, 240}) {
                RetainedMenu menu(width, height, pixels, "Templates", labels, details);
                if (menu.status() != NPS_RETAINED_READY) return 1;
                for (size_t selected = 0; selected < labels.size(); ++selected) {
                    if (!menu.select(selected)) return 1;
                    menu.render();
                    if (menu.pixels().empty()) return 1;
                    std::cout.write(reinterpret_cast<const char *>(menu.pixels().data()),
                                    static_cast<std::streamsize>(menu.pixels().size_bytes()));
                }
            }
        }
        lv_deinit();
        return std::cout.good() ? 0 : 1;
    }
    if (argc == 2 && (std::string_view(argv[1]) == "profile" || std::string_view(argv[1]) == "profile-guidance")) {
        const auto details = std::string_view(argv[1]) == "profile-guidance"
            ? std::span<const std::string_view>(descriptions) : std::span<const std::string_view>{};
        {
            RetainedMenu warmup(320, 212, pixels, "Templates", labels, details);
            if (!warmup.render()) return 1;
        }
        std::array<double, 9> elapsed{};
        size_t attempts = 0;
        size_t pool_used = 0;
        for (double &sample : elapsed) {
            for (int cycle = 0; cycle < 50; ++cycle) {
                nps_retained_test_fail_after(SIZE_MAX);
                const auto start = std::chrono::steady_clock::now();
                RetainedMenu menu(320, 212, pixels, "Templates", labels, details);
                sample += std::chrono::duration<double, std::micro>(
                    std::chrono::steady_clock::now() - start).count();
                attempts = nps_retained_test_attempts();
                if (!menu.render()) return 1;
                pool_used = menu.pool_used();
            }
            sample /= 50;
        }
        std::sort(elapsed.begin(), elapsed.end());
        std::cout << "24 entries, create allocations=" << attempts << ", rendered pool bytes=" << pool_used
                  << ", us/create min=" << elapsed.front() << " median=" << elapsed[4]
                  << " max=" << elapsed.back() << '\n';
        lv_deinit();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "assert") {
        bool destroyed = false;
        {
            ScopeExit guard{destroyed};
            nps_retained_test_assert_next();
            RetainedMenu menu(180, 212, pixels, "Templates", labels);
            check(menu.status() == NPS_RETAINED_FAILED && menu.pixels().empty(),
                  "an invariant assertion is a renderer failure, not an allocation diagnosis");
        }
        check(destroyed, "C++ cleanup also runs after an invariant assertion");
        return failures ? 1 : 0;
    }
    if (argc == 3) {
        const std::string_view phase = argv[1];
        const bool counting = std::string_view(argv[2]) == "count";
        const size_t budget = counting ? SIZE_MAX : std::stoull(argv[2]);
        bool destroyed = false;
        size_t phase_attempts = 0;
        {
            ScopeExit guard{destroyed};
            std::optional<RetainedMenu> menu;
            if (phase != "create") {
                menu.emplace(180, 212, pixels, "Calculus templates", labels, descriptions);
                check(menu->status() == NPS_RETAINED_READY, "fault setup creates a healthy menu");
                if (phase != "render") check(menu->render(), "fault setup renders its initial frame");
            }
            nps_retained_test_fail_after(budget);
            uint64_t published = menu ? menu->revision() : 0;
            if (phase == "create") menu.emplace(180, 212, pixels, "Calculus templates", labels, descriptions);
            else if (phase == "render") menu->render();
            else if (phase == "select") { menu->select(labels.size() - 1); menu->render(); }
            else if (phase == "scroll") { menu->scroll(-64); menu->render(); }
            else if (phase == "destroy") menu.reset();
            else { check(false, "known fault phase"); return 1; }
            phase_attempts = nps_retained_test_attempts();
            if (!counting) {
                check(nps_retained_failure() == NPS_RETAINED_RESOURCE_EXHAUSTED,
                      "allocation failure returns a resource outcome through the C boundary");
                if (menu) {
                    const size_t attempts = nps_retained_test_attempts();
                    check(menu->status() == NPS_RETAINED_RESOURCE_EXHAUSTED && menu->pixels().empty(),
                          "a failed renderer cannot expose a partially rendered image");
                    check(!menu->render() && !menu->select(0) && !menu->scroll(1) && menu->revision() == published,
                          "a failed renderer cannot publish a revision or mutate again");
                    check(nps_retained_test_attempts() == attempts, "failed-instance operations never allocate again");
                }
            }
        }
        check(destroyed, "C++ scope cleanup runs after the C failure boundary returns");
        if (counting) std::cout << phase_attempts << '\n';
        else {
            const size_t attempts = nps_retained_test_attempts();
            RetainedMenu unavailable(180, 212, pixels, "Templates", labels);
            check(unavailable.status() == NPS_RETAINED_RESOURCE_EXHAUSTED && unavailable.pixels().empty() &&
                      nps_retained_test_attempts() == attempts,
                  "later creation refuses without entering the poisoned allocator");
        }
        return failures ? 1 : 0;
    }
    {
        RetainedMenu invalid(320, 212, pixels, "Templates", labels,
                             std::span<const std::string_view>(descriptions).first(1));
        check(invalid.status() == NPS_RETAINED_INVALID_INPUT, "description count must match menu entries");
    }
    for (int width : {96, 180, 320}) {
        for (int height : {96, 120, 212, 240}) {
            RetainedMenu menu(width, height, pixels, "Templates", labels, descriptions);
            check(menu.status() == NPS_RETAINED_READY && menu.render(), "described rows fit every supported viewport");
            for (size_t index = 0; index < labels.size(); ++index) {
                check(menu.select(index), "described rows accept keyboard selection");
                menu.render();
                lv_obj_t *button = lv_group_get_focused(lv_group_by_index(0));
                lv_obj_t *title = lv_obj_get_child(button, 0);
                lv_obj_t *description = lv_obj_get_child(button, 1);
                check(lv_obj_get_child_count(button) == 2 &&
                      std::string_view(lv_label_get_text(description)) == descriptions[index],
                      "each row owns its complete description");
                lv_area_t bounds, title_bounds, description_bounds;
                lv_obj_get_content_coords(button, &bounds);
                lv_obj_get_coords(title, &title_bounds);
                lv_obj_get_coords(description, &description_bounds);
                check(title_bounds.y2 < description_bounds.y1 && description_bounds.y2 <= bounds.y2 &&
                      description_bounds.x1 >= bounds.x1 && description_bounds.x2 <= bounds.x2,
                      "flex layout wraps descriptions below their title inside the row");
                lv_obj_t *body = lv_obj_get_parent(button);
                const int32_t distance = lv_obj_get_scroll_bottom(body);
                for (int32_t scroll = 0; scroll <= distance / 240; ++scroll) {
                    const int32_t previous = lv_obj_get_scroll_y(body);
                    check(menu.scroll(-240), "described rows accept scrolling");
                    const int32_t advanced = lv_obj_get_scroll_y(body) - previous;
                    check(advanced >= 0 && advanced <= 240, "description scrolling advances without focus jumps");
                }
                menu.render();
                check(lv_obj_get_scroll_bottom(body) == 0, "the last described row remains reachable");
            }
            const auto revision = menu.revision();
            check(!menu.render() && menu.revision() == revision, "description layout is retained on unchanged paint");
        }
    }
    {
        pixels.fill(0xbeef);
        RetainedMenu invalid(95, 212, pixels, "Templates", labels);
        check(invalid.status() == NPS_RETAINED_INVALID_INPUT &&
                  std::all_of(pixels.begin(), pixels.end(), [](uint16_t pixel) { return pixel == 0xbeef; }),
              "invalid viewports refuse without touching the framebuffer");
    }
    {
        alignas(4) std::array<uint16_t, 96 * 96 + 1> storage{};
        storage.fill(0xbeef);
        RetainedMenu unaligned(96, 96, std::span(storage).subspan(1), "Templates", labels);
        check(unaligned.status() == NPS_RETAINED_INVALID_INPUT && nps_retained_failure() == NPS_RETAINED_READY &&
                  std::all_of(storage.begin(), storage.end(), [](uint16_t pixel) { return pixel == 0xbeef; }),
              "misaligned framebuffer refuses without poisoning LVGL or changing pixels");
    }
    for (int width : {96, 180, 320}) {
        RetainedMenu menu(width, 212, pixels, "Templates", labels);
        check(menu.status() == NPS_RETAINED_READY && menu.pixels().empty(), "new menu withholds unrendered pixels");
        check(menu.render() && menu.revision() == 1 && menu.pixels().size() == static_cast<size_t>(width * 212),
              "first successful frame publishes pixels and revision together");
        const uint64_t first_revision = menu.revision();
        for (int i = 0; i < 100; ++i) check(!menu.render(), "idle frames reuse the retained image");
        check(menu.revision() == first_revision, "idle frames do not publish another revision");
        std::array<int64_t, 9> idle_times{};
        bool idle_unchanged = true;
        for (auto &elapsed : idle_times) {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 5000; ++i) idle_unchanged = !menu.render() && idle_unchanged;
            elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
        }
        std::sort(idle_times.begin(), idle_times.end());
        check(idle_unchanged, "idle benchmark never publishes a frame");
        std::cout << width << "px, median of nine batches of 5000 idle renders: " << idle_times[4] << " us\n";
        check(!menu.select(labels.size()) && menu.status() == NPS_RETAINED_READY && menu.selected() == 0,
              "out-of-range selection leaves the healthy menu unchanged");
        for (size_t i = 0; i < labels.size(); ++i) {
            check(menu.select(i) && menu.selected() == i, "every entry accepts keyboard focus");
            menu.render();
        }
        check(menu.scroll(std::numeric_limits<int>::min()) && menu.scroll(std::numeric_limits<int>::max()),
              "extreme scroll deltas are bounded before reaching LVGL");
    }
    for (int width : {96, 180, 320}) {
        size_t hidden = 0;
        size_t mismatched = 0;
        size_t overscrolled = 0;
        for (int height : {96, 120, 212, 240}) {
            RetainedMenu menu(width, height, pixels, "Templates", labels);
            check(menu.status() == NPS_RETAINED_READY && menu.render(), "scroll visibility fixture opens");
            for (size_t start : {size_t{0}, labels.size() / 2, labels.size() - 1}) {
                menu.select(start);
                for (int delta : {-48, -48, -240, 48, 48, 240, 240, -240}) {
                    check(menu.scroll(delta), "scroll visibility fixture accepts navigation");
                    menu.render();
                    lv_obj_t *focused = lv_group_get_focused(lv_group_by_index(0));
                    lv_obj_t *body = lv_obj_get_parent(focused);
                    lv_area_t viewport, selected;
                    lv_obj_get_content_coords(body, &viewport);
                    lv_obj_get_coords(focused, &selected);
                    if (selected.y2 < viewport.y1 || selected.y1 > viewport.y2) ++hidden;
                    if (lv_obj_get_scroll_top(body) < 0 || lv_obj_get_scroll_bottom(body) < 0) ++overscrolled;
                    if (lv_obj_get_child(body, static_cast<int32_t>(menu.selected())) != focused) ++mismatched;
                }
            }
        }
        check(hidden == 0, "scrolling keeps the selected entry inside the visible menu body");
        check(mismatched == 0, "published selection identifies the visibly focused entry");
        check(overscrolled == 0, "scrolling stays within the retained content extent");
        std::cout << width << "px scroll cases with hidden selection: " << hidden << '\n';
    }
    {
        const std::string long_label(256, 'W');
        const std::array<std::string_view, 1> oversized = {long_label};
        RetainedMenu menu(96, 96, pixels, "Templates", oversized);
        check(menu.status() == NPS_RETAINED_READY && menu.render(), "oversized scrolling fixture opens");
        lv_obj_t *body = lv_obj_get_parent(lv_group_get_focused(lv_group_by_index(0)));
        int32_t previous = lv_obj_get_scroll_y(body);
        for (int i = 0; i < 20; ++i) {
            check(menu.scroll(-240) && menu.selected() == 0, "long label keeps focus while scrolling");
            menu.render();
            const int32_t current = lv_obj_get_scroll_y(body);
            check(current >= previous && current - previous <= 240, "long label scrolls forward without focus jumps");
            previous = current;
        }
        check(lv_obj_get_scroll_bottom(body) == 0, "the end of an oversized label is reachable");
        for (int i = 0; i < 20; ++i) menu.scroll(240);
        menu.render();
        check(lv_obj_get_scroll_y(body) == 0, "the start of an oversized label remains reachable");
        const uint64_t revision = menu.revision();
        check(menu.scroll(0) && !menu.render() && menu.revision() == revision,
              "zero scrolling preserves focus and reuses the retained image");
    }
    {
        const std::string longest(256, 'W');
        const std::array<std::string_view, 1> text = {longest};
        RetainedMenu menu(96, 96, pixels, "Templates", text, text);
        check(menu.status() == NPS_RETAINED_READY && menu.render(), "maximum title and description fit bounded storage");
        lv_obj_t *button = lv_group_get_focused(lv_group_by_index(0));
        lv_obj_t *body = lv_obj_get_parent(button);
        for (int i = 0; i < 20; ++i) check(menu.scroll(-240), "maximum row accepts forward scrolling");
        menu.render();
        check(lv_obj_get_scroll_bottom(body) == 0 && menu.selected() == 0,
              "the complete maximum description remains reachable without losing selection");
        for (int i = 0; i < 20; ++i) menu.scroll(240);
        menu.render();
        check(lv_obj_get_scroll_y(body) == 0, "the maximum row title remains reachable again");
    }
    check(lv_display_get_next(nullptr) == nullptr && lv_group_get_count() == 0 && lv_mem_test() == LV_RESULT_OK,
          "healthy scope exit releases displays and groups with an intact pool");
    lv_mem_monitor_t before_cycles{};
    lv_mem_monitor(&before_cycles);
    for (int cycle = 0; cycle < 32; ++cycle) {
        {
            RetainedMenu menu(cycle % 2 ? 96 : 320, 212, pixels, "Templates", labels, descriptions);
            check(menu.render() && menu.select(labels.size() - 1), "recreated menu renders and restores focus");
            menu.render();
            alignas(RetainedMenu::pixel_alignment) std::array<uint16_t, 96 * 96> second_pixels{};
            RetainedMenu second(96, 96, second_pixels, "Templates", labels);
            check(second.status() == NPS_RETAINED_UNAVAILABLE && menu.status() == NPS_RETAINED_READY,
                  "concurrent scene admission preserves the active menu");
        }
        lv_mem_monitor_t after_cycle{};
        lv_mem_monitor(&after_cycle);
        check(before_cycles.free_size == after_cycle.free_size && lv_mem_test() == LV_RESULT_OK,
              "repeated menu replacement restores the private pool");
    }
    lv_deinit();
    return failures ? 1 : 0;
}
