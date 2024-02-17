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
	if (argc != 2) {
		printf("%s: Need iface as argv[1]\n", argv[0]);
		return -1;
	}
	struct nethandler *nh = nh_create_init(argv[1], 17, "sendat_rx.csv");
	struct channel *ch = chan_create_rx(nh, &nc_channels[IDX_42]);
	nh_enable_ftrace(nh);
	if (!ch)
		return -1;

	struct sensor s = {0};
	bool running = true;

	while (running) {
		chan_read(ch, &s);
		uint64_t ts = tai_get_ns();
		if (s.seqnr == -1)
			break;
		uint64_t sample_ts = s.ts;
		uint64_t planned_tx = s.ts + s.tx_offset;

		/* Print out Rx time and diff from s.ts (which should be 500 ms + trans delay) */
		printf("Round %03lu\n", s.seqnr);
		printf("Sample  ts: %lu (%.3f ms ago)\n", sample_ts, (double)(ts - sample_ts)/1e6);
		printf("Planned tx: %lu\n", planned_tx);
		printf("        rx: %lu\n", ts);
		printf("       E2E: %23.3f us\n\n", ((double)ts - (double)planned_tx)/1e3);
	}

	nh_destroy(&nh);
	return 0;
}
