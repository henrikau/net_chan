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
	NETFIFO_RX(mcast42);
	usleep(10000);
	running = 1;
	signal(SIGINT, sighandler);

	int64_t d = 0;
	while (running && d >= 0) {
		READ_WAIT(mcast42, &d);

		if (d == -1) {
			printf("Magic marker received, stopping\n");
			running = false;
			continue;
		}

		if (!(d%10)) {
			printf(".");
			fflush(stdout);
		}
		if (!(d%200)) {
			printf("%lu\n", d);
			fflush(stdout);
		}
	}

	printf("%s(): terminating loop (d=%ld,running=%s)\n", __func__, d, running ? "true" : "false");
	CLEANUP();
	return 0;
}
