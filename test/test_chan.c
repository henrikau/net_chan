#include <stdio.h>
#include "unity.h"
#include "test_net_fifo.h"

/*
 * Test of external channel interface
 */
struct nethandler *nh;

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
	struct net_fifo chanattr = {
		.dst       = DEFAULT_MCAST,
		.stream_id = 42,
		.sc        = CLASS_A,
		.size      = 8,
		.interval_ns      = INT_50HZ,
		.name      = "test1"};
	TEST_ASSERT_NULL(chan_create_tx(NULL, &chanattr));
	TEST_ASSERT_NULL(chan_create_tx(nh, NULL));
	struct channel *ch = chan_create_tx(nh, &chanattr);
	TEST_ASSERT_NOT_NULL(ch);
	TEST_ASSERT(ch->nh == nh);
	TEST_ASSERT(ch->sidw.s64 == 42);
	TEST_ASSERT_EQUAL_STRING("test1", ch->name);
	TEST_ASSERT(ch->fd_r > 0);
	TEST_ASSERT(ch->fd_w > 0);

	TEST_ASSERT_MESSAGE(ch->tx_sock_prio == 3, "Invalid socket-prio.");
	TEST_ASSERT_MESSAGE(ch->tx_sock > 0, "Invalid socket for Tx-channel.");
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_create_tx_channel);

	return UNITY_END();
}
