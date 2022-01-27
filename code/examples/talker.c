#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <timedc_avtp.h>
#include <timedc_args.h>

#include "net_fifo.h"

int main(int argc, char *argv[])
{
	GET_ARGS();

	NETFIFO_TX(mcast42);

	for (uint64_t i = 0; i < 10; i++) {
		WRITE(mcast42, &i);
		printf("%lu: written\n", i);
		usleep(100000);
	}

	CLEANUP();
	return 0;
}
