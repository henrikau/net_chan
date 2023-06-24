#include <stdio.h>
#include "unity.h"
#include "test_net_fifo.h"

/*
 * Test of external channel interface
 */
struct nethandler *nh;
struct net_fifo chanattr = {
	.dst       = DEFAULT_MCAST,
	.stream_id = 42,
	.sc        = CLASS_A,
	.size      = 8,
	.interval_ns      = INT_50HZ,
	.name      = "test1"};

void setUp(void)
{
	nh = nh_create_init("lo", 16, NULL);
}

void tearDown(void)
{
	if (nh)
		nh_destroy(&nh);
}

static void test_create_tx_channel(void)
{
	TEST_ASSERT_NULL(chan_create_tx(NULL, &chanattr));
	TEST_ASSERT_NULL(chan_create_tx(nh, NULL));
	struct channel *ch = chan_create_tx(nh, &chanattr);
	TEST_ASSERT_NOT_NULL(ch);
	TEST_ASSERT(ch->nh == nh);
	TEST_ASSERT(ch->sidw.s64 == 42);
	TEST_ASSERT(ch->fd_r > 0);
	TEST_ASSERT(ch->fd_w > 0);

	TEST_ASSERT_MESSAGE(ch->tx_sock_prio == 3, "Invalid socket-prio.");
	TEST_ASSERT_MESSAGE(ch->tx_sock > 0, "Invalid socket for Tx-channel.");
}

static void test_create_rx_channel(void)
{
	nc_verbose();
	TEST_ASSERT_NULL(chan_create_rx(NULL, &chanattr));
	TEST_ASSERT_NULL(chan_create_rx(nh, NULL));
	struct channel *ch = chan_create_rx(nh, &chanattr);

	/* TEST_ASSERT_NOT_NULL_MESSAGE(ch, "Channel not created with valid arguments"); */
	/* TEST_ASSERT_NOT_NULL_MESSAGE(ch->cbp, "Generic callback not created for Rx channel"); */
}


int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_create_tx_channel);
	RUN_TEST(test_create_rx_channel);

	return UNITY_END();
}
