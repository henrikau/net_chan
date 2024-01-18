#include <stdio.h>
#include "unity.h"

#include <srp/mrp_client.h>

#include <stdbool.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <netinet/ether.h>

struct mrp_ctx txctx;
bool rx_running = false;
char *rx_buffer = NULL;
char *rx_payload = NULL;
#define BUFFER_SIZE 1522
pthread_t tid   = -1;

/* Receiver helper. Used to track the messages mrp_client sends to mrpd
 * (listening on 127.0.0.1:7500)
 *
 * This is a bloody mess, but in order to properly receive *all* frames
 * sent via lo, the socket had t be opened in raw-mode. This means we
 * have to parse the header from ether all the way up to UDP w/payload
 *
 * Once a UDP message to port 7500 is received, rx_payload is updated to
 * point to this and then the loop exits.
 *
 * Relevant tests:
 *    TEST_ASSERT_NOT_NULL(rx_payload);
 *    TEST_ASSERT_EQUAL_STRING_MESSAGE(rx_payload, <expected content>)
 */
void * receiver(void *data)
{
	if (!rx_buffer || !rx_running || !rx_payload)
		return NULL;
	memset(rx_buffer, 0, BUFFER_SIZE);
	memset(rx_payload, 0, BUFFER_SIZE);

	/* Open  */
	int sock = -1;
	sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sock < 0)
		return NULL;

	/* Set a short timeout in case we're not sending as expected,
	 * let test return.
	 */
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) == -1)
		printf("%s() Failed setting rx timeout on socket: %s\n", __func__, strerror(errno));

	struct ifreq req;
	snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", "lo");
	if (ioctl(sock, SIOCGIFINDEX, &req) == -1) {
		printf("%s(): Could not get interface index for %s: %s\n", __func__, "lo", strerror(errno));
		return NULL;
	}

	if (ioctl(sock, SIOCGIFFLAGS, &req) == -1) {
		perror("Failed retrieveing flags for lo");
		return NULL;
	}
	req.ifr_flags |= IFF_PROMISC;
	if (ioctl(sock, SIOCSIFFLAGS, &req) == -1) {
		perror("Failed placing lo in promiscuous mode, will not receive incoming data (tests may fail)");
		return NULL;
	}

	while (rx_running) {
		int rxsz = recv(sock, rx_buffer, BUFFER_SIZE, 0);
		if (rxsz == -1) {
			if (errno == 11) {
				printf("%s() timeout\n", __func__);
				continue;
			}
			printf("%s() unexpected error (%d) from recv(), closing Rx-thread. %s\n",
				__func__, errno, strerror(errno));
			return NULL;
		}
		struct ether_header *hdr  = (struct ether_header *)rx_buffer;
		struct iphdr *iphdr = (struct iphdr *)((void *)hdr + sizeof(*hdr));
		struct udphdr *udphdr = (struct udphdr *)((void *)iphdr + sizeof(*iphdr));

		if (rxsz < (sizeof(*hdr) + sizeof(*iphdr) + sizeof(*udphdr)))
			continue;

		/* outgoing msg to mrpd, grab a copy */
		if (ntohs(udphdr->dest) == MRPD_PORT_DEFAULT) {
			memcpy(rx_payload, (char *)udphdr + sizeof(*udphdr), BUFFER_SIZE);
		} else if (ntohs(udphdr->source) == MRPD_PORT_DEFAULT) {
			/* Experience has showed that a lot of different
			 * messages from the network can be returned
			 * from mrpd, so writing a small, simple test is
			 * not easy (i.e. we don't control the
			 * enviornment sufficiently).*/
		}
	}

	return NULL;
}

int start_rx(void)
{
	if (!rx_running || tid != -1 || !rx_buffer) {
		printf("%s(): invalid settings (%s, %p, %ld), aborting startup\n",
			__func__, rx_running ? "true" : "false", rx_buffer, tid);
		return -1;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);

	pthread_attr_setinheritsched (&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_RR);
	struct sched_param params;
	params.sched_priority = 10;
	pthread_attr_setschedparam(&attr, &params);

	rx_running = true;
	if (pthread_create(&tid, &attr, receiver, rx_buffer) == -1) {
		tid = -1;
		rx_running = false;
		return -1;
	}
	pthread_attr_destroy(&attr);

	return 0;
}

void setUp(void)
{
	/* Not pretty, but sleep for 100ms to make sure no outstanding
	 * messages to mrpd is left in the network from last round.
	 */
	usleep(100000);
	tid = -1;
	rx_buffer = malloc(BUFFER_SIZE);
	rx_payload = malloc(BUFFER_SIZE);
	if (rx_buffer && rx_payload) {
		rx_running = true;
		start_rx();
	}
	mrp_ctx_init(&txctx, false);
	mrp_connect(&txctx);
}

void tearDown(void)
{
	if (rx_running) {
		rx_running = false;
		if (tid != -1) {
			pthread_join(tid, NULL);
			tid = -1;
		}
	}
	txctx.halt_tx = 1;
	if (rx_buffer) {
		free(rx_buffer);
		rx_buffer = NULL;
	}
	if (rx_payload) {
		free(rx_payload);
		rx_payload = NULL;
	}
}

static void wait_for_rx_payload(void)
{
	for (int retries = 10; retries > 0; retries--) {
		if (strlen(rx_payload) > 0)
			break;
		usleep(5000);
	}
}


static void test_mrp_init(void)
{
	struct mrp_ctx ctx;
	mrp_ctx_init(&ctx, false);
	TEST_ASSERT(ctx.listeners == 0);
	TEST_ASSERT(ctx.control_socket == -1);
}
static void test_mrp_connect(void)
{
	struct mrp_ctx ctx;
	mrp_ctx_init(&ctx, false);
	TEST_ASSERT(mrp_connect(&ctx) == 0);
	TEST_ASSERT(ctx.halt_tx == 0);
	TEST_ASSERT(mrp_disconnect(&ctx));
	ctx.halt_tx = 1;
}

/* Do explicit setup for each test */
static void test_mrp_send_msg(void)
{
	struct mrp_ctx ctx;
	mrp_ctx_init(&ctx, false);
	mrp_connect(&ctx);

	TEST_ASSERT(ctx.halt_tx == 0);

	char msg[] = "FOOBAR                               ";
	size_t res = mrp_send_msg(msg, strlen(msg)+1, ctx.control_socket);
	TEST_ASSERT(res == strlen(msg)+1);

	/* wait for thread to return, payload should be updated. If not,
	 * it will time out and test will fail.
	 */
	usleep(1000);
	rx_running = false;
	pthread_join(tid, NULL);

	TEST_ASSERT_NOT_NULL(rx_payload);

	/* Verify that expected message was sent and unknown command
	 * returned from mrpd
	 */
	TEST_ASSERT_EQUAL_STRING_MESSAGE(msg, rx_payload, "Unknown *outgoing* message, did send_msg() send multiple?");
}

static void test_mrp_reg_domain(void)
{
	struct mrp_domain_attr class_a = {
		.priority = 3,
		.vid = 2,
		.id = 1337,
	};

	TEST_ASSERT(mrp_register_domain(&class_a, &txctx) > 0);
	wait_for_rx_payload();
	TEST_ASSERT(strlen(rx_payload) > 10);

	/* S+D: join domain
	 * C: stream id
	 * P: Priority
	 * V: Vlan ID
	 */
	TEST_ASSERT_EQUAL_STRING_MESSAGE("S+D:C=1337,P=3,V=0002", rx_payload, "Unexpected outgoing payload");
}

static void test_mrp_join_vlan(void)
{
	struct mrp_domain_attr class_a = {
		.priority = 3,
		.vid = 2,
		.id = 1337,
	};
	TEST_ASSERT(mrp_register_domain(&class_a, &txctx) > 0);
	wait_for_rx_payload();
	TEST_ASSERT_EQUAL_STRING_MESSAGE("S+D:C=1337,P=3,V=0002", rx_payload, "Unexpected outgoing mrp_register_domain payload");

	TEST_ASSERT(mrp_join_vlan(&class_a, &txctx) > 0);
	wait_for_rx_payload();
	const char *expected = "V++:I=0002\n";
	rx_payload[strlen(expected)] = 0x00;
	TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, rx_payload, "Unexpected outgoing payload");
}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	RUN_TEST(test_mrp_init);
	RUN_TEST(test_mrp_connect);
	RUN_TEST(test_mrp_send_msg);
	RUN_TEST(test_mrp_reg_domain);
	RUN_TEST(test_mrp_join_vlan);
	return UNITY_END();
}
