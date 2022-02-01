#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <timedc_avtp.h>
#include <timedc_args.h>

#include "net_fifo.h"
static int running = 0;

void sighandler(int signum)
{
	printf("%s(): Got signal (%d), closing\n", __func__, signum);
	fflush(stdout);
	running = 0;
}

int main(int argc, char *argv[])
{
	GET_ARGS();

	NETFIFO_TX(mcast42);
	running = 1;

	signal(SIGINT, sighandler);
	for (uint64_t i = 0; i < 100000 && running; i++) {
		WRITE(mcast42, &i);
		printf("%lu: written\n", i);
		usleep(100000);
	}

	CLEANUP();
	return 0;
}
