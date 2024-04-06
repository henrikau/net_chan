#include <stdio.h>
#include "unity.h"
#include "test_net_fifo.h"

struct nethandler *nh;
struct channel_attrs chanattr = {
	.dst       = DEFAULT_MCAST,
	.stream_id = 42,
	.sc        = SC_TAS,
	.size      = 8,
	.interval_ns      = INT_50HZ,
	.name      = "tastest"};

void setUp(void)
{
	nh = nh_create_init("lo", 16, NULL);
}

void tearDown(void)
{
	if (nh)
		nh_destroy(&nh);
}

static void test_create_tas(void)
{
	struct channel *ch = chan_create_tx(nh, &chanattr);
	TEST_ASSERT(ch != NULL);
	TEST_ASSERT(ch->sc == SC_TAS);
	TEST_ASSERT(ch->pcp_prio = DEFAULT_CLASS_TAS_PRIO);
}
int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_create_tas);
	return UNITY_END();
}
