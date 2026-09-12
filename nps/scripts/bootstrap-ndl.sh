#!/usr/bin/env bash
set -eu

fail() {
    printf 'bootstrap-ndl: %s\n' "$1" >&2
    exit 1
}

require_executable() {
    [ -x "$sdk/$1" ] ||
        fail "missing executable SDK input: $1. Build the pinned vendored ndl toolchain first."
}

require_file() {
    [ -r "$sdk/$1" ] ||
        fail "missing readable SDK input: $1. Build the pinned vendored ndl SDK first."
}

require_pin() {
    local actual
    actual=$(awk -v name="$1" '$1 ~ ("^" name "=") { print $1; exit }' "$build_script")
    [ "$actual" = "$1=$2" ] ||
        fail "vendored toolchain pin for $1 is not $2"
}

script_dir=$(cd -P -- "${0%/*}" 2>/dev/null && pwd) || fail "cannot locate the script directory"
project_dir=$(cd -P -- "$script_dir/.." 2>/dev/null && pwd) || fail "cannot locate the project directory"
[ -r "$project_dir/cmake/toolchains/ndl-arm926ej-s.cmake" ] ||
    fail "the ndl CMake toolchain file is missing"

for utility in awk bash cmake git make ninja readlink tr uname; do
    command -v "$utility" >/dev/null 2>&1 ||
        fail "required host tool is unavailable: $utility. Install it and put it on PATH."
done

requested_sdk=${NDL_SDK:-$project_dir/../vendor/ndl-src/ndl-sdk}
[ -d "$requested_sdk" ] ||
    fail "ndl SDK directory does not exist: $requested_sdk. Provide NDL_SDK or restore the vendored checkout."
sdk=$(cd -P -- "$requested_sdk" 2>/dev/null && pwd) || fail "cannot resolve the ndl SDK directory"

git -C "$sdk" ls-files --error-unmatch toolchain/build_toolchain.sh >/dev/null 2>&1 ||
    fail "ndl SDK is not tracked by a Git repository: $sdk. Restore the vendored tree."
revision=$(git -C "$sdk" rev-parse HEAD 2>/dev/null) || revision='uncommitted'

build_script=$sdk/toolchain/build_toolchain.sh
require_file toolchain/build_toolchain.sh
require_pin BINUTILS binutils-2.44
require_pin GCC gcc-14.2.0
require_pin NEWLIB newlib-4.5.0.20241231
require_pin GDB gdb-16.2

executables=(
    bin/arm-none-eabi-ld.gold
    bin/genzehn
    bin/nspire-g++
    bin/nspire-gcc
    bin/nspire-ld
    bin/nspire-tools
    tools/luna/luna
    toolchain/install/bin/arm-none-eabi-ar
    toolchain/install/bin/arm-none-eabi-as
    toolchain/install/bin/arm-none-eabi-g++
    toolchain/install/bin/arm-none-eabi-gcc
    toolchain/install/bin/arm-none-eabi-ld
    toolchain/install/bin/arm-none-eabi-nm
    toolchain/install/bin/arm-none-eabi-ranlib
)
for executable in "${executables[@]}"; do
    require_executable "$executable"
done

files=(
    include/nucleus.h
    lib/libndls.a
    lib/libsyscalls.a
    libndls/config.c
    libndls/file_each.c
    system/crt0.o
    system/crti.o
    system/crtn.o
    system/ldscript
    toolchain/install/arm-none-eabi/lib/libc.a
    toolchain/install/arm-none-eabi/lib/libm.a
    toolchain/install/arm-none-eabi/lib/libstdc++.a
    toolchain/install/lib/libgmp.a
    toolchain/install/lib/libmpfi.a
    toolchain/install/lib/libmpfr.a
)
for file in "${files[@]}"; do
    require_file "$file"
done

# The four .built_ stamps used to stand here. They record that build_toolchain.sh ran on this
# machine, which stopped being the only way to get a prefix once one was committed to the
# repository, and a prefix that arrives unpacked carries none of them. Every claim they made is
# available from the prefix itself: binutils from arm-none-eabi-ld below, both GCC passes from
# arm-none-eabi-gcc below and from libstdc++.a above, which only the second pass produces, and
# newlib from the header it installs. The snapshot date is not recoverable from an installed tree,
# so the pin above is what carries it.
newlib_header=toolchain/install/arm-none-eabi/include/_newlib_version.h
newlib_expected=4.5.0
require_file "$newlib_header"
newlib_version=''
while read -r directive name value; do
    [ "$directive" = '#define' ] && [ "$name" = _NEWLIB_VERSION ] || continue
    value=${value#\"}
    newlib_version=${value%\"}
    break
done < "$sdk/$newlib_header"
[ "$newlib_version" = "$newlib_expected" ] ||
    fail "installed newlib is ${newlib_version:-unreadable}, expected $newlib_expected"

ndl_path=$sdk/bin:$sdk/toolchain/install/bin:$PATH
reported_sdk=$(PATH="$ndl_path" "$sdk/bin/nspire-tools" path 2>/dev/null) ||
    fail "nspire-tools could not resolve the SDK"
reported_sdk=${reported_sdk%/}
[ "$reported_sdk" = "$sdk" ] || fail "nspire-tools resolved a different SDK: $reported_sdk"

gcc_version=$("$sdk/toolchain/install/bin/arm-none-eabi-gcc" -dumpfullversion 2>/dev/null) ||
    fail "the ARM GCC compiler could not start"
[ "$gcc_version" = 14.2.0 ] || fail "ARM GCC is $gcc_version, expected 14.2.0"

target=$("$sdk/toolchain/install/bin/arm-none-eabi-g++" -dumpmachine 2>/dev/null) ||
    fail "the ARM C++ compiler could not report its target"
[ "$target" = arm-none-eabi ] || fail "ARM C++ compiler target is $target, expected arm-none-eabi"

number_abi_symbols=$("$sdk/toolchain/install/bin/arm-none-eabi-nm" -u "$sdk/lib/libsyscalls.a") ||
    fail "the ndl syscall archive could not be inspected"
case "$number_abi_symbols" in
    *'__aeabi_i2d'*|*'__aeabi_d2iz'*)
        fail "ndl Lua number syscalls contain ABI-losing integer conversions"
        ;;
esac

binutils_output=$("$sdk/toolchain/install/bin/arm-none-eabi-ld" --version 2>&1) ||
    fail "the ARM linker could not report its version"
binutils_line=${binutils_output%%$'\n'*}
case "$binutils_line" in
    *' 2.44') ;;
    *) fail "ARM Binutils version is incompatible: $binutils_line" ;;
esac

genzehn_output=$("$sdk/bin/genzehn" --help 2>&1) || fail "genzehn could not start"
genzehn_line=${genzehn_output%%$'\n'*}
case "$genzehn_line" in
    genzehn\ *) ;;
    *) fail "genzehn returned unrecognized version output: $genzehn_line" ;;
esac

PATH="$ndl_path" "$sdk/bin/nspire-ld" --help >/dev/null 2>&1 || fail "nspire-ld could not start"
luna_output=$("$sdk/tools/luna/luna" --help 2>&1) || fail "luna could not start"
luna_line=${luna_output%%$'\n'*}
case "$luna_line" in
    'Luna v'*' usage:') luna_version=${luna_line% usage:} ;;
    *) fail "luna returned unrecognized version output: $luna_line" ;;
esac

if ! PATH="$ndl_path" "$sdk/bin/nspire-g++" -std=c++20 -Os -marm -fno-exceptions -fno-rtti \
    -fsyntax-only -x c++ - >/dev/null 2>&1 <<'EOF'
#include <cstdint>
#include <span>
#include <string_view>

consteval std::uint32_t checked_value() { return 4U; }
constexpr std::string_view label = "nps";
static_assert(checked_value() == 4U && label.size() == 3U);

int main() {
    std::uint32_t values[] = {checked_value()};
    std::span<std::uint32_t> view(values);
    return view.empty() ? 1 : 0;
}
EOF
then
    fail "nspire-g++ rejected the required C++20 target flags"
fi

gdb=$sdk/toolchain/install/bin/arm-none-eabi-gdb
if [ -x "$gdb" ]; then
    gdb_output=$("$gdb" --version 2>&1) || fail "the ARM GDB executable could not start"
    gdb_line=${gdb_output%%$'\n'*}
    case "$gdb_line" in
        *' 16.2') gdb_state=installed ;;
        *) fail "ARM GDB version is incompatible: $gdb_line" ;;
    esac
else
    gdb_state='pinned, optional debugger not installed'
    printf 'bootstrap-ndl: warning: GDB 16.2 is pinned but not installed; build it before target debugging\n' >&2
fi

printf 'bootstrap-ndl: revision %s\n' "$revision"
printf 'bootstrap-ndl: binutils 2.44\n'
printf 'bootstrap-ndl: gcc %s\n' "$gcc_version"
printf 'bootstrap-ndl: newlib %s, from the newlib-4.5.0.20241231 pin\n' "$newlib_version"
printf 'bootstrap-ndl: gdb 16.2 (%s)\n' "$gdb_state"
printf 'bootstrap-ndl: target %s\n' "$target"
printf 'bootstrap-ndl: flags -Os -marm -fno-exceptions -fno-rtti\n'
printf 'bootstrap-ndl: packaging %s, nspire-ld, %s\n' "$genzehn_line" "$luna_version"
printf 'bootstrap-ndl: ready\n'
