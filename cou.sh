export CFLAGS="-O2 -g0 -fno-lto -fomit-frame-pointer"
export CXXFLAGS="-DNO_STD_REGEX=1 -Wno-error -O2 -g0 -fno-lto -fomit-frame-pointer"
export LDFLAGS="-lunwind"
export NDK="${NDK:-/data/user/0/com.termux/files/home/android-sdk/ndk/29.0.14206865}"
export PATH=$NDK/toolchains/llvm/prebuilt/linux-arm64/bin:$PATH
export CC=aarch64-linux-android34-clang
export CXX=aarch64-linux-android34-clang++
export STRIP=$NDK/toolchains/llvm/prebuilt/linux-arm64/bin/llvm-strip

INSTALL_ROOT=android_build

python3 ./waf configure -T release \
  --prefix=$INSTALL_ROOT \
  --android=aarch64,host,34 \
  --target=$INSTALL_ROOT/aarch64 \
  --disable-warns \
  --togles \
  --build-games=hl2 \
  --enable-opus
