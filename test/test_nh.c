
#include <stdio.h>

#include "unity.h"
#include "test_net_fifo.h"

#include <unistd.h>

/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/netchan.c"
#include "../src/netchan_standalone.c"
#include "../src/netchan_socket.c"

#define DATA17SZ 32
#define INT17 INT_10HZ
char data17[DATA17SZ] = {0};
struct channel *pdu17;

#define DATA42SZ 8
#define INT42 INT_50HZ
char data42[DATA42SZ] = {0};
struct channel *pdu42;
struct channel *pdu43_r;

struct nethandler *nh;

static void *cb_data = NULL;
static void *cb_pdu = NULL;

/* PIPE callback */
int pfd[2];
void setUp(void)
{
	nh = nh_create_init("lo", 16, NULL);
	pdu17 = _chan_create(nh, &nc_channels[MCAST17]);
	pdu42 = _chan_create(nh, &nc_channels[MCAST42]);
	pdu43_r = _chan_create(nh, &nc_channels[PDU43_R]);
	memset(data42, 0x42, DATA42SZ);
	memset(data17, 0x17, DATA17SZ);

	/* Use a pipe as fd to test callback */
	pipe(pfd);
}

void tearDown(void)
{
	chan_destroy(&pdu17);
	chan_destroy(&pdu42);
	chan_destroy(&pdu43_r);
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
	TEST_ASSERT(cb(NULL, &pdu43_r->pdu) == -EINVAL);

	/* We are are constructiong the channel manually, so we need to
	 * update the cbp pointer as well.
	 */
	pdu43_r->cbp = &cbp;

	TEST_ASSERT(nh_reg_callback(nh, 43, &cbp, cb) == 0);

	uint64_t val = 0xdeadbeef;
	TEST_ASSERT(chan_update(pdu43_r, 0, &val) == 0);
	TEST_ASSERT(nh_feed_pdu(nh, &pdu43_r->pdu) == 0);

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
	if (pm)
		free(pm);
	val = 1;

	/* Remember to drop ref to object of automatic storage duration.. (thanks Olve!) */
	pdu43_r->cbp = NULL;
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
	TEST_ASSERT(nh_reg_callback(nh_small, 5, &cbp, cb) == -ENOMEM);

	nh_destroy(&nh_small);
}


static void test_create_tx_fifo(void)
{
	NETCHAN_TX(test1);
	TEST_ASSERT_NOT_NULL_MESSAGE(test1_du, "Missing DU, should have been created from valid name");
	TEST_ASSERT(test1_du->nh == _nh);
	TEST_ASSERT(test1_du->dst[0] == 0x01);
	TEST_ASSERT(test1_du->dst[1] == 0x00);
	TEST_ASSERT(test1_du->dst[2] == 0x5e);

	uint64_t val = 32;
	TEST_ASSERT(chan_update(test1_du, 0, &val) == 0);

	/* Cleanup memory not needed, injected in standalone, nh_destroy will free*/
}

static void test_nh_standalone_create(void)
{
	TEST_ASSERT_NULL(_nh);
	TEST_ASSERT(nh_create_init_standalone() == 0);
	TEST_ASSERT_NOT_NULL(_nh);

	/* make sure default values have been set */
	TEST_ASSERT(!_nh->verbose);
	TEST_ASSERT(!_nh->use_srp);
	TEST_ASSERT(_nh->ftrace_break_us == -1);


	/* make sure default values have not been changed set */
	nh_set_trace_breakval(_nh, 50000);
	TEST_ASSERT(nh_create_init_standalone() == 0);
	TEST_ASSERT(_nh->ftrace_break_us == 50000);
}

static void test_nh_standalone_destroy(void)
{
	TEST_ASSERT_NULL(_nh);
	TEST_ASSERT(nh_create_init_standalone() == 0);
	nh_destroy_standalone();
	TEST_ASSERT_NULL(_nh);
}


static void test_nh_add_remove_tx_chan(void)
{
	TEST_ASSERT(nh_get_num_tx(nh) == 0);
	struct channel *ch1 = chan_create_tx(nh, &nc_channels[MCAST43]);
	TEST_ASSERT_NOT_NULL(ch1);
	TEST_ASSERT(nh_get_num_tx(nh) == 1);

	struct channel *ch2 = chan_create_tx(nh, &nc_channels[MCAST42]);
	TEST_ASSERT(nh_get_num_tx(nh) == 2);

	TEST_ASSERT(nh_remove_tx(NULL) == -ENOMEM);
	TEST_ASSERT(nh_remove_tx(ch1) == 0);
	TEST_ASSERT(nh_get_num_tx(nh) == 1);

	/* Cannot do double remove */
	TEST_ASSERT(nh_remove_tx(ch1) == -ENOMEM);

	/* Remove final */
	TEST_ASSERT(nh_remove_tx(ch2) == 0);
	TEST_ASSERT(nh_get_num_tx(nh) == 0);
}

static void test_nh_add_remove_rx_chan(void)
{
	struct channel_attrs chanattr = {
		.dst       = DEFAULT_MCAST,
		.stream_id = 42,
		.sc        = CLASS_A,
		.size      = 8,
		.interval_ns      = INT_50HZ,
		.name      = "test1"};
	struct channel *ch[10];
	for (int i = 0; i < 10; i++) {
		chanattr.stream_id++;
		ch[i] = chan_create_rx(nh, &chanattr);
		TEST_ASSERT_NOT_NULL(ch[i]);
		TEST_ASSERT(nh_get_num_rx(nh) == i+1);
	}
	TEST_ASSERT(nh_remove_rx(NULL) == -ENOMEM);

	/* Remove head */
	TEST_ASSERT(ch[0]->nh != NULL);
	TEST_ASSERT(ch[0]->next != NULL);
	TEST_ASSERT(nh_remove_rx(nh->du_rx_head) == 0);
	TEST_ASSERT(ch[0]->nh == NULL);
	TEST_ASSERT(ch[0]->next == NULL);
	TEST_ASSERT(nh_get_num_rx(nh) == 9);

	/* Double remove */
	TEST_ASSERT(nh_remove_rx(ch[0]) == -ENOMEM);

	/* Remove tail */
	TEST_ASSERT(nh_remove_rx(ch[9]) == 0);
	TEST_ASSERT(ch[9]->nh == NULL);
	TEST_ASSERT(ch[9]->next == NULL);
	TEST_ASSERT(nh_get_num_rx(nh) == 8);

	/* Remove middle */
	TEST_ASSERT(nh_remove_rx(ch[5]) == 0);
	TEST_ASSERT(ch[5]->nh == NULL);
	TEST_ASSERT(ch[5]->next == NULL);
	TEST_ASSERT(nh_get_num_rx(nh) == 7);
}

static void test_sc_values(void)
{
	TEST_ASSERT_EQUAL_MESSAGE(2*NS_IN_MS, CLASS_A, "Class A should have default value 2 ms");
	TEST_ASSERT_EQUAL_MESSAGE(50*NS_IN_MS, CLASS_B, "Class B should have default value 50 ms");
}
int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	nc_set_nic("lo");

	RUN_TEST(test_nh_hashmap);
	RUN_TEST(test_nh_feed_pdu);
	RUN_TEST(test_create_cb);
	RUN_TEST(test_nh_add_cb_overflow);
	RUN_TEST(test_create_tx_fifo);
	RUN_TEST(test_nh_standalone_create);
	RUN_TEST(test_nh_standalone_destroy);
	RUN_TEST(test_nh_add_remove_tx_chan);
	RUN_TEST(test_nh_add_remove_rx_chan);
	RUN_TEST(test_sc_values);

	return UNITY_END();
}
