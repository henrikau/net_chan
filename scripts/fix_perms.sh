#!/bin/bash
set -e
filelist=("testnetfifo"
	  "testpdu"
	  "testnh"
	  "testchan"
	  "testtas"
	  "talker"
	  "listener"
	  "cpp_listener"
	  "cpp_talker"
	  "manychan"
	  "sendat_tx"
	  "sendat_rx"
	 )
echo "Fixing permissions for compiled binaries (id: $(id -u))"
pushd "$(dirname $(dirname $(realpath -s $0)))/build" > /dev/null
if [[ $(id -u) -eq 0 ]]; then
    for f in ${filelist[@]}; do
	echo "Updating ${f} (as $(id -u -n))"
	test -e ${f} && setcap cap_net_raw,cap_net_admin=eip ${f}
    done
else
    for f in ${filelist[@]}; do
	echo "Updating ${f} (as $(id -u -n)))"
	test -e ${f} && sudo setcap cap_net_raw,cap_net_admin=eip ${f}
    done
fi
popd > /dev/null
