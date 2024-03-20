#include <stdio.h>
#include <netchan.h>
#include "unity.h"
#include <unistd.h>

/*
 * WARNING!!! WARNING!!! WARNING!!! WARNING!!!
 *
 * include c directly (need access to internals)
 *
 * WARNING!!! WARNING!!! WARNING!!! WARNING!!!
 */
#include "../src/netchan_utils.c"

static struct periodic_timer *pt_tai;
static uint64_t ts_ns = 0;

uint64_t checkpoint(void)
{
	uint64_t ts_now = real_get_ns();
	uint64_t ts_diff = ts_now - ts_ns;
	ts_ns = ts_now;
	return ts_diff;
}

void setUp(void)
{
	pt_tai = pt_init(tai_get_ns(), 100*NS_IN_MS, CLOCK_TAI);
	checkpoint();
}

void tearDown(void)
{
	if (pt_tai)
		free(pt_tai);
	pt_tai = NULL;
}


static void test_ts_add_ok(void)
{
	struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
	ts_subtract_ns(&ts, 100);
	TEST_ASSERT(ts.tv_sec == 0);
	TEST_ASSERT(ts.tv_nsec == (NS_IN_SEC - 100));
	ts_add_ns(&ts, 100);
	TEST_ASSERT(ts.tv_sec == 1);
	TEST_ASSERT(ts.tv_nsec == 0);
	ts_add_ns(&ts, 2*NS_IN_SEC);
	TEST_ASSERT(ts.tv_sec == 3);
	TEST_ASSERT(ts.tv_nsec == 0);
}

static void test_ts_normalize(void)
{
	ts_normalize(NULL);
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 3*NS_IN_SEC };
	ts_normalize(&ts);
	TEST_ASSERT(ts.tv_nsec == 0);
	TEST_ASSERT(ts.tv_sec == 3);

	ts.tv_sec = 1;
	ts.tv_nsec = 2*NS_IN_SEC + 1337;
	ts_normalize(&ts);
	TEST_ASSERT(ts.tv_sec == 3);
	TEST_ASSERT(ts.tv_nsec == 1337);
}


static void test_pt_init(void)
{
	TEST_ASSERT_NOT_NULL(pt_init(0, 100*NS_IN_MS, CLOCK_REALTIME));
	TEST_ASSERT_NULL(pt_init(0, 0, CLOCK_REALTIME));
	TEST_ASSERT_NULL(pt_init(0, 100 * NS_IN_MS, -1));
	TEST_ASSERT_NULL(pt_init(1024, 100 * NS_IN_MS, CLOCK_REALTIME));
}

static void test_pt_tai(void)
{
	uint64_t tai_now = tai_get_ns();
	struct periodic_timer *pt = pt_init(tai_now, 100*NS_IN_MS, CLOCK_TAI);
	TEST_ASSERT_NOT_NULL(pt);
	uint64_t ts_sec = tai_now / 1000000000;
	uint64_t ts_nsec = tai_now % 1000000000;
	TEST_ASSERT(pt->pt_ts.tv_sec == ts_sec);
	TEST_ASSERT(pt->pt_ts.tv_nsec == ts_nsec);
	TEST_ASSERT(pt->clock_id == CLOCK_TAI);
	TEST_ASSERT(pt->phase_ns == 100 * NS_IN_MS);
}
static void test_pt_tai_cycle(void)
{
	TEST_ASSERT(pt_next_cycle(NULL) == -1);
	TEST_ASSERT(pt_next_cycle(pt_tai) != -1);

	TEST_ASSERT(pt_next_cycle(pt_tai) != -1);

	checkpoint();
	TEST_ASSERT(pt_next_cycle(pt_tai) != -1);
	uint64_t diff = checkpoint();

	TEST_ASSERT(diff > 0);
	/* We should not sleep for a lot more than 100 us, but beware of
	 * scheduling issues on a busy system */
	TEST_ASSERT(diff < 150 * NS_IN_MS);
	TEST_ASSERT(diff > 50 * NS_IN_MS);
}

static void test_pt_mono_full(void)
{
	struct periodic_timer *pt = pt_init(0, 10*NS_IN_MS, CLOCK_MONOTONIC);
	TEST_ASSERT_NOT_NULL(pt);
	checkpoint();
	int iters = 20;
	for (int i = 0; i < iters; i++) {
		printf("."); fflush(stdout);
		TEST_ASSERT(pt_next_cycle(pt) == 0);
	}
	uint64_t diff = checkpoint();

	/* We are running as normal, expect some variation, but within
	 * 200us (should ideally be slower, but test-server is sometimes
	 * under load). At least it will catch a general timeout if
	 * timedwait is completely bonkers
	 *
	 * Since it is a periodic timer, it should not grow in error
	 */
	TEST_ASSERT_UINT64_WITHIN(200*NS_IN_US, iters*10*NS_IN_MS, diff);
}

static void test_pt_real_full(void)
{
	struct periodic_timer *pt = pt_init(0, 10*NS_IN_MS, CLOCK_REALTIME);
	TEST_ASSERT_NOT_NULL(pt);
	checkpoint();
	TEST_ASSERT(pt_next_cycle(pt) == 0);
	TEST_ASSERT(pt_next_cycle(pt) == 0);
	uint64_t diff = checkpoint();
	TEST_ASSERT_UINT64_WITHIN(200*NS_IN_US, 2*10*NS_IN_MS, diff);
}

static void test_pt_tai_offset(void)
{
	uint64_t now_ns = real_get_ns() + 50 * NS_IN_MS;

	/* First timer should start 50ms into the future and every iteration should be 10ms */
	struct periodic_timer *pt = pt_init(now_ns, 10*NS_IN_MS, CLOCK_REALTIME);

	checkpoint();
	TEST_ASSERT(pt_next_cycle(pt) == 0);
	uint64_t diff = checkpoint();

	TEST_ASSERT_UINT64_WITHIN(200*NS_IN_US, 60*NS_IN_MS, diff);

	// Adding this will cause the test to fail as we sleep outside
	// of periodic timer
	//
	// usleep(500000);
	checkpoint();
	int iters = 20;
	for (int i = 0; i < iters; i++) {
		printf("."); fflush(stdout);
		TEST_ASSERT(pt_next_cycle(pt) == 0);
	}
	diff = checkpoint();
	TEST_ASSERT_UINT64_WITHIN(100*NS_IN_US, iters*10*NS_IN_MS, diff);
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_ts_add_ok);
	RUN_TEST(test_ts_normalize);
	RUN_TEST(test_pt_init);
	RUN_TEST(test_pt_tai);
	RUN_TEST(test_pt_tai_cycle);
	RUN_TEST(test_pt_mono_full);
	RUN_TEST(test_pt_real_full);
	RUN_TEST(test_pt_tai_offset);
	return UNITY_END();
}
