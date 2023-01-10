#include <stdio.h>
#include "unity.h"
#include "helper.h"
#include "test_net_fifo.h"
#include <netchan.h>
#include <pthread.h>

void setUp(void)
{
	nf_set_nic("lo");
}

void tearDown(void)
{
	nh_destroy_standalone();
}

static void *worker(void *d)
{
	helper_recv(2, d, 2, "lo");
	return NULL;
}

static void test_macro_nf_write(void)
{
	pthread_t tid = 0;
	uint64_t rxd = 0;

	pthread_create(&tid, NULL, worker, &rxd);

	/* Make sure worker manages to start, more than 1ms typically
	 * indicates a sched_switch()
	 */
	usleep(10000);

	uint64_t data = 0xa0a0a0a0;

	NETCHAN_TX(macro11);
	WRITE(macro11, &data);

	/* wait for frame to be received and processed by worker */
	pthread_join(tid, NULL);

	TEST_ASSERT(rxd == data);
}

static void * txworker(void *d)
{
	uint64_t *dtx = (uint64_t *)d;

	/* force a sched switch */
	usleep(10000);
	helper_send_8byte(2, *dtx);
	return NULL;
}

static void test_macro_nf_read(void)
{
	NETCHAN_RX(macro11);

	uint64_t rxd = 0;
	uint64_t txd = 0xdeadbeef;
	pthread_t tid = 0;
	/* thread will wait for 10ms to allow us to get ready and block on pipe */
	pthread_create(&tid, NULL, txworker, &txd);

	READ(macro11, &rxd);
	TEST_ASSERT(txd == rxd);

	pthread_join(tid, NULL);

}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();

	RUN_TEST(test_macro_nf_write);
	RUN_TEST(test_macro_nf_read);

	return UNITY_END();
}
