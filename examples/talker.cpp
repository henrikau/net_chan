/*
 * Copyright 2023 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */

// This file is not included in the meson build script (meson does not
// like mixing C and C++ in the same project).
//
// To compile:
// 1. build netchan
//    meson build
//    ninja -C build/
//
// Compile and link C++ code
// 2-1 g++ -o build/cpp_talker examples/talker.cpp -I include -L build/ -lnetchan
// -- or link statically
// 2-2 g++ -o build/cpp_talker examples/talker.cpp build/libnetchan.a build/libmrp.a -pthread -I include/

#include <iostream>
#include <netchan.hpp>
#include <unistd.h>
#include "manifest.h"

int main()
{
    netchan::NetHandler nh("enp6s0", false);
    netchan::NetChanTx tx(nh, &nc_channels[0]);

    struct sensor s = { .val = 0xdeadbeefa0a0a0a0, .seqnr = 1337 };
    for (int i = 0; i < 50; i++) {
        s.seqnr = i;
        if (!tx.send(&s)) {
            printf("%s(): sending failed\n", __func__);
            break;
        }
        usleep(100000);
    }
    s.seqnr = -1;
    tx.send(&s);

    return 0;
}
