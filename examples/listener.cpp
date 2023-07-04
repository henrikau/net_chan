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
// 2. g++ -o build/cpp_listener examples/listener.cpp -I include -L build/ -lnetchan

#include <iostream>
#include <netchan.hpp>
#include "manifest.h"

int main()
{
    netchan::NetHandler nh("enp6s0", false);
    netchan::NetChanRx rx(nh, &nc_channels[0]);

    struct sensor s = { 0 };
    for (int i = 0; i < 1000; i++) {
        if (rx.read(&s)) {
            printf("%lu : %lx\n", s.seqnr, s.val);
            if (s.seqnr == -1)
                break;
        }
    }

    return 0;
}
