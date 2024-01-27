#!/bin/bash
set -e

pushd "$(dirname $(dirname $(realpath -s $0)))" > /dev/null
test -e doxygen.in || touch doxygen.in
test -d build/ || { mkdir build/ ; /usr/bin/meson build; }

ninja -C build/ && \
    g++ -o build/cpp_listener examples/listener.cpp build/libnetchan.a -lboost_program_options -pthread -I include && \
    g++ -o build/cpp_talker examples/talker.cpp build/libnetchan.a -lboost_program_options -pthread -I include && \
    ./scripts/fix_perms.sh
popd > /dev/null
