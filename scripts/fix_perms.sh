#!/bin/bash
set -e
filelist=("testnetfifo"
	  "testpdu"
	  "testnh"
	  "testchan"
	  "talker"
	  "listener"
	  "cpp_listener"
	  "cpp_talker"
	  "manychan"
	 )

pushd "$(dirname $(dirname $(realpath -s $0)))/build" > /dev/null
if [[ $(id -u) -eq 0 ]]; then
    for f in ${filelist[@]}; do
	setcap cap_net_raw,cap_net_admin=eip ${f}
    done
else
    for f in ${filelist[@]}; do
	sudo setcap cap_net_raw,cap_net_admin=eip ${f}
    done
fi
popd > /dev/null
