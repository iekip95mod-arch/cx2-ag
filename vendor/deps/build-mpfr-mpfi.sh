#!/bin/bash
# MPFR then MPFI, both against the GMP already installed into the ndless toolchain prefix. Order
# matters: MPFI needs MPFR's headers at configure time.
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

cd "$HERE"

echo "=== MPFR"
rm -rf mpfr-4.2.2
tar xJf mpfr-4.2.2.tar.xz
cd mpfr-4.2.2
./configure --host=arm-none-eabi --prefix="$PREFIX" --with-gmp="$PREFIX" \
	--disable-shared --enable-static --disable-thread-safe
make -j8
make install
cd ..
echo "MPFR OK"

echo "=== MPFI"
rm -rf mpfi-master
tar xzf mpfi.tar.gz
cd mpfi-master
# gitlab archive ships no configure script
[ -x ./configure ] || ./autogen.sh || autoreconf -fi
./configure --host=arm-none-eabi --prefix="$PREFIX" \
	--with-gmp="$PREFIX" --with-mpfr="$PREFIX" \
	--disable-shared --enable-static
make -j8
make install
echo "MPFI OK"

ls -la "$PREFIX/lib/libmpfr.a" "$PREFIX/lib/libmpfi.a"
