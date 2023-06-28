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

	TEST_ASSERT_NOT_NULL_MESSAGE(ch, "Channel not created with valid arguments");
	TEST_ASSERT_NOT_NULL_MESSAGE(ch->cbp, "Generic callback not created for Rx channel");
}

/* Gradually assemble and channel and make sure chan_valid() triggers on
 * missing values.
 */
static void test_chan_valid(void)
{
	TEST_ASSERT(!chan_valid(NULL));
	struct channel ch = {0};
	TEST_ASSERT(!chan_valid(&ch));
	ch.nh = nh;
	TEST_ASSERT(!chan_valid(&ch));
	ch.tx_sock = 7;
	TEST_ASSERT(!chan_valid(&ch));

	/* StreamID but also PDU's streamID */
	ch.sidw.s64 = 17;
	ch.pdu.stream_id = 17;

	TEST_ASSERT(!chan_valid(&ch));
	ch.payload_size = 8;
	TEST_ASSERT(!chan_valid(&ch));
	ch.fd_w = 1;
	ch.fd_r = 2;
	TEST_ASSERT(!chan_valid(&ch));

	ch.interval_ns = 12345;
	TEST_ASSERT(chan_valid(&ch));

	/* If not Tx, then it must be Rx, and then cbp must point to something */
	ch.tx_sock = 0;
	TEST_ASSERT(!chan_valid(&ch));

	char buffer[128] = {0};
	ch.cbp = (struct cb_priv *)buffer;

	TEST_ASSERT(chan_valid(&ch));
	/* Tx-sock should not have callback memory set */
	ch.tx_sock = 1;
	TEST_ASSERT(!chan_valid(&ch));
	ch.cbp = NULL;
	TEST_ASSERT(chan_valid(&ch));

	struct channel *ch2 = chan_create_tx(nh, &chanattr);
	TEST_ASSERT(chan_valid(ch2));
}

static void test_chan_time_to_tx(void)
{
	TEST_ASSERT_EQUAL_UINT64_MESSAGE(UINT64_MAX, chan_time_to_tx(NULL),
					"Invalid channel should result in MAXINT");

	struct channel ch = {0};
	TEST_ASSERT_EQUAL_UINT64_MESSAGE(UINT64_MAX, chan_time_to_tx(&ch),
					"Invalid channel should result in MAXINT");

	struct channel *ch2 = chan_create_tx(nh, &chanattr);
	uint64_t now = tai_get_ns();
	ch2->interval_ns = 10 * NS_IN_MS;
	ch2->next_tx_ns = now + 9*NS_IN_SEC;

	/* Check interval */
	TEST_ASSERT_UINT64_WITHIN_MESSAGE(NS_IN_MS, 9*NS_IN_SEC, chan_time_to_tx(ch2),
					"Unexpected time to Tx; should be close to 9 sec in the future");

	/* Move time backwards, time to tx should be *now*, i.e. 0 */
	ch2->next_tx_ns = now - NS_IN_SEC;
	TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, chan_time_to_tx(ch2),
					"next_tx_ns in the past should result in Tx-time to *now* (0)");
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_create_tx_channel);
	RUN_TEST(test_create_rx_channel);
	RUN_TEST(test_chan_valid);
	RUN_TEST(test_chan_time_to_tx);
	return UNITY_END();
}
