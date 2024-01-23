#!/bin/bash
set -e

pushd "$(dirname $(dirname $(realpath -s $0)))/build" > /dev/null
if [[ ! -z $(which pmccabe) ]]; then
    meson test && { pmccabe -v /dev/null | head -n6; pmccabe ../src/* | sort -nk 2 | tail -n20; echo "Files: "; pmccabe -F ../src/* |sort -nk 2; }
else
    meson test
fi

popd > /dev/null
