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
#include <signal.h>
#include "manifest.h"

static std::string nic = "enp7s0";
static bool running(false);
netchan::NetChanRx *rx;
netchan::NetChanTx *tx;

#include <boost/program_options.hpp>
namespace po = boost::program_options;

void sighandler(int signum)
{
	printf("%s(): Got signal (%d), closing\n", __func__, signum);
	fflush(stdout);
	running = false;
        rx->stop();
}
int main(int argc, char *argv[])
{
    po::options_description desc("Talker options");
    desc.add_options()
        ("help,h", "Show help")
        ("verbose,v", "Increase logging output")
        ("interface,i", po::value<std::string>(&nic), "Change network interface")
        ("use_srp,S", "Run with SRP enabled")
        ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        exit(0);
    }
    std::cout << "Using NIC " << nic << std::endl;
    bool use_srp = false;
    if (vm.count("use_srp"))
        use_srp = true;

    netchan::NetHandler nh(nic, "listener.csv", use_srp);
    if (vm.count("verbose"))
        nh.verbose();

    rx = new netchan::NetChanRx(nh, &nc_channels[IDX_17]);
    tx = new netchan::NetChanTx(nh, &nc_channels[IDX_18]);

    uint64_t recv_ts = 0;
    running = true;
    signal(SIGINT, sighandler);
    while (running) {
        if (rx->read(&recv_ts)) {
            if (recv_ts == -1)
                break;

            uint64_t rx_ts = tai_get_ns();
            if (!tx->send_wait(&recv_ts))
                break;
        }
    }

    // Signal other end that we're closing down
    recv_ts = -1;
    tx->send(&recv_ts);

    rx->stop();
    tx->stop();
    delete rx;
    delete tx;
    return 0;
}
