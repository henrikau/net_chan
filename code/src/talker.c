#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include <timedc_avtp.h>
#include "net_fifo.h"

int main(int argc, char *argv[])
{
	GET_ARGS();

	struct timeval tv;

	NETFIFO_TX(mcast42);

	for (int i = 0; i < 10; i++) {
		gettimeofday(&tv, NULL);
		uint64_t ts = tv.tv_sec * 1e6 + tv.tv_usec;

		WRITE(mcast42, &ts);
		printf("%d: written\n", i);
		usleep(100000);
	}
	printf("done\n");
	return 0;
}
