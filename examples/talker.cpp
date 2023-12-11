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
#include <netchan_utils.h>
#include <signal.h>
#include "manifest.h"

#include <boost/program_options.hpp>
namespace po = boost::program_options;

static std::string nic = "enp7s0";
static int loops = 1000;
static bool running(false);

netchan::NetChanRx *rx;
netchan::NetChanTx *tx;

void sighandler(int signum)
{
    if (running)
        rx->stop();

    running = false;
    printf("%s(): Got signal (%d), closing\n", __func__, signum);
    fflush(stdout);
}


int main(int argc, char *argv[])
{
    bool use_srp = false;
    po::options_description desc("Talker options");
    desc.add_options()
        ("help,h", "Show help")
        ("verbose,v", "Increase logging output")
        ("interface,i", po::value<std::string>(&nic), "Change network interface")
        ("loops,l", po::value<int>(&loops), "Number of iterations (default 1000)")
        ("use_srp,S", "Run with SRP enabled")
        ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        exit(0);
    }
    if (vm.count("use_srp"))
        use_srp = true;

    std::cout << "Using NIC " << nic << ", running for " << loops << " iterations" << std::endl;

    netchan::NetHandler nh(nic, "talker.csv", use_srp);
    if (vm.count("verbose"))
        nh.verbose();

    tx = new netchan::NetChanTx(nh, &nc_channels[IDX_17]);
    rx = new netchan::NetChanRx(nh, &nc_channels[IDX_18]);
    struct periodic_timer *pt = pt_init(0, 100*NS_IN_MS, CLOCK_TAI);

    uint64_t ts = 0;
    running = true;
    signal(SIGINT, sighandler);
    while (--loops > 0 && running) {
        ts = tai_get_ns();
        if (!tx->send(&ts))
            break;

        if (!rx->read_wait(&ts)) {
            printf("rx read failed\n");
            break;
        }

        if (ts == -1)
            break;
        pt_next_cycle(pt);
    }

    printf("Loop stopped, signalling other end\n");
    ts = -1;
    tx->send(&ts);

    // rx->stop();
    tx->stop();
    delete rx;
    delete tx;
    return 0;
}
