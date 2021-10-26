#include <stdio.h>
#include "unity.h"

/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/common.c"

#define DATA42SZ 8
#define DATA17SZ 32
static char data42[DATA42SZ] = {0};
static char data17[DATA17SZ] = {0};
static struct timedc_avtp *pdu42 = NULL;
static struct timedc_avtp *pdu17 = NULL;

void setUp(void)
{
	pdu42 = pdu_create(42, 0, DATA42SZ);
	pdu17 = pdu_create(17, 1, DATA17SZ);
}

void tearDown(void)
{
	if (pdu42) {
		free(pdu42);
		pdu42 = NULL;
	}
	if (pdu17) {
		free(pdu17);
		pdu42 = NULL;
	}
}

static void test_pdu_create(void)
{
	struct timedc_avtp *pdu = pdu_create(42, 0, 128);
	TEST_ASSERT(pdu->pdu.stream_id == 42);
	TEST_ASSERT(pdu->payload_size == 128);
	free(pdu);
}

static void test_pdu_update(void)
{

	TEST_ASSERT(pdu_update(NULL, 1234, NULL, 7) == -ENOMEM);
	TEST_ASSERT(pdu_update(pdu42, 1234, data42, DATA42SZ+1) == -EINVAL);
	TEST_ASSERT(pdu_update(pdu17, 1236, NULL, DATA17SZ) == -EINVAL);
	TEST_ASSERT(pdu_update(pdu17, 1235, data17, DATA17SZ) == 0);
	TEST_ASSERT(pdu17->pdu.avtp_timestamp == 1235);
	TEST_ASSERT(pdu17->pdu.tv == 1);
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_pdu_create);
	RUN_TEST(test_pdu_update);

	return UNITY_END();
}
