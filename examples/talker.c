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
#include <netchan_standalone.h>
#include <netchan_args.h>

#include "manifest.h"
#define LIMIT 5000

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
	struct channel *ch = chan_create_standalone("mcast42", true, nc_channels, ARRAY_SIZE(nc_channels));
	if (!ch)
		return -1;

	chan_dump_state(ch);

	running = 1;
	signal(SIGINT, sighandler);

	struct sensor s = {0};
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);

	for (; s.seqnr < LIMIT && running; s.seqnr++) {
		// wait for period
		wait_for_tx_slot(ch);
		s.val = 0xdeadbeef;
		int res = chan_send_now(ch, &s);
		if (res < 0) {
			printf("%s(): Send failed\n", __func__);
			running = false;
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &end);
	int64_t start_ns = start.tv_sec * 1000000000 + start.tv_nsec;
	int64_t end_ns = end.tv_sec * 1000000000 + end.tv_nsec;
	int64_t diff_ns = end_ns - start_ns;
	printf("Loop ran for %.6f ms\n", (double)diff_ns / 1e6);

	printf("Sent %ld packets out of %d\n", s.seqnr + 1, LIMIT);

	printf("\n");
	printf("%s() Attempting to stop remote, sending magic marker\n", __func__);

	s.seqnr = -1;
	chan_send_now(ch, &s);
	usleep(10000);
	chan_send_now(ch, &s);
	CLEANUP();

	return 0;
}
