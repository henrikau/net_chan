#!/bin/bash
set -e
pushd "$(dirname $(dirname $(realpath -s $0)))" > /dev/null

TMPINSTALL=$(pwd)/build/tmpinstall
echo "Using tmpinstall=${TMPINSTALL}"

pushd build/ > /dev/null
test -d ${TMPINSTALL} && rm -rf ${TMPINSTALL}
mkdir ${TMPINSTALL}
meson install --destdir ${TMPINSTALL}

pushd ${TMPINSTALL} >/dev/null
sudo cp -rv usr/local/include/* /usr/local/include/.
sudo cp -v usr/local/lib/x86_64-linux-gnu/*.so /usr/local/lib/x86_64-linux-gnu/.
sudo cp -v usr/local/lib/x86_64-linux-gnu/*.a /usr/local/lib/x86_64-linux-gnu/.

popd >/dev/null			# TMPINSTALL

popd >/dev/null			# build

popd >/dev/null			# basepath
