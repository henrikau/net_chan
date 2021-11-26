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
    test -d build/ || { mkdir build; meson build; }
    ninja -C build/
    sudo setcap cap_net_raw,cap_net_admin=eip build/testnetfifo
    sudo setcap cap_net_raw,cap_net_admin=eip build/testpdu
    sudo setcap cap_net_raw,cap_net_admin=eip build/testprog
    sudo setcap cap_net_raw,cap_net_admin=eip build/testnfmacro
    sudo setcap cap_net_raw,cap_net_admin,cap_sys_nice=eip build/testmrp
    sudo setcap cap_net_raw,cap_net_admin=eip build/talker
    sudo setcap cap_net_raw,cap_net_admin=eip build/listener
    pushd build > /dev/null
    meson test -v
    popd > /dev/null

    dirs=$(for f in $(find . -name "*.[c|h]" -o -name "meson.build"); do dirname ${f}; done|grep -v build|sort|uniq)
done

popd > /dev/null
