#pragma once

#include <cstddef>
#include <cstdint>
#include "gen.h"
#include "fraction.h"

#if defined(SMARTPTR64) || defined(NSPIRE_NEWLIB)
namespace nps::giac_layout {
struct EnclosedValue {
    std::uint32_t prefix;
    giac::gen value;
};

static_assert(sizeof(giac::gen) == 8);
static_assert(sizeof(giac::gen) == sizeof(giac::alias_gen));
static_assert(alignof(giac::gen) == alignof(giac::alias_gen));
static_assert(offsetof(EnclosedValue, value) % alignof(giac::gen) == 0);
static_assert(offsetof(giac::ref_complex, re) == offsetof(giac::alias_ref_complex, re));
static_assert(offsetof(giac::ref_complex, im) == offsetof(giac::alias_ref_complex, im));
static_assert(sizeof(giac::ref_complex) == sizeof(giac::alias_ref_complex));
static_assert(offsetof(giac::ref_fraction, f) + offsetof(giac::fraction, num) ==
              offsetof(giac::alias_ref_fraction, num));
static_assert(offsetof(giac::ref_fraction, f) + offsetof(giac::fraction, den) ==
              offsetof(giac::alias_ref_fraction, den));
static_assert(sizeof(giac::ref_fraction) == sizeof(giac::alias_ref_fraction));
static_assert(offsetof(giac::ref_symbolic, s) + offsetof(giac::symbolic, sommet) ==
              offsetof(giac::alias_ref_symbolic, sommet));
#ifdef SMARTPTR64
static_assert(alignof(giac::gen) >= alignof(ulonglong));
static_assert(offsetof(EnclosedValue, value) % alignof(ulonglong) == 0);
static_assert(offsetof(giac::ref_symbolic, s) + offsetof(giac::symbolic, feuille) ==
              offsetof(giac::alias_ref_symbolic, feuille));
#else
static_assert(sizeof(void *) == 4);
static_assert(alignof(giac::gen) == 4);
static_assert(offsetof(EnclosedValue, value) == 4);
#endif
}
#endif
