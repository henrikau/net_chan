#pragma once
#include <timedc_avtp.h>
/* nethandler stuff */
char nf_nic[] = "eth0";
int nf_hmap_size = 42;

/* channels */

struct net_fifo net_fifo_chans[] = {
	{
		{0x01, 0x00, 0x5E, 0x00, 0x00, 0x00},
		42,
		8,
		50,
		"test42"
	},
	{
		{0x01, 0x00, 0x5E, 0x00, 0x00, 0x01},
		43,
		8,
		10,
		"test43"
	},
	{
		{0x01, 0x00, 0x5E, 0x00, 0x00, 0x03},
		44,
		8,
		10,
		"my-fifo"
	},
};
