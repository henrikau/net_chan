#include <stdio.h>
#include "unity.h"
#include "test_net_fifo.h"

/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/netchan.c"

#define DATA17SZ 32
#define INT17 INT_10HZ
char data17[DATA17SZ] = {0};
struct channel *pdu17;

#define DATA42SZ 8
#define INT42 INT_50HZ
char data42[DATA42SZ] = {0};
struct channel *pdu42;

struct nethandler *nh;

void setUp(void)
{
	nh = nh_create_init("lo", 16, NULL);
	pdu17 = chan_create(nh, (unsigned char *)"01:00:e5:01:02:42", 17, CLASS_A, DATA17SZ, INT17);
	pdu42 = chan_create(nh, (unsigned char *)"01:00:e5:01:02:42", 42, CLASS_A, DATA42SZ, INT42);
	memset(data42, 0x42, DATA42SZ);
	memset(data17, 0x17, DATA17SZ);
}

void tearDown(void)
{
	pdu_destroy(&pdu17);
	pdu_destroy(&pdu42);
	nh_destroy(&nh);

	/* remember to destroy _nh when using standalone */
	if (_nh)
		nh_destroy(&_nh);
}

static void test_chan_create(void)
{
	printf("%s(): start\n", __func__);
	TEST_ASSERT_NULL_MESSAGE(chan_create(NULL, (unsigned char *)"01:00:e5:01:02:42", 43, CLASS_B, 128, INT_50HZ),
				"Cannot create PDU and assign to non-existant nethandler!");

	struct channel *pdu = chan_create(nh, (unsigned char *)"01:00:e5:01:02:42", 43, CLASS_B, 128, INT_50HZ);
	TEST_ASSERT(pdu != NULL);
	TEST_ASSERT(pdu->pdu.stream_id == be64toh(43));
	TEST_ASSERT(pdu->payload_size == 128);
	pdu_destroy(&pdu);
	TEST_ASSERT(pdu == NULL);

	printf("%s(): end\n", __func__);
}

static void test_invalid_interval(void)
{
	/* 0 */
	TEST_ASSERT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 8, 0),
				"Invalid period (0)");

	/* shorter than minimum time to send smallest possible payload
	 * on link capacity.
	 * For test, we use lo, which sets the capacity to 1 Gbps, i.e 1ns pr bit
	 */
	TEST_ASSERT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 8, 66),
				"Invalid period (66 bits)");
	TEST_ASSERT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 8, 66*8),
				"Invalid period (528 ns (66 bytes on 1 Gbps link)");
	TEST_ASSERT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 8, 66*8-1),
				"Invalid period (527 ns (66 bytes -1 bit on 1 Gbps link)");
	TEST_ASSERT_NOT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 8, 74*8+1),
				"Failed for a valid period (time for 8 bytes payload (+ headers) + 1 bit)");

	/* larger than one hour (why would you need this?) */
	TEST_ASSERT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 8, 1e9*3600),
				"Too long period (> 1 hour)");
}

static void test_invalid_payload_size(void)
{
	/* payload 0 */
	TEST_ASSERT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 0, 50*NS_IN_MS),
				"Invalid size (0 bytes)");

	/* payload -> exceed MTU */
	TEST_ASSERT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 2048, 50*NS_IN_MS),
				"Invalid size (2k bytes)");
	TEST_ASSERT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 1500, 50*NS_IN_MS),
				"Invalid size (1500B payload, forgetting AVTP header)");
	TEST_ASSERT_NOT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 1476, 50*NS_IN_MS),
				"Just about valid size (1476 B payload, adding AVTP header -> 1500)");
}

static void test_payload_size_interval_combo(void)
{
	/* 1 Gbps needs 1522 ns to send 1476 payload when adding headers and IPG/preamble*/
	TEST_ASSERT_NOT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 8, 1500),
				"Invalid size (8) interval (1500ns)");
	TEST_ASSERT_NULL_MESSAGE(chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 1476, 1500),
				"Invalid size (1476 bytes) and interval (1500ns) combination, > 100% utilization");
}

static void test_pdu_update(void)
{
	TEST_ASSERT(pdu_update(NULL, 1, NULL) == -ENOMEM);

	/* Test failed update of data and that other fields have not been altered*/
	TEST_ASSERT(pdu42->pdu.stream_id == be64toh(42));
	TEST_ASSERT(pdu42->pdu.stream_id == be64toh(42));

	TEST_ASSERT(pdu_update(pdu17, 3, NULL) == -ENOMEM);
	TEST_ASSERT(pdu_update(pdu17, 4, data17) == 0);
	TEST_ASSERT(pdu17->pdu.avtp_timestamp == htonl(4));
	TEST_ASSERT(pdu17->pdu.tv == 1);
	TEST_ASSERT(pdu17->payload[0] == 0x17);
	TEST_ASSERT(pdu17->payload[DATA17SZ-1] == 0x17);

	/* Test payload */
	TEST_ASSERT(pdu42->pdu.stream_id == be64toh(42));
	uint64_t val = 0xdeadbeef;
	TEST_ASSERT(pdu_update(pdu42, 5, &val) == 0);
	TEST_ASSERT(pdu42->pdu.stream_id == be64toh(42));

	TEST_ASSERT(pdu42->pdu.avtp_timestamp == htonl(5));
	uint64_t *pl = (uint64_t *)pdu42->payload;
	TEST_ASSERT(*pl == 0xdeadbeef);

	TEST_ASSERT(pdu17->pdu.subtype == AVTP_SUBTYPE_NETCHAN);
	TEST_ASSERT(pdu42->pdu.subtype == AVTP_SUBTYPE_NETCHAN);
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

static void test_create_standalone(void)
{

	TEST_ASSERT(chan_create_standalone(NULL, false, net_fifo_chans, nfc_sz) == NULL);
	TEST_ASSERT(chan_create_standalone("missing", false, net_fifo_chans, nfc_sz) == NULL);
	NETCHAN_RX(missing);
	TEST_ASSERT(missing_du == NULL);

	struct channel *pdu;
	pdu = chan_create_standalone("test1", false, net_fifo_chans, nfc_sz);
	TEST_ASSERT(pdu != NULL);

	pdu = chan_create_standalone("test2", false, net_fifo_chans, nfc_sz);
	TEST_ASSERT(pdu != NULL);

	NETCHAN_RX(test1);
	TEST_ASSERT(test1_du != NULL);

	/* Test pdu internals after macro creation */
	TEST_ASSERT(test1_du->pdu.stream_id == be64toh(42));
	for (int i = 0; i < ETH_ALEN; i++)
		TEST_ASSERT(test1_du->dst[i] == net_fifo_chans[0].dst[i]);
	TEST_ASSERT(test1_du->nh == _nh);

}

static void test_add_anon_pdu(void)
{
	TEST_ASSERT(nh_get_num_tx(nh) == 0);
	TEST_ASSERT_NULL(nh->du_tx_head);
	struct channel *du = chan_create(nh, (unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 8, INT_50HZ);
	TEST_ASSERT_NOT_NULL(du);
	TEST_ASSERT(nh_add_tx(NULL, du) == -EINVAL);
	TEST_ASSERT(nh_add_tx(nh, NULL) == -EINVAL);
	TEST_ASSERT(nh_add_tx(nh, du) == 0);
	TEST_ASSERT_NOT_NULL(nh->du_tx_head);
	TEST_ASSERT(nh->du_tx_head == du);
	TEST_ASSERT(nh->du_tx_tail == du);
	TEST_ASSERT(nh_get_num_tx(nh) == 1);

	struct channel *du2 = chan_create(nh, (unsigned char *)"01:00:e5:01:02:43", 43, CLASS_A, 8, INT_50HZ);
	TEST_ASSERT(nh_add_tx(nh, du2) == 0);
	TEST_ASSERT(nh_get_num_tx(nh) == 2);
	TEST_ASSERT(nh->du_tx_head == du);
	TEST_ASSERT(nh->du_tx_tail == du2);
	TEST_ASSERT_NULL(nh->du_tx_tail->next);

	/* nh_destroy() will clean stored DUs, so no need to clean up
	 * here (valgrind is happy) */
}

static void test_add_anon_rx_pdu(void)
{
	struct channel *du1 = chan_create(nh, (unsigned char *)"01:00:e5:01:02:42", 43, CLASS_B, 8, INT_50HZ);
	struct channel *du2 = chan_create(nh, (unsigned char *)"01:00:e5:01:02:42", 43, CLASS_B, 8, INT_50HZ);
	struct channel *du3 = chan_create(nh, (unsigned char *)"01:00:e5:01:02:42", 43, CLASS_B, 8, INT_50HZ);
	struct channel *du4 = chan_create(nh, (unsigned char *)"01:00:e5:01:02:42", 43, CLASS_B, 8, INT_50HZ);

	TEST_ASSERT(nh_get_num_rx(nh) == 0);
	TEST_ASSERT(nh_add_rx(NULL, du1) == -EINVAL);
	TEST_ASSERT(nh_add_rx(nh, du1) == 0);
	TEST_ASSERT(nh_get_num_rx(nh) == 1);
	TEST_ASSERT(nh_add_rx(nh, du2) == 0);
	TEST_ASSERT(nh->du_rx_tail == du2);
	TEST_ASSERT(nh_add_rx(nh, du3) == 0);
	TEST_ASSERT(nh->du_rx_tail == du3);
	TEST_ASSERT(nh_add_rx(nh, du4) == 0);
	TEST_ASSERT(nh_get_num_rx(nh) == 4);
	TEST_ASSERT(nh->du_rx_head == du1);
	TEST_ASSERT(nh->du_rx_tail == du4);

	/* nh_destroy() will clean stored DUs, so no need to clean up
	 * here (valgrind is happy) */
}

static void test_pdu_send_now(void)
{
	uint64_t data = 0xa0a0a0a0;
	NETCHAN_TX(test1);
	TEST_ASSERT(test1_du->pdu.seqnr == 0xff);
	TEST_ASSERT(test1_du->pdu.avtp_timestamp == htonl(0));

	pdu_send_now(test1_du, &data);
	TEST_ASSERT(test1_du > 0);
	TEST_ASSERT(test1_du->pdu.seqnr == 0);
}


int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	nc_set_nic("lo");

	RUN_TEST(test_chan_create);
	RUN_TEST(test_invalid_interval);
	RUN_TEST(test_invalid_payload_size);
	RUN_TEST(test_payload_size_interval_combo);
	RUN_TEST(test_pdu_update);
	RUN_TEST(test_pdu_get_payload);
	RUN_TEST(test_pdu_send);
	RUN_TEST(test_create_standalone);
	RUN_TEST(test_add_anon_pdu);
	RUN_TEST(test_add_anon_rx_pdu);
	RUN_TEST(test_pdu_send_now);
	return UNITY_END();
}
