#include <stdio.h>
#include "unity.h"
#include "helper.h"
#include "test_net_fifo.h"
#include <netchan.h>
/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/netchan.c"
#include "../src/netchan_socket.c"

void tearDown(void)
{
	if (_nh)
		nh_destroy(&_nh);
}

static void test_arr_size(void)
{
	TEST_ASSERT(ARRAY_SIZE(nc_channels) == 5);
}

static void test_arr_idx(void)
{
	TEST_ASSERT(nc_get_chan_idx("test1", nc_channels, ARRAY_SIZE(nc_channels)) == 0);
	TEST_ASSERT(nc_get_chan_idx("test2", nc_channels, nfc_sz) == 1);
	TEST_ASSERT(nc_get_chan_idx("missing", nc_channels, nfc_sz) == -1);

	TEST_ASSERT(NC_CHAN_IDX("test2") == 1);
	TEST_ASSERT(NC_CHAN_IDX("missing") == -1);

	TEST_ASSERT_NULL_MESSAGE(NULL, "Should be null");
	struct channel_attrs *nf = NC_GET("missing");
	TEST_ASSERT_NULL_MESSAGE(nf, "nf should be null when name of channel_attrs is not available");
	nf = NC_GET("test1");
	TEST_ASSERT_NOT_NULL_MESSAGE(nf, "nf should *not* be null when name ('test1') of channel_attrs is available");
	TEST_ASSERT_MESSAGE(strncmp(nf->name, "test1", 5) == 0, "wrong channel_attrs returned");
	TEST_ASSERT_MESSAGE(nf->stream_id == 42, "wrong channel_attrs returned");
	TEST_ASSERT_MESSAGE(nf->size == 8, "wrong channel_attrs returned");
	TEST_ASSERT_MESSAGE(nf->interval_ns == INT_50HZ, "wrong channel_attrs returned");
	TEST_ASSERT_MESSAGE(nf->dst[2] == 0x5e, "wrong channel_attrs returned");
	TEST_ASSERT_MESSAGE(nf != &(nc_channels[0]), "Expected copy to be returned, not ref");
	if (nf)
		free(nf);
}

static void test_arr_get_ref(void)
{
	TEST_ASSERT_NULL_MESSAGE(NC_GET_REF("missing"), "should not get ref to missing channel_attrs");

	const struct channel_attrs *nf = NC_GET_REF("test1");
	TEST_ASSERT_NOT_NULL_MESSAGE(nf, "nf should *not* be null when name ('test1') of channel_attrs is available");
	TEST_ASSERT_MESSAGE(nf == &(nc_channels[0]), "Expected ref, not copy!");

	TEST_ASSERT_MESSAGE(strncmp(nf->name, "test1", 5) == 0, "wrong channel_attrs returned");
	TEST_ASSERT_MESSAGE(nf->stream_id == 42, "wrong channel_attrs returned");
	TEST_ASSERT_MESSAGE(nf->size == 8, "wrong channel_attrs returned");
	TEST_ASSERT_MESSAGE(nf->interval_ns == INT_50HZ, "wrong channel_attrs returned");
	TEST_ASSERT_MESSAGE(nf->dst[2] == 0x5e, "wrong channel_attrs returned");
}

struct tg_container {
	bool received_ok;
	int nc_idx;
	unsigned char *expected_data;
	struct channel *source_du;
	char buffer[2048];
};

static void test_create_netfifo_rx_pipe_ok(void)
{
	/* NETCHAN_RX() tested in test_pdu */
	int r = nc_rx_create("missing", nc_channels, nfc_sz);
	TEST_ASSERT(r==-1);
	r = nc_rx_create("test1", nc_channels, nfc_sz);
	TEST_ASSERT(r>=0);

	uint64_t data = 0xdeadbeef;
	write(_nh->du_rx_tail->fd_w, &data, 8);

	uint64_t res = 0;
	read(_nh->du_rx_tail->fd_r, &res, 8);
	TEST_ASSERT(data == res);
}

static void test_create_netfifo_rx_send_ok(void)
{
	/* Create listening socket and pipe-pair */
	int r = nc_rx_create("test1", nc_channels, nfc_sz);
	TEST_ASSERT(r > 0);

	uint64_t data = 0xdeadbeef;
	int txsz = helper_send_8byte(0, data);
	TEST_ASSERT(txsz == 8);

	struct pipe_meta *pm = malloc(sizeof(struct pipe_meta) + sizeof(uint64_t));
	read(r, pm, sizeof(*pm) + sizeof(uint64_t));
	uint64_t *received = (uint64_t *)&pm->payload[0];
	TEST_ASSERT(*received == data);
	free(pm);
}

static void test_create_netfifo_rx_recv(void)
{
	int rxchan = nc_rx_create("test2", nc_channels, nfc_sz);
	TEST_ASSERT(rxchan != -1);

	uint64_t data = 0x0a0a0a0a;
	int txsz = helper_send_8byte(1, data);
	TEST_ASSERT(txsz == 8);


	struct pipe_meta *pm = malloc(sizeof(struct pipe_meta) + sizeof(uint64_t));
	int rsz = read(rxchan, pm, sizeof(*pm) + sizeof(uint64_t));
	uint64_t *r = (uint64_t *)&pm->payload[0];
	TEST_ASSERT(rsz == (sizeof(struct pipe_meta) + sizeof(uint64_t)));
	TEST_ASSERT(*r == data);

	free(pm);
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	nc_set_nic("lo");

	RUN_TEST(test_arr_size);
	RUN_TEST(test_arr_idx);
	RUN_TEST(test_arr_get_ref);
	RUN_TEST(test_create_netfifo_rx_pipe_ok);
	RUN_TEST(test_create_netfifo_rx_send_ok);
	RUN_TEST(test_create_netfifo_rx_recv);

	return UNITY_END();
}
