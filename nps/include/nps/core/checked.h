#ifndef NPS_CHECKED_H
#define NPS_CHECKED_H

#include <cstdint>

namespace nps {

inline bool add_overflows(int64_t a, int64_t b) {
    int64_t ignored = 0;
    return __builtin_add_overflow(a, b, &ignored);
}

inline bool mul_overflows(int64_t a, int64_t b) {
    int64_t ignored = 0;
    return __builtin_mul_overflow(a, b, &ignored);
}

inline bool negate_overflows(int64_t a) {
    int64_t ignored = 0;
    return __builtin_sub_overflow(int64_t{0}, a, &ignored);
}

inline bool add_checked(int64_t a, int64_t b, int64_t *out) {
    int64_t value = 0;
    if (__builtin_add_overflow(a, b, &value))
        return false;
    *out = value;
    return true;
}

inline bool mul_checked(int64_t a, int64_t b, int64_t *out) {
    int64_t value = 0;
    if (__builtin_mul_overflow(a, b, &value))
        return false;
    *out = value;
    return true;
}

}

#endif
