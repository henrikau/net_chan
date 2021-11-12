#include <stdio.h>
#include "unity.h"
#include "test_net_fifo.h"
#include <timedc_avtp.h>

/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/timedc_avtp.c"
void tearDown(void)
{
	nh_destroy(&_nh);
}

static void test_arr_size(void)
{
	TEST_ASSERT(ARRAY_SIZE(net_fifo_chans) == 2);
	//TEST_ASSERT(ARRAY_SIZE(NULL) == -1);
}

static void test_arr_idx(void)
{
	TEST_ASSERT(nf_get_chan_idx("test1", net_fifo_chans, ARRAY_SIZE(net_fifo_chans)) == 0);
	TEST_ASSERT(nf_get_chan_idx("test2", net_fifo_chans, nfc_sz) == 1);
	TEST_ASSERT(nf_get_chan_idx("missing", net_fifo_chans, nfc_sz) == -1);

	TEST_ASSERT(NF_CHAN_IDX("test2") == 1);
	TEST_ASSERT(NF_CHAN_IDX("missing") == -1);

	TEST_ASSERT_NULL_MESSAGE(NULL, "Should be null");
	struct net_fifo *nf = NF_GET("missing");
	TEST_ASSERT_NULL_MESSAGE(nf, "nf should be null when name of net_fifo is not available");
	nf = NF_GET("test1");
	TEST_ASSERT_NOT_NULL_MESSAGE(nf, "nf should *not* be null when name ('test1') of net_fifo is available");
	TEST_ASSERT_MESSAGE(strncmp(nf->name, "test1", 5) == 0, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->stream_id == 42, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->size == 8, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->freq == 50, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->mcast[2] == 0x5e, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf != &(net_fifo_chans[0]), "Expected copy to be returned, not ref");
	if (nf)
		free(nf);
}

static void test_arr_get_ref(void)
{
	TEST_ASSERT_NULL_MESSAGE(NF_GET_REF("missing"), "should not get ref to missing net_fifo");

	const struct net_fifo *nf = NF_GET_REF("test1");
	TEST_ASSERT_NOT_NULL_MESSAGE(nf, "nf should *not* be null when name ('test1') of net_fifo is available");
	TEST_ASSERT_MESSAGE(nf == &(net_fifo_chans[0]), "Expected ref, not copy!");

	TEST_ASSERT_MESSAGE(strncmp(nf->name, "test1", 5) == 0, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->stream_id == 42, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->size == 8, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->freq == 50, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->mcast[2] == 0x5e, "wrong net_fifo returned");
}

static void test_create_netfifo_tx(void)
{
	/* Create pipe, datasize 8, streamID 42, first multicast address */
	int w = NETFIFO_TX_CREATE("missing");
	TEST_ASSERT(w == -1);

	w = NETFIFO_TX_CREATE("test1");
	TEST_ASSERT(w != -1);
	uint64_t val = 0xaa00aa00;
	usleep(1000);

	write(w, &val, 8);

	/* Need a 10ms sleep at the end to let the worker thread grab the data */
	usleep(10000);
	TEST_ASSERT(_nh->du_tx_tail->payload_size == 8);
	uint64_t *data = (uint64_t *)&(_nh->du_tx_tail->payload[0]);
	TEST_ASSERT(*data == val);

}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_arr_size);
	RUN_TEST(test_arr_idx);
	RUN_TEST(test_arr_get_ref);

	RUN_TEST(test_create_netfifo_tx);

	return UNITY_END();
}
