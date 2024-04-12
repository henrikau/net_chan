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

netchan::NetChanTx *tx;

void sighandler(int signum)
{
    running = false;
    printf("%s(): Got signal (%d), closing\n", __func__, signum);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    struct channel_attrs attr = nc_channels[IDX_17];
    std::string logfile;
    std::string stream_class;
    int tx_prio = DEFAULT_TX_TAS_SOCKET_PRIO;

    po::options_description desc("Talker options");
    desc.add_options()
        ("help,h", "Show help")
        ("verbose,v", "Increase logging output")
        ("interface,i", po::value<std::string>(&nic), "Change network interface")
        ("loops,l", po::value<int>(&loops), "Number of iterations (default 1000)")
        ("log,L", po::value<std::string>(&logfile), "Log to file")
        ("stream_id", po::value<uint64_t>(&attr.stream_id), "Use different StreamID (base10)")
        ("use_srp,S", "Enable SRP")
        ("tx_prio_cbs_a", po::value<int>(&tx_prio), "Use different Tx-prio than the default.")
        ("tx_prio_cbs_b", po::value<int>(&tx_prio), "Use different Tx-prio than the default.")
        ("tx_prio_tas",   po::value<int>(&tx_prio), "Use different Tx-prio than the default.")
        ("stream_class",  po::value<std::string>(&stream_class), "Use specific stream class")
        ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        exit(0);
    }
    bool use_srp = vm.count("use_srp");

    std::cout << "Using NIC " << nic << ", running for " << loops << " iterations" << std::endl;

    netchan::NetHandler nh(nic, logfile, use_srp);
    if (vm.count("verbose"))
        nh.verbose();

    if (vm.count("tx_prio_cbs_a")) {
        if (!nh.set_tx_prio(tx_prio, SC_CLASS_A)) {
            std::cerr << "Failed setting Tx-prio (CBS A) for handler" << std::endl;
            return -1;
        }
    } else if (vm.count("tx_prio_cbs_b")) {
        if (!nh.set_tx_prio(tx_prio, SC_CLASS_B)) {
            std::cerr << "Failed setting Tx-prio (CBS B) for handler" << std::endl;
            return -1;
        }
    } else if (vm.count("tx_tx_prio_tas")) {
        if (!nh.set_tx_prio(tx_prio, SC_TAS)) {
            std::cerr << "Failed setting Tx-prio (TAS) for handler" << std::endl;
            return -1;
        }
    }

    if (vm.count("stream_class")) {
        std::cerr << "Changing stream-class -> " << stream_class << std::endl;
        if (stream_class == "SC_CLASS_A")
            attr.sc = SC_CLASS_A;
        else if (stream_class == "SC_CLASS_B")
            attr.sc = SC_CLASS_B;
        else if (stream_class == "SC_TAS")
            attr.sc = SC_TAS;
        else {
            std::cerr << "Unknown stream-class! Aborting..." << std::endl;
            return -1;
        }
    }

    // When we allow for changing the streamID, we should also adapt the
    // destination group somewhat.
    // Updating the entire address is overkill, but at least avoid basic
    // collisions when we run multiple talkers on the same network.
    attr.dst[5]= ((uint8_t *)&attr.stream_id)[0];

    tx = new netchan::NetChanTx(nh, &attr);
    tx->dump_state();
    tx->ready_wait();


    // Enable pt for CBS classes
    struct periodic_timer *pt = attr.sc == SC_TAS ? NULL : pt_init_from_attr(&attr);

    uint64_t ts = 0;
    running = true;
    signal(SIGINT, sighandler);
    printf("Preparing to send %d values\n", loops);
    uint64_t ts_start = tai_get_ns();
    int ctr = 0;
    while (--loops >= 0 && running) {
        ts = tai_get_ns();
        if (!tx->send(&ts)){
            printf("send FAILED\n");
            break;
        }
        ctr++;
        if (pt)
            pt_next_cycle(pt);
    }
    uint64_t ts_end = tai_get_ns();
    uint64_t diff_ns = ts_end - ts_start;

    printf("Loop stopped, signalling other end (%d, %s)\n", loops, running ? "true" : "false");
    printf("%d frames sent, %.3f ms, rate=%.3f frames/sec\n",
           ctr, (double)diff_ns / 1e6, (double)ctr / ((double)diff_ns / 1e9));
    ts = -1;
    tx->send(&ts);

    tx->stop();
    delete tx;
    return 0;
}
