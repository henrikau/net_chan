#include <stdio.h>
#include <unistd.h>
#include <timedc_avtp.h>
#include <timedc_args.h>
#include "net_fifo.h"

int main(int argc, char *argv[])
{
	GET_ARGS();
	NETFIFO_RX(mcast42);
	while (1) {
		uint64_t d = 0;
		READ(mcast42, &d);
		printf("Counter received! -> %lu\n", d);
	}

	CLEANUP();
	return 0;
}
