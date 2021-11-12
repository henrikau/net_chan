#include <stdio.h>

#include "unity.h"
#include "test_net_fifo.h"

#include <unistd.h>

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

/* PIPE callback */
int pfd[2];
void setUp(void)
{
	nh = nh_init((unsigned char *)"lo", 16);
	pdu17 = pdu_create(nh, (unsigned char *)"01:00:e5:01:02:42", 17, DATA17SZ);
	pdu42 = pdu_create(nh, (unsigned char *)"01:00:e5:01:02:42", 42, DATA42SZ);
	memset(data42, 0x42, DATA42SZ);
	memset(data17, 0x17, DATA17SZ);

	/* Use a pipe as fd to test callback */
	pipe(pfd);
}

void tearDown(void)
{
	pdu_destroy(&pdu17);
	pdu_destroy(&pdu42);
	nh_destroy(&nh);
	/* close pipes */
	close(pfd[0]);
	close(pfd[1]);

	if (_nh)
		nh_destroy(&_nh);
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
	TEST_ASSERT(get_hm_idx(NULL, 17) == -ENOMEM);

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
	int res = 0;
	res = nh_feed_pdu(nh, pdu42);
	TEST_ASSERT(res == 0);
	TEST_ASSERT(((struct timedc_avtp *)cb_pdu)->pdu.stream_id == 42);
	free(cb_priv_data);
}

static void test_create_cb(void)
{
	/* use pipe w to send data */
	struct cb_priv cbp = { .fd = pfd[1], };

	int (*cb)(void *priv_data, struct timedc_avtp *pdu) = nh_std_cb;
	TEST_ASSERT(cb(NULL, NULL) == -EINVAL);
	TEST_ASSERT(cb(&cbp, NULL) == -EINVAL);
	TEST_ASSERT(cb(NULL, pdu42) == -EINVAL);


	TEST_ASSERT(nh_reg_callback(nh, 42, &cbp, cb) == 0);

	uint64_t val = 0xdeadbeef;
	pdu_update(pdu42, 0, &val);
	TEST_ASSERT(nh_feed_pdu(nh, pdu42) == 0);

	/* Verify that callback has written data into pipe */
	uint64_t res = 0;
	int rdsz = read(pfd[0], &res, sizeof(uint64_t));

	TEST_ASSERT(rdsz == 8);
	TEST_ASSERT(res == 0xdeadbeef);

	val = 1;
}

static void test_create_tx_fifo(void)
{
	struct timedc_avtp *du = NETFIFO_TX("test1", pfd);
	TEST_ASSERT_NOT_NULL_MESSAGE(du, "Missing DU, should have been created from valid name");
	TEST_ASSERT(du->nh == _nh);
	TEST_ASSERT_MESSAGE(strncmp(du->name, "test1", 5) == 0, "wrong net_fifo returned");
	TEST_ASSERT(du->dst[0] == 0x01);
	TEST_ASSERT(du->dst[1] == 0x00);
	TEST_ASSERT(du->dst[2] == 0x5e);

	uint64_t val = 32;
	TEST_ASSERT(pdu_update(du, 0, &val) == 0);

	/* Cleanup memory */
	pdu_destroy(&du);
}


int main(int argc, char *argv[])
{
	UNITY_BEGIN();

	RUN_TEST(test_nh_hashmap);
	RUN_TEST(test_nh_feed_pdu);
	RUN_TEST(test_create_cb);
	RUN_TEST(test_create_tx_fifo);

	return UNITY_END();
}
