#!/bin/bash
set -euo pipefail

research_dir="$(cd "$(dirname "$0")" && pwd)"
source_root="$(cd "$research_dir/../../vendor/ndl-src" && pwd)"
build_root="${1:-/tmp/cx2-ndl-relocation-build}"
if [ -e "$build_root" ]; then
    printf 'Build directory already exists: %s\n' "$build_root" >&2
    exit 1
fi

for relative in ndl-sdk/bin ndl-sdk/include ndl-sdk/lib \
    ndl-sdk/libndls ndl-sdk/system ndl-sdk/tools/luna \
    ndl-sdk/tools/zehn_loader ndl/src/resources \
    ndl/src/persistent-6.4 ndl/src/installer-6.2 ndl/src/tools/LuaBin
do
    destination="$build_root/ndl-src/$relative"
    mkdir -p "$(dirname "$destination")"
    cp -R "$source_root/$relative" "$destination"
done
mkdir -p "$build_root/ndl-src/ndl/calcbin"
patch -p1 -d "$build_root" -i "$research_dir/ndl-relocation.patch"

sdk="$build_root/ndl-src/ndl-sdk"
export NDL_TOOLCHAIN_PATH="${NDL_TOOLCHAIN_PATH:-$source_root/ndl-sdk/toolchain/install/bin}"
export NDL_ZEHN_PATH="$sdk/tools/zehn_loader"
export PATH="$sdk/bin:$NDL_TOOLCHAIN_PATH:$PATH"

make -B -C "$sdk/libndls" \
    GCCFLAGS='-O2 -funroll-loops -nostdlib -Wall -Werror -Wextra -ffunction-sections -fdata-sections -DNDL_RELOCATED'
make -B -C "$build_root/ndl-src/ndl/src/resources" \
    GCCFLAGS='-Wall -Wextra -W -marm -Os -DNDL_RELOCATED'
make -B -C "$build_root/ndl-src/ndl/src/persistent-6.4" \
    GCCFLAGS=-DNDL_RELOCATED
make -B -C "$build_root/ndl-src/ndl/src/installer-6.2" \
    GCCFLAGS=-DNDL_RELOCATED

shasum -a 256 "$build_root/ndl-src/ndl/calcbin/"*.tns
