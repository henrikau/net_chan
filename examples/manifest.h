/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <netchan.h>
struct sensor {
	uint64_t val;
	uint64_t seqnr;
	uint64_t ts;
	uint64_t tx_offset;
};

enum attr_idx {
	IDX_42,
	IDX_154,
	IDX_17,
	IDX_18,
};


#define HZ_500   (2 * NS_IN_MS)
#define HZ_100  (10 * NS_IN_MS)
#define HZ_50   (20 * NS_IN_MS)
#define HZ_10  (100 * NS_IN_MS)

struct channel_attrs nc_channels[] = {
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x42},
		.stream_id = 42,
		.sc	   = CLASS_A,
		.size      =  sizeof(struct sensor),
		.interval_ns = 5000000L, /* 200 Hz */
#ifndef __cplusplus
		.name      = "mcast42",
#endif
	},
	{
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x9A},
		.stream_id = 154,
		.sc	   = CLASS_A,
		.size      =  sizeof(struct sensor),
		.interval_ns = (200 * NS_IN_MS), /* 5 Hz */
#ifndef __cplusplus
		.name      = "mcast154",
#endif
	},
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x17},
		.stream_id = 17,
		.sc	   = CLASS_A,
		.size      =  sizeof(uint64_t),
		.interval_ns  = HZ_100,
#ifndef __cplusplus
		.name      = "mcast17"
#endif
	},
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x18},
		.stream_id = 18,
		.sc	   = CLASS_A,
		.size      =  sizeof(uint64_t),
		.interval_ns  = HZ_100,
#ifndef __cplusplus
		.name      = "mcast18"
#endif
	}
};

#ifdef __cplusplus
}
#endif
