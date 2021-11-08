#include <stdio.h>
#include "unity.h"
#include "test_net_fifo.h"

/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/timedc_avtp.c"

#define DATA17SZ 32
char data17[DATA17SZ] = {0};
struct timedc_avtp *pdu17;

#define DATA42SZ 8
char data42[DATA42SZ] = {0};
struct timedc_avtp *pdu42;

struct nethandler *nh;

void setUp(void)
{
	nh = nh_init((unsigned char *)"lo", 16);
	pdu17 = pdu_create(nh, (unsigned char *)"01:00:e5:01:02:42", 17, DATA17SZ);
	pdu42 = pdu_create(nh, (unsigned char *)"01:00:e5:01:02:42", 42, DATA42SZ);
	memset(data42, 0x42, DATA42SZ);
	memset(data17, 0x17, DATA17SZ);
}

void tearDown(void)
{
	pdu_destroy(&pdu17);
	pdu_destroy(&pdu42);
	nh_destroy(&nh);
}

static void test_pdu_create(void)
{
	printf("%s(): start\n", __func__);
	struct timedc_avtp *pdu = pdu_create(nh, (unsigned char *)"01:00:e5:01:02:42", 43, 128);
	TEST_ASSERT(pdu != NULL);
	TEST_ASSERT(pdu->pdu.stream_id == 43);
	TEST_ASSERT(pdu->payload_size == 128);
	pdu_destroy(&pdu);
	TEST_ASSERT(pdu == NULL);

	printf("%s(): end\n", __func__);
}

static void test_pdu_update(void)
{
	TEST_ASSERT(pdu_update(NULL, 1, NULL) == -ENOMEM);

	/* Test failed update of data and that other fields have not been altered*/
	TEST_ASSERT(pdu42->pdu.stream_id == 42);
	TEST_ASSERT(pdu42->pdu.stream_id == 42);

	TEST_ASSERT(pdu_update(pdu17, 3, NULL) == -ENOMEM);
	TEST_ASSERT(pdu_update(pdu17, 4, data17) == 0);
	TEST_ASSERT(pdu17->pdu.avtp_timestamp == 4);
	TEST_ASSERT(pdu17->pdu.tv == 1);
	TEST_ASSERT(pdu17->payload[0] == 0x17);
	TEST_ASSERT(pdu17->payload[DATA17SZ-1] == 0x17);

	/* Test payload */
	TEST_ASSERT(pdu42->pdu.stream_id == 42);
	uint64_t val = 0xdeadbeef;
	TEST_ASSERT(pdu_update(pdu42, 5, &val) == 0);
	TEST_ASSERT(pdu42->pdu.stream_id == 42);

	TEST_ASSERT(pdu42->pdu.avtp_timestamp == 5);
	uint64_t *pl = (uint64_t *)pdu42->payload;
	TEST_ASSERT(*pl == 0xdeadbeef);

	TEST_ASSERT(pdu17->pdu.subtype == AVTP_SUBTYPE_TIMEDC);
	TEST_ASSERT(pdu42->pdu.subtype == AVTP_SUBTYPE_TIMEDC);
}

static void test_pdu_get_payload(void)
{
	uint64_t val = 0xdeadbeef;
	pdu_update(pdu42, 5, &val);
	uint64_t *pl = pdu_get_payload(pdu42);
	TEST_ASSERT(*pl == val);
	TEST_ASSERT(pdu_get_payload(NULL) == NULL)
}

static void test_pdu_send(void)
{
	TEST_ASSERT(pdu_send(NULL) == -ENOMEM);

	uint64_t val = 0xdeadbeef;
	pdu_update(pdu42, 5, &val);
	TEST_ASSERT(pdu_send(pdu42) == -EINVAL);

}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_pdu_create);
	RUN_TEST(test_pdu_update);
	RUN_TEST(test_pdu_get_payload);
	RUN_TEST(test_pdu_send);
	return UNITY_END();
}
