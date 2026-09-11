#include <cstdint>
#include <iostream>
#include <new>
#include <string>
#include <vector>
#include "giac.h"
#include "../../src/cas/giac/giac_layout.h"

namespace {
unsigned checks, failures;

void check(bool condition, const char *label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cout << "FAIL " << label << '\n';
    }
}

struct EnclosedValue {
    std::uint32_t prefix;
    giac::gen value;
    std::uint32_t suffix;
};
}

int main() {
    giac::context context;
    alignas(giac::gen) unsigned char storage[2 * sizeof(giac::gen)];
    giac::gen *placed = new (static_cast<void *>(storage + alignof(giac::gen))) giac::gen(7);
    check(placed->type == giac::_INT_ && placed->val == 7, "legal placement constructor");
    *placed = giac::gen(-9);
    check(placed->type == giac::_INT_ && placed->val == -9, "legal placement assignment");
    placed->~gen();

    const giac::gen fraction = giac::gen(2) / giac::gen(3);
    const giac::gen complex(3, 4);
    const giac::gen variable(giac::identificateur("layout_x"));
    const giac::gen symbolic(giac::symbolic(giac::at_sin, variable));
    check(fraction.type == giac::_FRAC && fraction._FRACptr->num == giac::gen(2) &&
              fraction._FRACptr->den == giac::gen(3), "fraction fields");
    check(complex.type == giac::_CPLX && complex._CPLXptr[0] == giac::gen(3) &&
              complex._CPLXptr[1] == giac::gen(4), "complex fields");
    check(symbolic.type == giac::_SYMB && symbolic._SYMBptr->sommet == giac::at_sin &&
              symbolic._SYMBptr->feuille == variable, "symbolic fields");

    const std::vector<giac::gen> values = {giac::gen(7), fraction, complex, symbolic};
    for (const giac::gen &expected : values) {
        EnclosedValue enclosed{0x12345678U, expected, 0x87654321U};
        check(reinterpret_cast<std::uintptr_t>(&enclosed.value) % alignof(giac::gen) == 0,
              "enclosed value alignment");
        check(enclosed.value == expected, "enclosed copy");
        giac::gen copy = enclosed.value;
        enclosed.value = giac::gen(42);
        check(copy == expected && enclosed.prefix == 0x12345678U && enclosed.suffix == 0x87654321U,
              "copy ownership and enclosing sentinels");
        giac::swapgen(copy, enclosed.value);
        check(copy == giac::gen(42) && enclosed.value == expected, "whole value swap");
        const std::string printed = enclosed.value.print(&context);
        const giac::gen parsed = giac::eval(giac::gen(printed, &context), 1, &context);
        check(parsed == expected, "printed value roundtrip");
    }
    check(giac::normal(fraction + giac::gen(1) / giac::gen(6), &context) ==
              giac::gen(5) / giac::gen(6), "exact fraction addition");
    check(giac::normal(complex * giac::conj(complex, &context), &context) == giac::gen(25),
              "complex norm");
    check(giac::subst(symbolic, variable, giac::gen(0), false, &context).eval(1, &context) ==
              giac::gen(0), "symbolic substitution");
    check(giac::cst_i.type == giac::_CPLX && giac::cst_i._CPLXptr[0] == giac::gen(0) &&
              giac::cst_i._CPLXptr[1] == giac::gen(1), "library complex constant");
    std::cout << "value layout: " << checks << " checks, " << failures << " failed"
              << ", gen size=" << sizeof(giac::gen) << " alignment=" << alignof(giac::gen)
              << " embedded offset=" << offsetof(EnclosedValue, value) << '\n';
    return failures != 0;
}
