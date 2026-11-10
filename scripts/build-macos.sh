#!/bin/bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
export MACOSX_DEPLOYMENT_TARGET=13.5
sdk_path=$(xcrun --sdk macosx --show-sdk-path)
dependency_root="$project_dir/.cache/macos-deps"
mkdir -p "$dependency_root"
# 静态打包 sodium；网络、归档和图形框架使用 macOS 系统库，不依赖客户安装 Homebrew。
for architecture in arm64 x86_64; do
  build_dir="$dependency_root/build-$architecture"
  mkdir -p "$build_dir"
  (
    cd "$build_dir"
    "$project_dir/.cache/libsodium-1.0.22/configure" \
      --host="$architecture-apple-darwin" --prefix="$dependency_root/$architecture" \
      --disable-shared --enable-static \
      CC="$(xcrun -f clang)" CFLAGS="-O2 -arch $architecture -isysroot $sdk_path"
    make -j 3
    make install
  )
done
universal="$dependency_root/universal"
mkdir -p "$universal/lib/pkgconfig"
cp -R "$dependency_root/arm64/include" "$universal/"
lipo -create "$dependency_root/arm64/lib/libsodium.a" \
  "$dependency_root/x86_64/lib/libsodium.a" -output "$universal/lib/libsodium.a"
cat > "$universal/lib/pkgconfig/libsodium.pc" <<EOF
prefix=$universal
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: libsodium
Description: Sodium cryptography library
Version: 1.0.22
Libs: -L\${libdir} -lsodium
Cflags: -I\${includedir}
EOF
export PKG_CONFIG_PATH="$universal/lib/pkgconfig"
cmake -S . -B build-native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.5 \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DWX_SOURCE_DIR="$project_dir/.cache/wxWidgets-3.2.12" \
  -DCURL_INCLUDE_DIR="$sdk_path/usr/include" -DCURL_LIBRARY="$sdk_path/usr/lib/libcurl.tbd" \
  -DLibArchive_INCLUDE_DIR="$sdk_path/usr/include" -DLibArchive_LIBRARY="$sdk_path/usr/lib/libarchive.tbd"
cmake --build build-native --parallel 3
