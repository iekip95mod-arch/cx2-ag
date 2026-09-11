#ifndef NPS_RETAINED_MENU_PRIVATE_H
#define NPS_RETAINED_MENU_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nps_retained_status {
    NPS_RETAINED_READY,
    NPS_RETAINED_INVALID_INPUT,
    NPS_RETAINED_UNAVAILABLE,
    NPS_RETAINED_RESOURCE_EXHAUSTED,
    NPS_RETAINED_FAILED
};

struct nps_retained_text {
    const char *text;
    size_t length;
};
enum { NPS_RETAINED_MAX_ENTRIES = 24, NPS_RETAINED_MAX_LABEL_BYTES = 256, NPS_RETAINED_PIXEL_ALIGNMENT = 4 };
struct nps_retained_menu;

enum nps_retained_status nps_retained_failure(void);
void nps_retained_assert(void);
enum nps_retained_status nps_retained_menu_create(int width, int height, uint16_t *pixels, size_t pixel_count,
    struct nps_retained_text title, const struct nps_retained_text *labels,
    const struct nps_retained_text *descriptions, size_t count,
    struct nps_retained_menu **menu);
enum nps_retained_status nps_retained_menu_render(struct nps_retained_menu *menu, uint64_t *revision, size_t *pool_used);
enum nps_retained_status nps_retained_menu_select(struct nps_retained_menu *menu, size_t index);
enum nps_retained_status nps_retained_menu_scroll(struct nps_retained_menu *menu, int pixels, size_t *selected);
void nps_retained_menu_destroy(struct nps_retained_menu *menu);

#ifdef NPS_UI_TESTING
void nps_retained_test_fail_after(size_t attempts);
size_t nps_retained_test_attempts(void);
void nps_retained_test_assert_next(void);
#endif

#ifdef __cplusplus
}
#endif
#endif
