#!/usr/bin/env bash
# Runs the curated clang-tidy set (see ../.clang-tidy) over the shipped algorithmic core. The two
# diagnostic-heavy device entry files, lua_module.cc and device_probe.cc, are left out until their
# trace instrumentation is stripped, since their only findings are intentional ignored IO returns in
# that scratch code. Skips when clang-tidy is absent, the same way the other checks skip.
set -u

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/.." && pwd)

clang_tidy=$(command -v clang-tidy 2>/dev/null || true)
if [ -z "$clang_tidy" ]; then
    echo "clang-tidy: skipped, not found"
    exit 0
fi
if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists 'gmp = 6.3.0'; then
    echo "clang-tidy: GMP 6.3.0 development headers not found"
    exit 1
fi
read -r -a gmp_cflags <<< "$(pkg-config --cflags 'gmp = 6.3.0')"

cd "$project" || exit 2

files=(
    src/core/ast.cc src/core/canonical.cc src/core/context.cc src/core/parser.cc src/core/print.cc
    src/steps/derivation.cc src/steps/differentiate.cc src/steps/integrate.cc src/steps/linear.cc
    src/steps/schema.cc
    src/physics/kinematics.cc src/units/units.cc src/cas/giac/giac_adapter.cc
)

# WarningsAsErrors in .clang-tidy makes any finding a non-zero exit. The output is held back and only
# shown on failure, so a clean run does not bury the rest of the build under per-file progress lines.
out=$("$clang_tidy" --quiet "${files[@]}" -- -std=c++20 -fno-exceptions -fno-rtti -I include \
      "${gmp_cflags[@]}" 2>&1)
if [ $? -eq 0 ]; then
    echo "clang-tidy: ok"
else
    printf '%s\n' "$out" | grep -F -e 'error:' -e 'warning:' || printf '%s\n' "$out"
    echo ""
    echo "clang-tidy: findings above. Fix them, or if a check is genuinely wrong for this code,"
    echo "narrow the curated set in .clang-tidy and say why."
    exit 1
fi
