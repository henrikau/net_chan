#include <stdio.h>
#include <unistd.h>
#include <timedc_avtp.h>
#include "net_fifo.h"

int main(int argc, char *argv[])
{
	GET_ARGS();

	int c = NETFIFO_RX_CREATE("bcast");

	printf("sleeping, waiting for Rx to settle\n");
	usleep(1000000);

	while (1) {
		uint64_t ts = 0;
		read(c, &ts, 8);
		printf("Timestamp received! -> %f\n", ts / 1e6);
	}

	printf("Done\n");
	return 0;
}
