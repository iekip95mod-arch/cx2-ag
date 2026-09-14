#!/usr/bin/env bash
set -euo pipefail

sdk="$PWD/vendor/ndl-src/ndl-sdk"
toolchain_ready="$sdk/toolchain/install/.review-ready"
platform=$(uname -s)
if [ "$platform" = Linux ]; then
  sudo apt-get update -qq
  sudo apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config ccache libgmp-dev libmpfr-dev libmpc-dev \
    zlib1g-dev lua5.1 luajit libluajit-5.1-dev php python3-dev libboost-program-options-dev \
    autoconf automake libtool bison flex texinfo wget
  sudo update-alternatives --set lua-interpreter /usr/bin/lua5.1
  jobs=$(nproc)
else
  brew install ccache gmp php lua luajit boost
  export CPATH="$(brew --prefix boost)/include${CPATH:+:$CPATH}"
  export LIBRARY_PATH="$(brew --prefix boost)/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
  jobs=$(sysctl -n hw.ncpu)
fi
export PATH="$sdk/bin:$sdk/toolchain/install/bin:$PATH"
if [ "$platform" = Linux ]; then
  if [ ! -x "$sdk/toolchain/install/bin/arm-none-eabi-gcc" ] || [ ! -f "$toolchain_ready" ]; then
    (cd "$sdk/toolchain" && NDL_SKIP_GDB=1 PARALLEL="-j$jobs" sh ./build_toolchain.sh)
    make -C "$sdk" build-libndls build-tools -j"$jobs"
    bash vendor/deps/build-gmp.sh
    bash vendor/deps/build-mpfr-mpfi.sh
    touch "$toolchain_ready"
  fi
else
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
fi
make -C "$sdk" build-libndls build-tools -j"$jobs"
test -x "$sdk/toolchain/install/bin/arm-none-eabi-gcc"
test -x "$sdk/bin/genzehn"
test -f "$sdk/lib/libsyscalls.a"
{
  if [ "$platform" != Linux ]; then
    echo "PKG_CONFIG_PATH=$(brew --prefix luajit)/lib/pkgconfig:$(brew --prefix gmp)/lib/pkgconfig"
  fi
  echo "NDL_SDK=$sdk"
  echo "GIAC_ROOT=$PWD/vendor/khi-src"
} >> "$GITHUB_ENV"
echo "Reviewer SDK and Lua prerequisites are available."
