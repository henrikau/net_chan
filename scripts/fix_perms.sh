#!/bin/bash
pushd "$(dirname $(dirname $(realpath -s $0)))/build"
sudo setcap cap_net_raw,cap_net_admin=eip testnetfifo
sudo setcap cap_net_raw,cap_net_admin=eip testpdu
sudo setcap cap_net_raw,cap_net_admin=eip testnh
sudo setcap cap_net_raw,cap_net_admin=eip testnfmacro
sudo setcap cap_net_raw,cap_net_admin,cap_sys_nice=eip testmrp
sudo setcap cap_net_raw,cap_net_admin=eip talker
sudo setcap cap_net_raw,cap_net_admin=eip listener
popd
