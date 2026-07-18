#!/bin/sh

set -eu



python ./waf build
python ./waf install

INSTALL_ROOT="${INSTALL_ROOT:-android_build}"
STRIP="${STRIP:-llvm-strip}"
NDK="${NDK:-/data/user/0/com.termux/files/home/android-sdk/ndk/29.0.14206865}"
LIB_DIR="${LIB_DIR:-$INSTALL_ROOT/lib/arm64-v8a}"
LIBCXX_SHARED="${LIBCXX_SHARED:-$(find "$NDK/toolchains/llvm/prebuilt" -type f -path '*/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so' -print -quit)}"

if [ ! -f "$LIBCXX_SHARED" ]; then
	echo "Unable to find ARM64 libc++_shared.so under $NDK" >&2
	exit 1
fi

mkdir -p "$LIB_DIR"
cp "$LIBCXX_SHARED" "$LIB_DIR/libc++_shared.so"

find "$INSTALL_ROOT" -type f -name '*.so' -exec "$STRIP" --strip-unneeded {} +
