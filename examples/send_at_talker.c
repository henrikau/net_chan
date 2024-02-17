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
	struct nethandler *nh = nh_create_init(argv[1], 17, "sendat_tx.csv");
	struct channel *ch = chan_create_tx(nh, &nc_channels[IDX_42]);
	nh_enable_ftrace(nh);

	if (!ch)
		return -1;
	struct sensor s = {
		.val = 0xdeadbeef,
		.seqnr = 1337,
		.tx_offset = 500 * NS_IN_MS,
	};

	/* Initalize with 1 Hz*/
	struct periodic_timer *pt = pt_init(0, NS_IN_SEC, CLOCK_TAI);

	const int limit = 10;
	for (int i = 0; i < limit; i++) {
		uint64_t ts = tai_get_ns();
		s.ts = ts;
		s.seqnr++;

		chan_update(ch, ts, &s);

		/* Send frame at now + offset */
		ts += s.tx_offset;
		chan_send(ch, &ts);

		printf("[%03d] ts=%lu, tx_ts=%lu, actual_tx_ts=%lu diff=%.3f us\n",
			limit - i,
			s.ts,		     /* sample time */
			s.ts + s.tx_offset,  /* planned tx */
			ts,		     /* actual tx */
			(double)(ts - (s.ts + s.tx_offset))/1e3);
		pt_next_cycle(pt);
	}
	s.seqnr = -1;
	s.val = -1;
	chan_send_now(ch, &s);

	nh_destroy(&nh);
	return 0;
}
