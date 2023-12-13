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
#include <thread>

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

static int rx_ctr = 0;
void async_rx_ctr(void)
{
    struct timespec ts_cpu;
    if (clock_gettime(CLOCK_REALTIME, &ts_cpu) == -1) {
        std::cerr << "Failed getting timespec, aborting thread::async_rx_ctr!" << std::endl;
        return;
    }
    uint64_t start_ts_ns = ts_cpu.tv_nsec + ts_cpu.tv_sec*1000000000;
    int start = rx_ctr;
    int last_ctr = rx_ctr;
    printf("Starting async_rx_ctr() as own thread, running=%d\n", running);
    while (running) {
        ts_cpu.tv_sec++;
        if (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts_cpu, NULL) == -1) {
            std::cerr << "Aborting thread::async_rx_ctr" << std::endl;
            return;
        }
        int diff = rx_ctr - last_ctr;
        last_ctr = rx_ctr;
        printf("\r[%08d] Rate: %5d/sec", rx_ctr, diff);
        fflush(stdout);
    }
}


int main(int argc, char *argv[])
{
    std::string logfile;

    po::options_description desc("Talker options");
    desc.add_options()
        ("help,h", "Show help")
        ("verbose,v", "Increase logging output")
        ("interface,i", po::value<std::string>(&nic), "Change network interface")
        ("log,L", po::value<std::string>(&logfile), "Log to file")
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

    netchan::NetHandler nh(nic, logfile, use_srp);
    if (vm.count("verbose"))
        nh.verbose();

    rx = new netchan::NetChanRx(nh, &nc_channels[IDX_17]);
    tx = new netchan::NetChanTx(nh, &nc_channels[IDX_18]);

    uint64_t recv_ts = 0;
    running = true;
    signal(SIGINT, sighandler);

    std::thread th_rate { [&] { async_rx_ctr(); }};
    uint64_t start, end;
    while (running) {
        if (rx->read(&recv_ts)) {
            if (rx_ctr == 0)
                start = tai_get_ns();
            rx_ctr++;
            if (recv_ts == -1) {
                printf("Received stop-signal\n");
                running = false;
                continue;
            }

            uint64_t rx_ts = tai_get_ns();
            if (!tx->send_wait(&recv_ts))
                break;
        } else {
            printf("Read failed\n");
            running = false;
        }
    }
    end = tai_get_ns();

    running = false;
    th_rate.join();
    // Signal other end that we're closing down
    recv_ts = -1;
    tx->send(&recv_ts);

    rx->stop();
    tx->stop();
    delete rx;
    delete tx;

    printf("Run complete, received %d frames in %f secs\n",
           rx_ctr, (double)(end-start)/1e9);
    return 0;
}
