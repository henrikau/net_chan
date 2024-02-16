/*
 * Copyright 2024 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 *
 * A simple talker that purposefully sends frame-pairs in the wrong
 * order.  I.e. Frame A have a txtime *before* frame B, but B is given
 * to netchan before A.
 */
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include <netchan.h>
#include "manifest.h"

int main(int argc, char *argv[])
{
	struct nethandler *nh = nh_create_init("enp6s0.2", 17, "talker.csv");
	struct channel *txa = chan_create_tx(nh, &nc_channels[IDX_42]);
	struct channel *txb = chan_create_tx(nh, &nc_channels[IDX_154]);

	if (!txa || !txb)
		return -1;

	/* Schedule one frame to be sent, clear out NS (make it easier
	 * to spot the timestamps in tcpdump later
	 *
	 * netchan uses CLOCK_TAI
	 */
	uint64_t now = tai_get_ns();
	now -= now % 1000000000;
	now += 1000000000;

	uint64_t now_a = now;
	struct sensor a = {
		.seqnr = 1337,
		.val = 0xaaaaaaaaaaaaaaaa,
		.ts = now_a,
	};

	uint64_t now_b = now + 500 * NS_IN_MS;
	struct sensor b = {
		.seqnr = 42,
		.val = 0xbbbbbbbbbbbbbbbb,
		.ts = now_b,
	};

	chan_update(txa, now_a, &a);
	chan_update(txb, now_b, &b);

	chan_send(txa, &now_a);
	printf("[A] Scheduled to send A at %lu, actually sent: %lu, error: %.3f us\n",
		now, now_a, (double)(now_a - now)/1e3);


	chan_send(txb, &now_b);
	printf("[B] Scheduled to send B at %lu, actually sent: %lu, error: %.3f us\n",
		now + 1500*NS_IN_MS, now_b, (double)(now_b - (now + 500 * NS_IN_MS))/1e3);


	printf("Staying alive to let channels finish tx\n");
	for (int i =0; i < 10; i++) {
		usleep(100000);
		printf("."); fflush(stdout);
	}
	printf("\n");

	nh_rotate_logs(nh);
	nh_destroy(&nh);
	return 0;
}
