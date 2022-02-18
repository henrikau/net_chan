#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <timedc_avtp.h>
#include <timedc_args.h>

#include "manifest.h"
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
	for (uint64_t i = 0; i < 500 && running; i++) {
		WRITE_WAIT(mcast42, &i);
		if (!(i%10))
			printf("%lu: written\n", i);

		/* Take freq and subtract expected wait from class to
		 * get roughly right delay
		 */
		int udel = 1000000 / net_fifo_chans[0].freq - (net_fifo_chans[0].class == CLASS_A ? 2*US_IN_MS : 50*US_IN_MS);
		usleep(udel);
	}

	CLEANUP();
	return 0;
}
