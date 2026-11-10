#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${WX_SOURCE_DIR:?Set WX_SOURCE_DIR to wxWidgets 3.2 source directory}"
# 构建依赖在开发机准备，目标客户电脑不需要编译工具链。
for dependency in libcurl libarchive libsodium; do
  pkg-config --exists "$dependency" || {
    printf '%s\n' "Missing build dependency: $dependency" >&2
    exit 1
  }
done
cmake -S "$project_dir" -B "$project_dir/build-native" \
  -DCMAKE_BUILD_TYPE=Release -DWX_SOURCE_DIR="$WX_SOURCE_DIR"
cmake --build "$project_dir/build-native" --parallel 4
