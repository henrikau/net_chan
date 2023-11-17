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

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_ts_add_ok);
	RUN_TEST(test_ts_normalize);

	return UNITY_END();
}
