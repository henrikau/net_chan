#!/bin/bash
set -e

pushd "$(dirname $(dirname $(realpath -s $0)))/build" > /dev/null

if ! meson test ; then
    cat meson-logs/testlog.txt
    exit 1
else
    if [[ ! -z $(which pmccabe) ]]; then
	echo "disabling pmccabe for now"
	pmccabe -v /dev/null \
	    | head -n6; pmccabe ../src/* \
	    | sort -nk 2 \
	    | tail -n20; echo "Files: ";
	pmccabe -F ../src/* |sort -nk 2;
    fi
fi
popd > /dev/null
