#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/ether.h>
#include <timedc_avtp.h>
#include "net_fifo.h"

int main(int argc, char *argv[])
{
	GET_ARGS();

	struct timeval tv;
	int c = NETFIFO_TX_CREATE("bcast");
	for (int i = 0; i < 10; i++) {
		gettimeofday(&tv, NULL);
		uint64_t ts = tv.tv_sec * 1e6 + tv.tv_usec;

		write(c, &ts, 8);
		printf("%d: written\n", i);
		usleep(100000);
	}
	printf("done\n");
	return 0;
}
