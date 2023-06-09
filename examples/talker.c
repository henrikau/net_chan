/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <netchan.h>
#include <timedc_args.h>

#include "manifest.h"
static int running = 0;
void sighandler(int signum)
{
	printf("%s(): Got signal (%d), closing\n", __func__, signum);
	fflush(stdout);
	running = 0;
}

int main(int argc, char *argv[])
{
	GET_ARGS();

	NETCHAN_TX(mcast42);
	usleep(10000);
	running = 1;

	signal(SIGINT, sighandler);
	int freq = 1e9 / net_fifo_chans[0].interval_ns;

	for (int64_t i = 0; i < (freq*60*60*6) && running; i++) {

		struct timespec ts_cpu = {0};
		if (clock_gettime(CLOCK_MONOTONIC, &ts_cpu) == -1) {
			fprintf(stderr, "%s() FAILED (%d, %s)\n", __func__, errno, strerror(errno));
			return -1;
		}
		ts_cpu.tv_nsec += net_fifo_chans[0].interval_ns;
		ts_normalize(&ts_cpu);

		WRITE_WAIT(mcast42, &i);
		if (!(i%50))
			printf("%lu: written\n", i);

		if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_cpu, NULL) == -1)
			printf("%s() clock_nanosleep() failed: %s\n", __func__, strerror(errno));
	}

	int64_t marker = -1;
	printf("%s() Attempting to stop remote, sending magic marker\n", __func__);
	WRITE_WAIT(mcast42, &marker);
	usleep(10000);
	printf("%s() Attempting to stop remote, sending magic marker\n", __func__);
	WRITE_WAIT(mcast42, &marker);
	CLEANUP();
	return 0;
}
