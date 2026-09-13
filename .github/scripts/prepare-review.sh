#!/usr/bin/env bash
set -euo pipefail

brew install ccache gmp php lua luajit boost
sdk="$PWD/vendor/ndl-src/ndl-sdk"
toolchain_ready="$sdk/toolchain/install/.review-ready"
stamp=vendor/toolchain/built-from.sha256
want=$(shasum -a 256 vendor/ndl-src/ndl-sdk/toolchain/build_toolchain.sh \
  vendor/deps/build-gmp.sh vendor/deps/build-mpfr-mpfi.sh | shasum -a 256 | cut -d' ' -f1)
test "$want" = "$(cut -d' ' -f1 < "$stamp")"
if [ ! -x "$sdk/toolchain/install/bin/arm-none-eabi-gcc" ] || [ ! -f "$toolchain_ready" ]; then
  git -c credential.helper= -c 'credential.helper=!gh auth git-credential' lfs pull --include "vendor/toolchain/*"
  mkdir -p "$sdk/toolchain/install"
  zstd -d -c vendor/toolchain/ndl-arm-none-eabi-darwin-arm64.tar.zst | tar -x -C "$sdk/toolchain/install"
  touch "$toolchain_ready"
fi
export PATH="$sdk/bin:$sdk/toolchain/install/bin:$PATH"
export CPATH="$(brew --prefix boost)/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$(brew --prefix boost)/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
make -C "$sdk" build-libndls build-tools -j"$(sysctl -n hw.ncpu)"
test -x "$sdk/toolchain/install/bin/arm-none-eabi-gcc"
test -x "$sdk/bin/genzehn"
test -f "$sdk/lib/libsyscalls.a"
{
  echo "PKG_CONFIG_PATH=$(brew --prefix luajit)/lib/pkgconfig:$(brew --prefix gmp)/lib/pkgconfig"
  echo "NDL_SDK=$sdk"
  echo "GIAC_ROOT=$PWD/vendor/khi-src"
} >> "$GITHUB_ENV"
echo "Reviewer SDK and Lua prerequisites are available."
