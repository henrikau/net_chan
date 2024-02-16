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
#include <netchan_standalone.h>
#include <netchan_args.h>

#include "manifest.h"

int main(int argc, char *argv[])
{
	GET_ARGS();
	struct channel *txa = chan_create_standalone("mcast42", true, nc_channels, ARRAY_SIZE(nc_channels));
	struct channel *txb = chan_create_standalone("mcast154", true, nc_channels, ARRAY_SIZE(nc_channels));
	if (!txa || !txb)
		return -1;

	chan_dump_state(txa);
	printf("\n");
	chan_dump_state(txb);

	/* Schedule one frame to be sent, clear out NS (make it easier
	 * to spot the timestamps in tcpdump later
	 */
	uint64_t now = tai_get_ns();
	now -= now % 1000000000;
	now += 1000000000;


	struct sensor a = {
		.seqnr = 1337,
		.val = 0xaaaaaaaaaaaaaaaa,
	};
	uint64_t now_a = now;

	struct sensor b = {
		.seqnr = 42,
		.val = 0xbbbbbbbbbbbbbbbb,
	};
	uint64_t now_b = now + 500 * NS_IN_MS;

	chan_update(txa, now_a, &a);
	chan_update(txb, now_b, &b);

	chan_send(txb, &now_b);
	chan_send(txa, &now_a);
#if 0
	running = 1;
	signal(SIGINT, sighandler);

	while (running && a.seqnr < LIMIT && b.seqnr < LIMIT)
	{
		a.seqnr++;
		b.seqnr++;
		uint64_t now = tai_get_ns();
		uint64_t now_a = now;
		uint64_t now_b = now + 500 * NS_IN_US;

		chan_update(ch, now + 500 * NS_IN_US, &b);
		int res = chan_send(ch, &now_b);
		if (res < 0) {
			printf("Failed sending B, res=%d\n", res);
		} else {
			printf("Sent B,  Target: %lu, actual: %lu diff: %lu\n",
				now + 500*NS_IN_US, now_b, now_b - (now + 500*NS_IN_US));
		}

		chan_update(ch, now, &a);
		res = chan_send(ch, &now_a);
		if (res < 0) {
			printf("Failed sending A, res=%d\n", res);
		} else {
			printf("Sent A,  Target: %lu, actual: %lu diff: %lu\n",
				now, now_b, now_b-now);
		}

		// wait for next period (we sent 2)
		wait_for_tx_slot(ch);
		wait_for_tx_slot(ch);
	}

	a.seqnr = -1;
	chan_send_now(ch, &a);
	CLEANUP();

	return 0;
#endif
}
