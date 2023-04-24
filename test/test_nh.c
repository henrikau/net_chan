#include <stdio.h>

#include "unity.h"
#include "test_net_fifo.h"

#include <unistd.h>

/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/netchan.c"

#define DATA17SZ 32
char data17[DATA17SZ] = {0};
struct netchan_avtp *pdu17;

#define DATA42SZ 8
char data42[DATA42SZ] = {0};
struct netchan_avtp *pdu42;

struct nethandler *nh;

static void *cb_data = NULL;
static void *cb_pdu = NULL;

/* PIPE callback */
int pfd[2];
void setUp(void)
{
	nh = nh_create_init("lo", 16, NULL);
	pdu17 = pdu_create(nh, (unsigned char *)"01:00:e5:01:02:42", 17, CLASS_A, DATA17SZ);
	pdu42 = pdu_create(nh, (unsigned char *)"01:00:e5:01:02:42", 42, CLASS_B, DATA42SZ);
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

	nh_destroy_standalone();
	cb_data = NULL;
	cb_pdu = NULL;
}



int nh_callback(void *data, struct avtpdu_cshdr *du)
{
	cb_data = data;
	cb_pdu = du;
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
	TEST_ASSERT(nh_feed_pdu(nh, &pdu42->pdu) == -EBADFD);

	TEST_ASSERT(nh_reg_callback(nh, 16, cb_priv_data, nh_callback) == 0);
	TEST_ASSERT(nh_feed_pdu(nh, &pdu42->pdu) == -EBADFD);

	TEST_ASSERT(nh_reg_callback(nh, 42, cb_priv_data, nh_callback) == 0);
	TEST_ASSERT(cb_pdu == NULL);
	TEST_ASSERT(cb_data == NULL);

	TEST_ASSERT(nh_feed_pdu(nh, &pdu42->pdu) == 0);
	TEST_ASSERT(cb_pdu == &pdu42->pdu);
	TEST_ASSERT(cb_data == cb_priv_data);
	TEST_ASSERT(((struct avtpdu_cshdr *)cb_pdu)->stream_id == htobe64(42));

	TEST_ASSERT(((unsigned char *)cb_data)[0] == 0xa0);
	TEST_ASSERT(((unsigned char *)cb_data)[31] == 0xa0);

	/* verify that calling feed_pdu will call cb with correct data */
	TEST_ASSERT(nh_reg_callback(nh, 17, cb_priv_data, nh_callback) == 0);
	TEST_ASSERT(nh_feed_pdu(nh, &pdu17->pdu) == 0);
	TEST_ASSERT(((struct avtpdu_cshdr *)cb_pdu)->stream_id == htobe64(17));
	int res = 0;
	res = nh_feed_pdu(nh, &pdu42->pdu);
	TEST_ASSERT(res == 0);
	TEST_ASSERT(((struct avtpdu_cshdr *)cb_pdu)->stream_id == htobe64(42));
	free(cb_priv_data);
}

static void test_create_cb(void)
{
	/* use pipe w to send data */
	struct cb_priv cbp = {
		.sz = sizeof(uint64_t),
		.fd = pfd[1], };

	int (*cb)(void *priv_data, struct avtpdu_cshdr *du) = nh_std_cb;
	TEST_ASSERT(cb(NULL, NULL) == -EINVAL);
	TEST_ASSERT(cb(&cbp, NULL) == -EINVAL);
	TEST_ASSERT(cb(NULL, &pdu42->pdu) == -EINVAL);


	TEST_ASSERT(nh_reg_callback(nh, 42, &cbp, cb) == 0);

	uint64_t val = 0xdeadbeef;
	pdu_update(pdu42, 0, &val);
	TEST_ASSERT(nh_feed_pdu(nh, &pdu42->pdu) == 0);

	/* Verify that callback has written data into pipe
	 *
	 * We bundle some metadata alongside the payload in the pipe (to
	 * feed timestamps), so it's somewhat more involved dissecting
	 * the data.
	 */
	struct pipe_meta *pm = malloc(sizeof(struct pipe_meta) + sizeof(uint64_t));
	int rdsz = read(pfd[0], pm, sizeof(*pm) + sizeof(uint64_t));
	uint64_t *res = (uint64_t *)&pm->payload[0];

	TEST_ASSERT(rdsz == sizeof(*pm) + sizeof(uint64_t));
	TEST_ASSERT(*res == 0xdeadbeef);
	free(pm);
	val = 1;
}

static void test_nh_add_cb_overflow(void)
{
	struct cb_priv cbp = { .fd = pfd[1], };
	int (*cb)(void *priv_data, struct avtpdu_cshdr *du) = nh_std_cb;
	struct nethandler *nh_small = nh_create_init("lo", 4, NULL);

	TEST_ASSERT(nh_reg_callback(nh_small, 1, &cbp, cb) == 0);
	TEST_ASSERT(nh_reg_callback(nh_small, 2, &cbp, cb) == 0);
	TEST_ASSERT(nh_reg_callback(nh_small, 3, &cbp, cb) == 0);
	TEST_ASSERT(nh_reg_callback(nh_small, 4, &cbp, cb) == 0);
	TEST_ASSERT(nh_reg_callback(nh_small, 5, &cbp, cb) == -1);

	nh_destroy(&nh_small);
}


static void test_create_tx_fifo(void)
{
	NETCHAN_TX(test1);
	TEST_ASSERT_NOT_NULL_MESSAGE(test1_du, "Missing DU, should have been created from valid name");
	TEST_ASSERT(test1_du->nh == _nh);
	TEST_ASSERT_MESSAGE(strncmp(test1_du->name, "test1", 5) == 0, "wrong net_fifo returned");
	TEST_ASSERT(test1_du->dst[0] == 0x01);
	TEST_ASSERT(test1_du->dst[1] == 0x00);
	TEST_ASSERT(test1_du->dst[2] == 0x5e);

	uint64_t val = 32;
	TEST_ASSERT(pdu_update(test1_du, 0, &val) == 0);

	/* Cleanup memory not needed, injected in standalone, nh_destroy will free*/
}

static void test_nh_standalone_create(void)
{
	TEST_ASSERT_NULL(_nh);
	TEST_ASSERT(nh_create_init_standalone() == 0);
	TEST_ASSERT_NOT_NULL(_nh);
	TEST_ASSERT(nh_create_init_standalone() == -1);
}

static void test_nh_standalone_destroy(void)
{
	TEST_ASSERT_NULL(_nh);
	TEST_ASSERT(nh_create_init_standalone() == 0);
	nh_destroy_standalone();
	TEST_ASSERT_NULL(_nh);
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	nf_set_nic("lo");

	RUN_TEST(test_nh_hashmap);
	RUN_TEST(test_nh_feed_pdu);
	RUN_TEST(test_create_cb);
	RUN_TEST(test_nh_add_cb_overflow);
	RUN_TEST(test_create_tx_fifo);
	RUN_TEST(test_nh_standalone_create);
	RUN_TEST(test_nh_standalone_destroy);

	return UNITY_END();
}
