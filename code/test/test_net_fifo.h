#pragma once
#include <timedc_avtp.h>

struct net_fifo net_fifo_chans[] = {
	{
		{0x01, 0x00, 0x5E, 0x00, 0x00, 0x00},
		42,
		8,
		50,
		"test1"
	},
	{
		{0x01, 0x00, 0x5E, 0xde, 0xad, 0x42},
		43,
		8,
		10,
		"test2"
	},
};
unsigned char nf_nic[] = "lo";
int nf_hmap_size = 41;

int nfc_sz = ARRAY_SIZE(net_fifo_chans);
