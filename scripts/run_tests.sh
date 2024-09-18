#!/bin/bash
set -e

pushd "$(dirname $(dirname $(realpath -s $0)))" > /dev/null


if ! meson test -C build/ ; then
    cat meson-logs/testlog.txt
    exit 1
fi

if [[ ! -z $(which pmccabe) ]]; then
    echo "disabling pmccabe for now"
    pmccabe -v /dev/null \
	| head -n6; pmccabe ../src/* \
	| sort -nk 2 \
	| tail -n20; echo "Files: ";
    pmccabe -F src/* |sort -nk 2;
fi

popd > /dev/null
