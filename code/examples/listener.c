#include <stdio.h>
#include <unistd.h>
#include <timedc_avtp.h>
#include <timedc_args.h>

#include "manifest.h"

int main(int argc, char *argv[])
{
	GET_ARGS();
	NETFIFO_RX(mcast42);
	while (1) {
		uint64_t d = 0;
		READ_WAIT(mcast42, &d);
		if (!(d%10))
			printf("Counter received! -> %lu\n", d);
	}

	CLEANUP();
	return 0;
}
