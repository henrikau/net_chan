/*
 * Copyright 2023 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 *
 * This implements a *one-way* listener
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

#include <boost/program_options.hpp>
namespace po = boost::program_options;

void sighandler(int signum)
{
	printf("%s(): Got signal (%d), closing\n", __func__, signum);
	fflush(stdout);
	running = false;
        rx->stop();
}


/*
 * Very rough rate estimator.
 *
 * Run every 1sec and count received packets. Only used to give an
 * indication that the listener is actually working and that the
 * received rate is as expected.
 */
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
    struct channel_attrs attr = nc_channels[IDX_17];

    po::options_description desc("Talker options");
    desc.add_options()
        ("help,h", "Show help")
        ("verbose,v", "Increase logging output")
        ("interface,i", po::value<std::string>(&nic), "Change network interface")
        ("log,L", po::value<std::string>(&logfile), "Log to file")
        ("stream_id", po::value<uint64_t>(&attr.stream_id), "Use different StreamID (base10)")
        ("use_srp,S", "Enable SRP")
         ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    bool use_srp = vm.count("use_srp");

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        exit(0);
    }
    std::cout << "Using NIC " << nic << std::endl;

    netchan::NetHandler nh(nic, logfile, use_srp);
    if (vm.count("verbose"))
        nh.verbose();

    // When we allow for changing the streamID, we should also adapt the
    // destination group somewhat.
    // Updating the entire address is overkill, but at least avoid basic
    // collisions when we run multiple talkers on the same network.
    attr.dst[5]= ((uint8_t *)&attr.stream_id)[0];

    rx = new netchan::NetChanRx(nh, &attr);
    rx->ready_wait();

    uint64_t recv_ts = 0;
    running = true;
    signal(SIGINT, sighandler);

    // Rx rate estimator
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

        }
    }
    end = tai_get_ns();
    running = false;

    // wait for async_ctr
    th_rate.join();

    rx->stop();

    printf("Run complete, received %d frames in %f secs\n",
           rx_ctr, (double)(end-start)/1e9);

    delete rx;
    return 0;
}
