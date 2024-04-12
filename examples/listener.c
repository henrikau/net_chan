/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <netchan_standalone.h>
#include <netchan_args.h>

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
	NETCHAN_RX(mcast42);
	usleep(10000);
	running = 1;
	signal(SIGINT, sighandler);

	struct sensor s = {0};

	struct timespec start, end;
	clock_gettime(CLOCK_REALTIME, &start);
	while (running && s.seqnr >= 0) {
		READ_WAIT(mcast42, &s);

		if (s.seqnr == -1) {
			printf("Magic marker received, stopping\n");
			running = false;
			continue;
		}

		if (!(s.seqnr%10)) {
			printf(".");
			fflush(stdout);
		}
		if (!(s.seqnr%200)) {
			printf("%lu\n", s.seqnr);
			fflush(stdout);
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	int64_t start_ns = start.tv_sec * 1000000000 + start.tv_nsec;
	int64_t end_ns = end.tv_sec * 1000000000 + end.tv_nsec;
	int64_t diff_ns = end_ns - start_ns;

	printf("%s(): terminating loop (d=%ld,running=%s)\n", __func__, s.seqnr, running ? "true" : "false");
	printf("Loop ran for %.6f ms\n", (double)diff_ns / 1e6);

	CLEANUP();
	return 0;
}
