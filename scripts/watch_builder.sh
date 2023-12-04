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
    ninja -C build/ &&  {
	echo "Building C++ apps"
	g++ -o build/cpp_listener examples/listener.cpp build/libnetchan.a build/libmrp.a -lboost_program_options -pthread -I include
	g++ -o build/cpp_talker examples/talker.cpp build/libnetchan.a build/libmrp.a -lboost_program_options -pthread -I include
	# only run tests if build was ok
	./scripts/fix_perms.sh
	pushd build > /dev/null
	if [[ ! -z $(which pmccabe) ]]; then
	    meson test && { pmccabe -v /dev/null | head -n6; pmccabe ../src/* | sort -nk 2 | tail -n20; echo "Files: "; pmccabe -F ../src/* |sort -nk 2; }
	else
	    meson test
	fi
	popd > /dev/null
    }

    dirs=$(for f in $(find . -name "*.[c|h]" -o -name "meson.build"); do dirname ${f}; done|grep -v build|sort|uniq)
done

popd > /dev/null
