#!/bin/bash
# require: inotify-tools

# find directorires with files
pushd "$(dirname $(dirname $(realpath -s $0)))"

test -z ${NUM_CPUS} && NUM_CPUS=$(($(nproc)*2))

dirs=$(for f in $(find . -name "*.[c|h]" -o -name "meson.build"); do dirname ${f}; done|grep -v build|sort|uniq)
if [[ -z ${dirs} ]]; then
    echo "Could not find any source-files, try to move to the root of the project!"
    exit 1
fi
echo ${dirs}

# This is a fairly naive approach. Once an event is detected,
# inotifywait will return and a new process is started. This means that
# if more events are happening while build.sh is running, we will miss
# them.
while inotifywait -e modify -e create --exclude "\#" ${dirs} ; do
    ./scripts/build.sh && ./scripts/run_tests.sh && ./scripts/analyze_coverage.sh
    dirs=$(for f in $(find . -name "*.[c|h]" -o -name "meson.build"); do dirname ${f}; done|grep -v build|sort|uniq)
done

popd > /dev/null
net
