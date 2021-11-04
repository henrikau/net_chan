#include <stdio.h>
#include "unity.h"
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
unsigned char nf_nic[] = "eth0";
int nf_hmap_size = 41;

/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/timedc_avtp.c"

static int nfc_sz = ARRAY_SIZE(net_fifo_chans);

static void test_arr_size(void)
{
	TEST_ASSERT(ARRAY_SIZE(net_fifo_chans) == 2);
	//TEST_ASSERT(ARRAY_SIZE(NULL) == -1);
}

static void test_arr_idx(void)
{
	TEST_ASSERT(get_chan_idx("test1", net_fifo_chans, ARRAY_SIZE(net_fifo_chans)) == 0);
	TEST_ASSERT(get_chan_idx("test2", net_fifo_chans, nfc_sz) == 1);
	TEST_ASSERT(get_chan_idx("missing", net_fifo_chans, nfc_sz) == -1);
}

static void test_create_standalone(void)
{

	TEST_ASSERT(pdu_create_standalone(NULL, false, net_fifo_chans, nfc_sz, nf_nic, nf_hmap_size) == NULL);
	TEST_ASSERT(pdu_create_standalone("missing", false, net_fifo_chans, nfc_sz, nf_nic, nf_hmap_size) == NULL);
	TEST_ASSERT(NETFIFO_RX("missing") == NULL);

	struct timedc_avtp *pdu;
	pdu = pdu_create_standalone("test1", false, net_fifo_chans, nfc_sz, nf_nic, nf_hmap_size);
	TEST_ASSERT(pdu != NULL);
	pdu_destroy(&pdu);

	pdu = pdu_create_standalone("test2", false, net_fifo_chans, nfc_sz, nf_nic, nf_hmap_size);
	TEST_ASSERT(pdu != NULL);
	pdu_destroy(&pdu);

	pdu = NETFIFO_RX("test1");
	TEST_ASSERT(pdu != NULL);

	/* Test pdu internals after macro creation */
	TEST_ASSERT(pdu->pdu.stream_id == 42);
	for (int i = 0; i < ETH_ALEN; i++)
		TEST_ASSERT(pdu->dst[i] == net_fifo_chans[0].mcast[i]);
	TEST_ASSERT(pdu->nh == _nh);

	pdu_destroy(&pdu);
	nh_destroy(&_nh);	/* remember to destroy _nh when using standalone */
 }

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_arr_size);
	RUN_TEST(test_arr_idx);
	RUN_TEST(test_create_standalone);
	return UNITY_END();
}
