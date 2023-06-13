/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#pragma once
#include <netchan.h>
struct sensor {
	uint64_t val;
	uint64_t seqnr;
};

struct net_fifo net_fifo_chans[] = {
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x42},
		.stream_id = 42,
		.sc	   = CLASS_A,
		.size      =  sizeof(struct sensor),
		.interval_ns = 5000000L, /* 200 Hz */
		.name      = "mcast42",
	},
	{
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x9A},
		.stream_id = 154,
		.sc	   = CLASS_A,
		.size      =  sizeof(struct sensor),
		.interval_ns = (200 * NS_IN_MS), /* 5 Hz */
		.name      = "mcast154",
	},
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x17},
		.stream_id = 17,
		.sc	   = CLASS_A,
		.size      =  8,
		.interval_ns  = 100000000L, /* 10 Hz */
		.name      = "mcast17"
	},
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x18},
		.stream_id = 18,
		.sc	   = CLASS_A,
		.size      =  8,
		.interval_ns  = 100000000L, /* 10 Hz */
		.name      = "mcast18"
	}
};
