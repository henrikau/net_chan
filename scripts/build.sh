#!/bin/bash
set -e

pushd "$(dirname $(dirname $(realpath -s $0)))" > /dev/null
test -e doxygen.in || touch doxygen.in

if [[ -d build/ ]]; then
    meson setup --reconfigure build/ -Db_coverage=true
else
    meson setup build -Db_coverage=true
fi

ninja -C build/ && \
    g++ -o build/cpp_listener examples/listener.cpp build/libnetchan.a -lboost_program_options -pthread -I include && \
    g++ -o build/cpp_talker examples/talker.cpp build/libnetchan.a -lboost_program_options -pthread -I include && \
    ./scripts/fix_perms.sh
popd > /dev/null
