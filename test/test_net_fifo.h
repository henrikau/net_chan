#pragma once
#include <netchan_standalone.h>
#define INT_10HZ 100000000L
#define INT_50HZ  20000000L

enum fifo_arr {
	MCAST42 = 0,
	MCAST43 = 1,
	MCAST11 = 2,
	MCAST17 = 3,
	PDU43_R = 4,
};

#define DATA17SZ 32
#define INT17 INT_10HZ

struct channel_attrs nc_channels[] = {
	{			/* 0 */
		.dst       = DEFAULT_MCAST,
		.stream_id = 42,
		.sc        = SC_CLASS_A,
		.size      = 8,
		.interval_ns      = INT_50HZ,
		.name      = "test1"
	},
	{			/* 1 */
		.dst       = {0x01, 0x00, 0x5E, 0xde, 0xad, 0x42},
		.stream_id = 43,
		.sc        = SC_CLASS_A,
		.size      = 8,
		.interval_ns = INT_10HZ,
		.name      = "test2"
	},
	{			/* 2 */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x02, 0x03},
		.stream_id = 11,
		.sc        = SC_CLASS_B,
		.size      = 8,
		.interval_ns = INT_10HZ,
		.name      = "macro11"
	},
	{			/* 3 */
		.dst       = {0x01, 0x00, 0xe5, 0x01, 0x02, 0x42},
		.stream_id = 17,
		.sc        = SC_CLASS_A,
		.size      = DATA17SZ,
		.interval_ns = INT17,
		.name      = "test2",
	},
	{			/* 4 */
		.dst       = DEFAULT_MCAST,
		.stream_id = 43,
		.sc        = SC_CLASS_A,
		.size      = 8,
		.interval_ns      = INT_50HZ,
		.name      = "pdu43_r"
	},

};

int nfc_sz = ARRAY_SIZE(nc_channels);
