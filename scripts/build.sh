#!/bin/bash
set -e
test -d build/ && rm -rf build/

mkdir build/
meson build

pushd "$(dirname $(dirname $(realpath -s $0)))" > /dev/null
ninja -C build/
popd > /dev/null
