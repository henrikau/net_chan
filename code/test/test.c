#include <stdio.h>
#include "unity.h"

/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/common.c"

#define DATA17SZ 32
char data17[DATA17SZ] = {0};
struct timedc_avtp *pdu17;

#define DATA42SZ 8
char data42[DATA42SZ] = {0};
struct timedc_avtp *pdu42;

struct nethandler *nh;

void setUp(void)
{
	pdu17 = pdu_create(17, DATA17SZ);
	pdu42 = pdu_create(42, DATA42SZ);
	memset(data42, 0x42, DATA42SZ);
	memset(data17, 0x17, DATA17SZ);
	nh = nh_init((unsigned char *)"lo", 16, (unsigned char *)"01:00:e5:01:02:42");
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
	struct timedc_avtp *pdu = pdu_create(43, 128);
	TEST_ASSERT(pdu != NULL);
	TEST_ASSERT(pdu->pdu.stream_id == 43);
	TEST_ASSERT(pdu->payload_size == 128);
	pdu_destroy(&pdu);
	printf("%s(): end\n", __func__);
}

static void test_pdu_update(void)
{
	TEST_ASSERT(pdu_update(NULL, 1, NULL, 7) == -ENOMEM);

	/* Test failed update of data and that other fields have not been altered*/
	TEST_ASSERT(pdu42->pdu.stream_id == 42);
	TEST_ASSERT(pdu_update(pdu42, 2, data42, DATA42SZ+1) == -EMSGSIZE);
	TEST_ASSERT(pdu42->pdu.stream_id == 42);

	TEST_ASSERT(pdu_update(pdu17, 3, NULL, DATA17SZ) == -ENOMEM);
	TEST_ASSERT(pdu_update(pdu17, 4, data17, DATA17SZ) == 0);
	TEST_ASSERT(pdu17->pdu.avtp_timestamp == 4);
	TEST_ASSERT(pdu17->pdu.tv == 1);
	TEST_ASSERT(pdu17->payload[0] == 0x17);
	TEST_ASSERT(pdu17->payload[DATA17SZ-1] == 0x17);

	/* Test payload */
	TEST_ASSERT(pdu42->pdu.stream_id == 42);
	uint64_t val = 0xdeadbeef;
	TEST_ASSERT(pdu_update(pdu42, 5, &val, DATA42SZ) == 0);
	TEST_ASSERT(pdu42->pdu.stream_id == 42);

	TEST_ASSERT(pdu42->pdu.avtp_timestamp == 5);
	uint64_t *pl = (uint64_t *)pdu42->payload;
	TEST_ASSERT(*pl == 0xdeadbeef);

	TEST_ASSERT(pdu17->pdu.subtype == AVTP_SUBTYPE_TIMEDC);
	TEST_ASSERT(pdu42->pdu.subtype == AVTP_SUBTYPE_TIMEDC);
}


static void *cb_data = NULL;
static void *cb_pdu = NULL;
int nh_callback(void *data, struct timedc_avtp *pdu)
{
	cb_data = data;
	cb_pdu = pdu;
	return 0;
}

static void test_nh_hashmap(void)
{
	unsigned char *cb_priv_data = malloc(32);
	if (!cb_priv_data)
		return;

	TEST_ASSERT(nh->hmap_sz == 16);
	TEST_ASSERT(nh_reg_callback(NULL, 16, cb_priv_data, nh_callback) == -EINVAL);
	TEST_ASSERT(nh_reg_callback(nh, 16, NULL, NULL) == -EINVAL);

	TEST_ASSERT(nh_reg_callback(nh, 15, NULL, nh_callback) == 0);
	TEST_ASSERT(nh_reg_callback(nh, 16, cb_priv_data, nh_callback) == 0);
	TEST_ASSERT(nh->hmap[0].priv_data == cb_priv_data);
	TEST_ASSERT(nh->hmap[0].cb == nh_callback);
	TEST_ASSERT(get_hm_idx(nh, 16) == 0);

	TEST_ASSERT(get_hm_idx(nh, 17) == -1);
	TEST_ASSERT(nh_reg_callback(nh, 17, cb_priv_data, nh_callback) == 0);
	TEST_ASSERT(get_hm_idx(nh, 17) == 1);
	TEST_ASSERT(nh_reg_callback(nh, 33, cb_priv_data, nh_callback) == 0);
	TEST_ASSERT(get_hm_idx(nh, 33) == 2);

	nh_destroy(&nh);
	free(cb_priv_data);
}

static void test_nh_feed_pdu(void)
{
	unsigned char *cb_priv_data = malloc(32);
	if (!cb_priv_data)
		return;
	memset(cb_priv_data, 0xa0, 32);

	TEST_ASSERT(nh_feed_pdu(NULL, NULL) == -EINVAL);
	TEST_ASSERT(nh_feed_pdu(nh, pdu42) == -EBADFD);

	TEST_ASSERT(nh_reg_callback(nh, 16, cb_priv_data, nh_callback) == 0);
	TEST_ASSERT(nh_feed_pdu(nh, pdu42) == -EBADFD);

	TEST_ASSERT(nh_reg_callback(nh, 42, cb_priv_data, nh_callback) == 0);
	TEST_ASSERT(cb_pdu == NULL);
	TEST_ASSERT(cb_data == NULL);
	TEST_ASSERT(nh_feed_pdu(nh, pdu42) == 0);
	TEST_ASSERT(cb_pdu == pdu42);
	TEST_ASSERT(cb_data == cb_priv_data);
	TEST_ASSERT(((struct timedc_avtp *)cb_pdu)->pdu.stream_id == 42);

	TEST_ASSERT(((unsigned char *)cb_data)[0] == 0xa0);
	TEST_ASSERT(((unsigned char *)cb_data)[31] == 0xa0);

	/* verify that calling feed_pdu will call cb with correct data */
	TEST_ASSERT(nh_reg_callback(nh, 17, cb_priv_data, nh_callback) == 0);
	TEST_ASSERT(nh_feed_pdu(nh, pdu17) == 0);
	TEST_ASSERT(((struct timedc_avtp *)cb_pdu)->pdu.stream_id == 17);
	TEST_ASSERT(nh_feed_pdu(nh, pdu42) == 0);
	TEST_ASSERT(((struct timedc_avtp *)cb_pdu)->pdu.stream_id == 42);
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_pdu_create);
	RUN_TEST(test_pdu_update);
	RUN_TEST(test_nh_hashmap);
	RUN_TEST(test_nh_feed_pdu);
	return UNITY_END();
}
