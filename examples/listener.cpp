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
// 2-1 g++ -o build/cpp_listener examples/listener.cpp -I include -L build/ -lnetchan
// -- or link statically
// 2-2 g++ -o build/cpp_listener examples/listener.cpp build/libnetchan.a build/libmrp.a -pthread -I include/

#include <iostream>
#include <netchan.hpp>
#include "manifest.h"
struct ts {
    uint64_t tx;
    uint64_t rx;
};

#define LIMIT 1024
int main()
{
    netchan::NetHandler nh("enp6s0", "listener.csv", false);
    netchan::NetChanRx rx(nh, &nc_channels[3]);

    uint64_t recv_ts = 0;
    struct ts ts[LIMIT];
    int idx = 0;
    for (; idx < LIMIT; idx++) {
        if (rx.read(&recv_ts)) {
            uint64_t rx_ts = tai_get_ns();
            if (recv_ts == -1)
                break;
            ts[idx].tx = recv_ts;
            ts[idx].rx = rx_ts;
        }
    }
    FILE *fp = fopen("ts.csv", "w+");
    if (fp) {
        fprintf(fp,"idx,tx,rx\n");
        for (int i = 0; i < idx; i++) {
            fprintf(fp,"%d,%" PRIu64 ",%" PRIu64 "\n", i, ts[i].tx, ts[i].rx);
        }
        fclose(fp);
    }
    return 0;
}
