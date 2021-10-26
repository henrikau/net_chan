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
		pdu17 = NULL;
	}
}

static void test_pdu_create(void)
{
	struct timedc_avtp *pdu = pdu_create(43, 0, 128);
	TEST_ASSERT(pdu->pdu.stream_id == 43);
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
	struct nethandler *nh;
	unsigned char *cb_priv_data = malloc(32);
	nh = nh_init((unsigned char *)"lo", 16, (unsigned char *)"14:da:e9:2b:0a:c1");
	TEST_ASSERT(nh->hmap_sz == 16);
	TEST_ASSERT(nh_reg_callback(NULL, 17, cb_priv_data, nh_callback) == -1);
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

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_pdu_create);
	RUN_TEST(test_pdu_update);
	RUN_TEST(test_nh_hashmap);
	return UNITY_END();
}
