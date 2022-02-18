#pragma once
#include <timedc_avtp.h>
struct net_fifo net_fifo_chans[] = {
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x42},
		.stream_id = 42,
		.class	   = CLASS_A,
		.size      =  8,
		.freq      = 50,
		.name      = "mcast42",
	},
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x17},
		.stream_id = 17,
		.class	   = CLASS_A,
		.size      =  8,
		.freq      = 10,
		.name      = "mcast17"
	},
	{
		/* DEFAULT_MCAST */
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x11, 0x18},
		.stream_id = 18,
		.class	   = CLASS_A,
		.size      =  8,
		.freq      = 10,
		.name      = "mcast18"
	}
};
