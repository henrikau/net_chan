#pragma once
#include <timedc_avtp.h>

struct net_fifo net_fifo_chans[] = {
	{
		.dst       = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x00},
		.stream_id = 42,
		.class     = CLASS_A,
		.size      = 8,
		.freq      = 50,
		.name      = "test1"
	},
	{
		.dst       = {0x01, 0x00, 0x5E, 0xde, 0xad, 0x42},
		.stream_id = 43,
		.class     = CLASS_A,
		.size      = 8,
		.freq      = 10,
		.name      = "test2"
	},
	{
		.dst       = {0x01, 0x00, 0x5E, 0x01, 0x02, 0x03},
		.stream_id = 11,
		.class     = CLASS_B,
		.size      = 8,
		.freq      = 10,
		.name      = "macro11"

	}
};
int nfc_sz = ARRAY_SIZE(net_fifo_chans);
