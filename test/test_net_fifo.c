#include <stdio.h>
#include "unity.h"
#include "helper.h"
#include "test_net_fifo.h"
#include <netchan.h>
/*
 * include c directly (need access to internals) as common hides
 * internals (and we want to produce a single file for ktc later)
 */
#include "../src/netchan.c"

void tearDown(void)
{
	if (_nh)
		nh_destroy(&_nh);
}

static void test_arr_size(void)
{
	TEST_ASSERT(ARRAY_SIZE(net_fifo_chans) == 3);
}

static void test_arr_idx(void)
{
	TEST_ASSERT(nf_get_chan_idx("test1", net_fifo_chans, ARRAY_SIZE(net_fifo_chans)) == 0);
	TEST_ASSERT(nf_get_chan_idx("test2", net_fifo_chans, nfc_sz) == 1);
	TEST_ASSERT(nf_get_chan_idx("missing", net_fifo_chans, nfc_sz) == -1);

	TEST_ASSERT(NF_CHAN_IDX("test2") == 1);
	TEST_ASSERT(NF_CHAN_IDX("missing") == -1);

	TEST_ASSERT_NULL_MESSAGE(NULL, "Should be null");
	struct net_fifo *nf = NF_GET("missing");
	TEST_ASSERT_NULL_MESSAGE(nf, "nf should be null when name of net_fifo is not available");
	nf = NF_GET("test1");
	TEST_ASSERT_NOT_NULL_MESSAGE(nf, "nf should *not* be null when name ('test1') of net_fifo is available");
	TEST_ASSERT_MESSAGE(strncmp(nf->name, "test1", 5) == 0, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->stream_id == 42, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->size == 8, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->freq == 50, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->dst[2] == 0x5e, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf != &(net_fifo_chans[0]), "Expected copy to be returned, not ref");
	if (nf)
		free(nf);
}

static void test_arr_get_ref(void)
{
	TEST_ASSERT_NULL_MESSAGE(NF_GET_REF("missing"), "should not get ref to missing net_fifo");

	const struct net_fifo *nf = NF_GET_REF("test1");
	TEST_ASSERT_NOT_NULL_MESSAGE(nf, "nf should *not* be null when name ('test1') of net_fifo is available");
	TEST_ASSERT_MESSAGE(nf == &(net_fifo_chans[0]), "Expected ref, not copy!");

	TEST_ASSERT_MESSAGE(strncmp(nf->name, "test1", 5) == 0, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->stream_id == 42, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->size == 8, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->freq == 50, "wrong net_fifo returned");
	TEST_ASSERT_MESSAGE(nf->dst[2] == 0x5e, "wrong net_fifo returned");
}

static void test_create_netfifo_tx(void)
{
	/* Create pipe, datasize 8, streamID 42, first multicast address */
	int w = NETFIFO_TX_CREATE("missing");
	TEST_ASSERT(w == -1);

	w = NETFIFO_TX_CREATE("test1");
	TEST_ASSERT(w != -1);
	uint64_t val = 0xaa00aa00;

	usleep(1000);
	write(w, &val, 8);

	/* Need a 10ms sleep at the end to let the worker thread grab the data */
	usleep(10000);
	TEST_ASSERT(_nh->du_tx_tail->payload_size == 8);
	uint64_t *data = (uint64_t *)&(_nh->du_tx_tail->payload[0]);
	TEST_ASSERT(*data == val);
}

struct tg_container {
	bool received_ok;
	int nf_idx;
	unsigned char *expected_data;
	struct netchan_avtp *source_du;
	char buffer[2048];
};

static bool tg_runner;
static void * test_grabber(void *data)
{
	struct tg_container *tgc = (struct tg_container *)data;
	if (!tgc)
		return NULL;

	/*
	 * Create a listener socket (promiscous mode), spawn a listener
	 * and verify that data is correctly sent
	 */
	int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_TSN));
	if (sock < 0) {
		perror("Failed opening TSN-socket\n");
		return NULL;
	}

	struct ifreq ifr = {0};
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", nf_nic);

	int res = ioctl(sock, SIOCGIFINDEX, &ifr);
	TEST_ASSERT(res >= 0);

	struct packet_mreq mr;
	memset(&mr, 0, sizeof(mr));
	mr.mr_ifindex = ifr.ifr_ifindex;
	mr.mr_type = PACKET_MR_PROMISC;
	res = setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr));

	/* Set a short timeout in case we're not sending as expected, let test return */
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

	/* Ready receive buffer */
	struct ether_header *hdr = (struct ether_header *)tgc->buffer;
	struct avtpdu_cshdr *cshdr = (struct avtpdu_cshdr *)(&tgc->buffer[0] + sizeof(*hdr));

	while (tg_runner) {
		int r = recv(sock, hdr, 1522, 0);
		if (r < 0) {
			perror("Failed reading from socket");
			tg_runner = false;
			continue;
		}
		if (be64toh(cshdr->stream_id) == net_fifo_chans[tgc->nf_idx].stream_id) {
			tgc->received_ok = true;
			tg_runner = false;
			continue;
		}
	}

	return NULL;
}

static void test_create_netfifo_tx_send(void)
{
	uint64_t data = 0xdeadbeef;
	int w = NETFIFO_TX_CREATE("test1");
	TEST_ASSERT(w != -1);
	pthread_t tg_tid;
	tg_runner = true;

	struct tg_container tgc = {
		.received_ok = false,
		.nf_idx = 0,
		.expected_data = (unsigned char *)&data,
		.source_du = _nh->du_tx_tail
	};
	memset(&tgc.buffer, 0, 2048);

	pthread_create(&tg_tid, NULL, test_grabber, &tgc);
	usleep(5000);

	/* write to pipe, this should trigger data  */
	write(w, &data, 8);
	usleep(5000);
	tg_runner = false;
	pthread_join(tg_tid, NULL);

	/* Verify received data */
	TEST_ASSERT(tgc.received_ok);

	struct ether_header *hdr = (struct ether_header *)tgc.buffer;
	struct avtpdu_cshdr *cshdr = (struct avtpdu_cshdr *)(&tgc.buffer[0] + sizeof(*hdr));
	TEST_ASSERT(cshdr->subtype == AVTP_SUBTYPE_TIMEDC);
	TEST_ASSERT(be64toh(cshdr->stream_id) == 42);
	TEST_ASSERT(hdr->ether_dhost[0] == net_fifo_chans[0].dst[0]);
	TEST_ASSERT(hdr->ether_dhost[1] == net_fifo_chans[0].dst[1]);
	TEST_ASSERT(hdr->ether_dhost[2] == net_fifo_chans[0].dst[2]);
	TEST_ASSERT(hdr->ether_dhost[3] == net_fifo_chans[0].dst[3]);
	TEST_ASSERT(hdr->ether_dhost[4] == net_fifo_chans[0].dst[4]);
	TEST_ASSERT(hdr->ether_dhost[5] == net_fifo_chans[0].dst[5]);
}

static void test_create_netfifo_rx_pipe_ok(void)
{
	/* NETFIFO_RX() tested in test_pdu */
	int r = nf_rx_create("missing", net_fifo_chans, nfc_sz);
	TEST_ASSERT(r==-1);
	r = nf_rx_create("test1", net_fifo_chans, nfc_sz);
	TEST_ASSERT(r>=0);

	uint64_t data = 0xdeadbeef;
	write(_nh->du_rx_tail->fd_w, &data, 8);

	uint64_t res = 0;
	read(_nh->du_rx_tail->fd_r, &res, 8);
	TEST_ASSERT(data == res);
}

static void test_create_netfifo_rx_send_ok(void)
{
	/* Create listening socket and pipe-pair */
	int r = nf_rx_create("test1", net_fifo_chans, nfc_sz);
	TEST_ASSERT(r > 0);

	uint64_t data = 0xdeadbeef;
	int txsz = helper_send_8byte(0, data);
	TEST_ASSERT(txsz == 8);

	struct pipe_meta *pm = malloc(sizeof(struct pipe_meta) + sizeof(uint64_t));
	read(r, pm, sizeof(*pm) + sizeof(uint64_t));
	uint64_t *received = (uint64_t *)&pm->payload[0];
	TEST_ASSERT(*received == data);
	free(pm);
}

static void test_create_netfifo_rx_recv(void)
{
	int wrong = NETFIFO_RX_CREATE("missing");
	TEST_ASSERT(wrong==-1);
	int rxchan = NETFIFO_RX_CREATE("test2");
	TEST_ASSERT(rxchan != -1);

	uint64_t data = 0x0a0a0a0a;
	int txsz = helper_send_8byte(1, data);
	TEST_ASSERT(txsz == 8);


	struct pipe_meta *pm = malloc(sizeof(struct pipe_meta) + sizeof(uint64_t));
	int rsz = read(rxchan, pm, sizeof(*pm) + sizeof(uint64_t));
	uint64_t *r = (uint64_t *)&pm->payload[0];
	TEST_ASSERT(rsz == (sizeof(struct pipe_meta) + sizeof(uint64_t)));
	TEST_ASSERT(*r == data);

	free(pm);
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	nf_set_nic("lo");

	RUN_TEST(test_arr_size);
	RUN_TEST(test_arr_idx);
	RUN_TEST(test_arr_get_ref);
	RUN_TEST(test_create_netfifo_tx);
	RUN_TEST(test_create_netfifo_tx_send);
	RUN_TEST(test_create_netfifo_rx_pipe_ok);
	RUN_TEST(test_create_netfifo_rx_send_ok);
	RUN_TEST(test_create_netfifo_rx_recv);

	return UNITY_END();
}
