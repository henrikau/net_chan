#include <stdio.h>
#include <netchan.h>
#include "unity.h"
#include <unistd.h>
#include "../src/logger.c"


void setUp(void)
{
}

void tearDown(void)
{
	/* NOTE: we are highjacking malloc and free, so .. avoid using
	 * those here
	 */
}


static void test_create(void)
{
	TEST_ASSERT_NULL(log_create(NULL));
	TEST_ASSERT_NULL(log_create(""));
	struct logc *l = log_create("/tmp/tmplog.csv");
	TEST_ASSERT_NOT_NULL(l);
	TEST_ASSERT_NOT_NULL(l->lb);
	TEST_ASSERT_NOT_NULL(l->wdb);
}

static void test_create_wb(void)
{
	TEST_ASSERT_EQUAL(-EINVAL, _log_create_wakeup_delay_buffer(NULL));

	struct logc *logc = calloc(1, sizeof(struct logc));
	TEST_ASSERT_NOT_NULL(logc);
	TEST_ASSERT_EQUAL(0, _log_create_wakeup_delay_buffer(logc));
	free(logc->wdb);
	free(logc);
}

static void test_create_ts(void)
{
	TEST_ASSERT_EQUAL(-EINVAL, _log_create_ts(NULL));
	struct logc *logc = calloc(1, sizeof(struct logc));
	TEST_ASSERT_NOT_NULL(logc);
}

static void test_log_destroy(void)
{
	struct logc *logger  = log_create("/tmp/testlogger.csv");
	TEST_ASSERT_NOT_NULL(logger);
	log_destroy(logger);

	/* Expose failure to handle invalid parameters */
	log_destroy(NULL);

	/* logger is not correctly initialized, but don't explode. */
	logger = calloc(1, sizeof(*logger));
	log_destroy(logger);
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_create);
	RUN_TEST(test_create_wb);
	RUN_TEST(test_create_ts);
	RUN_TEST(test_log_destroy);
	return UNITY_END();
}
