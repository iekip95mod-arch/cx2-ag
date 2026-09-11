#!/bin/bash
# Cross-build GMP for the Nspire, following src/README.nspire in the giac tree. Installs into the
# ndless toolchain prefix so Makefile.nspire finds it where it already looks.
set -e

HERE=$(cd -P -- "${0%/*}" && pwd)
SDK=$(cd -P -- "$HERE/../ndl-src/ndl-sdk" && pwd)
PREFIX=$SDK/toolchain/install

export PATH="$PREFIX/bin:$SDK/bin:$PATH"
export CC=nspire-gcc
export CXX=nspire-g++
export LD=nspire-ld
export AR=arm-none-eabi-ar
export AS=nspire-as
export RANLIB=arm-none-eabi-ranlib
export STRIP=arm-none-eabi-strip

cd "$HERE"
rm -rf gmp-6.3.0
tar xJf gmp-6.3.0.tar.xz
cd gmp-6.3.0

# No assembly: the ARM926EJ-S paths assume a hosted ABI GMP cannot probe through nspire-gcc, and a
# generic C build is fast enough for a calculator. Static only, since Ndless has no shared objects.
./configure --host=arm-none-eabi --prefix="$PREFIX" \
	--disable-assembly --disable-shared --enable-static \
	--enable-cxx=no

make -j8
make install

echo "GMP BUILD OK"
ls -la "$PREFIX/lib/libgmp.a" "$PREFIX/include/gmp.h"
