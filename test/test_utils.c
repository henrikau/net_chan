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
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	uint64_t ts_now = ts.tv_sec * NS_IN_SEC + ts.tv_nsec;
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
	TEST_ASSERT(pt->phase == 100 * NS_IN_MS);
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
	TEST_ASSERT(diff < 100 * NS_IN_MS);
	TEST_ASSERT(diff > 95 * NS_IN_MS);
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_ts_add_ok);
	RUN_TEST(test_ts_normalize);
	RUN_TEST(test_pt_init);
	RUN_TEST(test_pt_tai);
	RUN_TEST(test_pt_tai_cycle);

	return UNITY_END();
}
