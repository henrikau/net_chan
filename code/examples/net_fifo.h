#pragma once
#include <timedc_avtp.h>

struct net_fifo net_fifo_chans[] = {
	{
		.dst       = DEFAULT_MCAST,
		.stream_id = 42,
		.size      =  8,
		.freq      = 50,
		.name      = "mcast42"
	},
	{
		.dst       = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01},
		.stream_id = 43,
		.size      = 8,
		.freq      = 10,
		.name      = "mcast43"
	},
	{
		.dst       = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x03},
		.stream_id = 44,
		.size      = 8,
		.freq      = 10,
		.name      = "my-fifo"
	},
	{
		.dst       = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
		.stream_id = 1337,
		.size      = 8,
		.freq      = 10,
		.name      = "bcast",
	},
	{

		.dst       = {0x24, 0x5e,0xbe, 0x54, 0x2e, 0x23},
		.stream_id = 31337,
		.size      = 8,
		.freq      = 100, /* every 10ms */
		.name	   = "uni100",
	},
};
