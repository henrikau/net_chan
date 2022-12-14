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
	for (int64_t i = 0; i < (50*60*60*6) && running; i++) {
		WRITE_WAIT(mcast42, &i);
		if (!(i%50))
			printf("%lu: written\n", i);

		/* Take freq and subtract expected wait from class to
		 * get roughly right delay
		 */
		int udel = 1000000 / net_fifo_chans[0].freq - (net_fifo_chans[0].sc == CLASS_A ? 2*US_IN_MS : 50*US_IN_MS);
		usleep(udel);
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
