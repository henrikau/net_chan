#include <stdio.h>
#include "unity.h"
#include "test_net_fifo.h"

/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/netchan.c"
#include "../src/netchan_socket.c"

char data17[DATA17SZ] = {0};
struct channel *pdu17;

#define DATA42SZ 8
#define DATA17SZ 32

#define INT42 INT_50HZ
#define INT17 INT_10HZ

char data42[DATA42SZ] = {0};
struct channel *pdu42;
struct net_fifo nfc;

struct nethandler *nh;
struct net_fifo nfc = {
	.dst       = { 0x01, 0x00, 0xe5, 0x01, 0x02, 0x42},
	.stream_id = 18,
	.sc        = CLASS_A,
	.size      = 8,
	.interval_ns = 2 * NS_IN_MS,
	.name      = "invalid_period",
};


void setUp(void)
{
	nh = nh_create_init("lo", 16, NULL);
	nfc.stream_id = 17;
	nfc.size = DATA17SZ;
	nfc.interval_ns = INT17;
	pdu17 = _chan_create(nh, &nfc);

	nfc.stream_id = 42;
	nfc.size = DATA42SZ;
	nfc.interval_ns = INT42;
	pdu42 = _chan_create(nh, &nfc);

	/* Must set all channel attributes before channel can be used */
	pdu17->tx_sock = 3;
	pdu42->tx_sock = 42;

	memset(data42, 0x42, DATA42SZ);
	memset(data17, 0x17, DATA17SZ);
}

void tearDown(void)
{
	chan_destroy(&pdu17);
	chan_destroy(&pdu42);
	nh_destroy(&nh);

	/* remember to destroy _nh when using standalone */
	if (_nh)
		nh_destroy(&_nh);
}

static void test_chan_create(void)
{
	nfc.stream_id = 43;
	nfc.sc = CLASS_B;
	nfc.size = 128;
	nfc.interval_ns = INT_50HZ;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(NULL, &nfc),
				"Cannot create PDU and assign to non-existant nethandler!");

	struct channel *pdu = _chan_create(nh, &nfc);
	TEST_ASSERT(pdu != NULL);
	TEST_ASSERT(pdu->pdu.stream_id == be64toh(43));
	TEST_ASSERT(pdu->payload_size == 128);
	chan_destroy(&pdu);
	TEST_ASSERT(pdu == NULL);
}

static void test_invalid_interval(void)
{
	/* 0 */
	nfc.interval_ns = 0;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(nh, &nfc), "Invalid period (0)");

	/* shorter than minimum time to send smallest possible payload
	 * on link capacity.
	 * For test, we use lo, which sets the capacity to 1 Gbps, i.e 1ns pr bit
	 */
	nfc.interval_ns = 66;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(nh, &nfc), "Invalid period (66 bits)");

	nfc.interval_ns = 66 * 8;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(nh, &nfc), "Invalid period (528 ns (66 bytes on 1 Gbps link)");

	nfc.interval_ns = 66 * 8 - 1;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(nh, &nfc), "Invalid period (527 ns (66 bytes -1 bit on 1 Gbps link)");

	nfc.interval_ns = 74 * 8 + 1;
	TEST_ASSERT_NOT_NULL_MESSAGE(_chan_create(nh, &nfc), "Failed for a valid period (time for 8 bytes payload (+ headers) + 1 bit)");

	/* larger than one hour (why would you need this?) */
	nfc.interval_ns = NS_IN_SEC * 3600;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(nh, &nfc), "Too long period (> 1 hour)");
}

static void test_invalid_payload_size(void)
{
	/* payload 0 */
	nfc.size = 0;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(nh, &nfc), "Invalid size (0 bytes)");

	/* payload -> exceed MTU */
	nfc.size = 2048;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(nh, &nfc), "Invalid size (2k bytes)");
	nfc.size = 1500;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(nh, &nfc), "Invalid size (1500B payload, forgetting AVTP header)");
	nfc.size = 1476;
	nfc.interval_ns = 500 * NS_IN_MS;
	TEST_ASSERT_NOT_NULL_MESSAGE(_chan_create(nh, &nfc), "Just about valid size (1476 B payload, adding AVTP header -> 1500)");
}

static void test_payload_size_interval_combo(void)
{
	/* 1 Gbps needs 1522 ns to send 1476 payload when adding headers and IPG/preamble*/
	nfc.size = 8;
	nfc.interval_ns = 1500;
	TEST_ASSERT_NOT_NULL_MESSAGE(_chan_create(nh, &nfc), "Valid size (8) interval (1500ns)");

	nfc.size = 1576;
	nfc.interval_ns = 1500;
	TEST_ASSERT_NULL_MESSAGE(_chan_create(nh, &nfc), "Invalid size (1476 bytes) and interval (1500ns) combination, > 100% utilization");

	/* TEST_ASSERT_NULL_MESSAGE(_chan_create(nh,(unsigned char *)"01:00:e5:01:02:42", 43, CLASS_A, 1476, 1500), */
	/* 			"Invalid size (1476 bytes) and interval (1500ns) combination, > 100% utilization"); */
}

static void test_chan_update(void)
{
	TEST_ASSERT(chan_update(NULL, 1, NULL) == -ENOMEM);

	/* Test failed update of data and that other fields have not been altered*/
	TEST_ASSERT(pdu42->pdu.stream_id == be64toh(42));
	TEST_ASSERT(pdu42->pdu.stream_id == be64toh(42));

	TEST_ASSERT(chan_update(pdu17, 3, NULL) == -ENOMEM);
	TEST_ASSERT(chan_update(pdu17, 4, data17) == 0);
	TEST_ASSERT(pdu17->pdu.avtp_timestamp == htonl(4));
	TEST_ASSERT(pdu17->pdu.tv == 1);
	TEST_ASSERT(pdu17->payload[0] == 0x17);
	TEST_ASSERT(pdu17->payload[DATA17SZ-1] == 0x17);

	/* Test payload */
	TEST_ASSERT(pdu42->pdu.stream_id == be64toh(42));
	uint64_t val = 0xdeadbeef;
	TEST_ASSERT(chan_update(pdu42, 5, &val) == 0);
	TEST_ASSERT(pdu42->pdu.stream_id == be64toh(42));

	TEST_ASSERT(pdu42->pdu.avtp_timestamp == htonl(5));
	uint64_t *pl = (uint64_t *)pdu42->payload;
	TEST_ASSERT(*pl == 0xdeadbeef);

	TEST_ASSERT(pdu17->pdu.subtype == AVTP_SUBTYPE_NETCHAN);
	TEST_ASSERT(pdu42->pdu.subtype == AVTP_SUBTYPE_NETCHAN);
}

static void test_chan_get_payload(void)
{
	uint64_t val = 0xdeadbeef;
	chan_update(pdu42, 5, &val);
	uint64_t *pl = chan_get_payload(pdu42);
	TEST_ASSERT(*pl == val);
	TEST_ASSERT(chan_get_payload(NULL) == NULL)
}

static void test_chan_send(void)
{
	TEST_ASSERT_MESSAGE(chan_send(NULL, NULL) == -EINVAL, "Null-channel should get -EINVAL");

	uint64_t val = 0xdeadbeef;
	chan_update(pdu42, 5, &val);
	pdu42->tx_sock = -1;
	TEST_ASSERT_MESSAGE(chan_send(pdu42, NULL) == -EINVAL, "Should not be able to send without valid Tx fd");
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
	struct channel *du = _chan_create(nh, &net_fifo_chans[MCAST42]);
	TEST_ASSERT_NOT_NULL(du);
	TEST_ASSERT(nh_add_tx(NULL, du) == -EINVAL);
	TEST_ASSERT(nh_add_tx(nh, NULL) == -EINVAL);
	TEST_ASSERT(nh_add_tx(nh, du) == 0);
	TEST_ASSERT_NOT_NULL(nh->du_tx_head);
	TEST_ASSERT(nh->du_tx_head == du);
	TEST_ASSERT(nh->du_tx_tail == du);
	TEST_ASSERT(nh_get_num_tx(nh) == 1);

	struct channel *du2 = _chan_create(nh, &net_fifo_chans[MCAST43]);
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
	struct channel *du1 = _chan_create(nh, &net_fifo_chans[MCAST43]);
	struct channel *du2 = _chan_create(nh, &net_fifo_chans[MCAST43]);
	struct channel *du3 = _chan_create(nh, &net_fifo_chans[MCAST43]);
	struct channel *du4 = _chan_create(nh, &net_fifo_chans[MCAST43]);

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

static void test_chan_send_now(void)
{
	uint64_t data = 0xa0a0a0a0;
	NETCHAN_TX(test1);
	TEST_ASSERT(test1_du->pdu.seqnr == 0xff);
	TEST_ASSERT(test1_du->pdu.avtp_timestamp == htonl(0));

	chan_send_now(test1_du, &data);
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
	RUN_TEST(test_chan_update);
	RUN_TEST(test_chan_get_payload);
	RUN_TEST(test_chan_send);
	RUN_TEST(test_create_standalone);
	RUN_TEST(test_add_anon_pdu);
	RUN_TEST(test_add_anon_rx_pdu);
	RUN_TEST(test_chan_send_now);
	return UNITY_END();
}
