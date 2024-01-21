/*
 * Copyright 2023 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */

/*
 * Simple N:N channel tester
 *
 * Create a mesh of N entities each with N outgoing (and incoming)
 * channels. Provided local clocks are in sync, the generated logfiles
 * can be used to determine total delay for the mesh afterwards.
 *
 * Intended use: stress test a networks SRP capabilities.
 */
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <netchan.h>
#include <argp.h>
#include <pthread.h>
#include <logger.h>


#define HZ_100  (10 * NS_IN_MS)
#define ENSEMBLE_SIZE_LIMIT 14
#define ENTITY_READY_MAGIC 0xdeadbeef
#define ID_BASE_OFFSET 42 /* avoid streamID 0 */

static char logfile[129] = {0};
static char nic[IFNAMSIZ] = "eth0";
static bool remote_ready[ENSEMBLE_SIZE_LIMIT] = {0};
static int ensemble_size = 3;
static int ensemble_idx = 0;
static int iterations = -1;
static bool verbose = false;
static bool do_srp = false;
static bool running = false;
static int period_ns = HZ_100;

/* This example has slightly different needs than the plain example, so
 * we add our own parser.
 */
static struct argp_option options[] = {
       {"log_ts"    , 'L', "LOGFILE", 0, "Log timestamps to logfile, idx and .csv will be appended"},
       {"iterations", 'i', "M"      , 0, "Run for M iterations (<= 0 sets continous mode)"},
       {"nic"       , 'n', "NIC"    , 0, "Network Interface" },
       {"size"      , 's', "N"      , 0, "Size of ensemble, will create N:N connections"},
       {"idx"       , 'I', "IDX"    , 0, "Unique index of agent in ensemble."},
       {"srp"       , 'S', NULL     , 0, "Enable stream reservation (SRP), including MMRP and MVRP"},
       {"period_us" , 'P', "PERIOD" , 0, "Period (in us) between each frame, default 10ms (100 Hz)"},
       {"verbose"    ,'v', NULL     , 0, "Verbose mode (noisy)"},
       { 0 }
};

error_t parser(int key, char *arg, struct argp_state *state);

static struct argp argp __attribute__((unused)) = {
	.options = options,
	.parser = parser};

error_t parser(int key, char *arg, struct argp_state *state)
{
      switch (key) {
      case 'L':
	      strncpy(logfile, arg, sizeof(logfile)-15); /* account for "-<num>.csv" */
	      break;
      case 'i':
	      iterations = atoi(arg);
	      break;
      case 'n':
	      strncpy(nic, arg, IFNAMSIZ-1);
	      break;
      case 'v':
	      verbose = true;
	      break;
      case 's':
	      ensemble_size = atoi(arg);
	      break;
      case 'P':
      {
	      int tmp = atoi(arg) * 1000;
	      if (tmp <= 1*NS_IN_SEC && tmp >= 125 * NS_IN_US)
		      period_ns = tmp;
      }
	      break;
      case 'S':
	      do_srp = true;
	      break;
      case 'I':
	      ensemble_idx = atoi(arg);
	      break;
      }

       return 0;
}

void print_conf(void)
{
	printf("ManyChan configuration settings:\n");
	printf("Using NIC:\t%s\n", nic);
	printf("Ensemble size:\t%d (%d connections)\n", ensemble_size, ensemble_size * ensemble_size);
	printf("Ensemble idx:\t%d\n", ensemble_idx);
	printf("Iterations\t%d %s\n", iterations, iterations <= 0 ? "(Continous)" : "");
	printf("Period:\t\t%d us (%.3f Hz)\n", period_ns/1000, 1e9/period_ns);
	printf("Logging to:\t%s\n", strlen(logfile) > 0 ? logfile : "Logging disabled");
	printf("Verbose mode:\t%s\n", verbose ? "Enabled" : "Disabled");
	printf("Use SRP:\t%s\n", do_srp ? "Enabled" : "Disabled");
}

bool valid_settings(void)
{
	if (ensemble_idx >= ensemble_size) {
		fprintf(stderr, "Ensemble idx (%d) outside size of ensemble (%d)!\n",
			ensemble_idx, ensemble_size);
		return false;
	}
	if (ensemble_size > ENSEMBLE_SIZE_LIMIT) {
		fprintf(stderr, "Ensemble size (%d) too large, we currently cannot support %d connections. Upper limit is %d (%d connections)\n",
			ensemble_size, ensemble_size*ensemble_size,
			ENSEMBLE_SIZE_LIMIT, ENSEMBLE_SIZE_LIMIT*ENSEMBLE_SIZE_LIMIT);
		return false;
	}
	return true;
}


struct channel_attrs base = {
	.dst       = {0x01, 0x00, 0x5E, 0x01, 0x01, 0x00},
	.stream_id = 0,
	.sc	   = CLASS_A,
	.size      =  sizeof(uint64_t),
	.interval_ns = HZ_100,
};


void sighandler(int signum)
{
	fflush(stdout);
	running = 0;
}

void * rx_drain(void *data)
{
	if (!data)
		return NULL;

	struct channel *rx = (struct channel *)data;

	/* Wait for Rx channel to become ready */
	while (chan_ready_timedwait(rx, 100 * NS_IN_MS) != 0 && running) ;

	while (running) {
		uint64_t data = 0;
		int res = chan_read(rx, &data);
		if (res < 0) {
			fprintf(stderr, "\n%s() [%lu]: Reading from channel failed, aborting (%d)\n", __func__, pthread_self(), res);
			running = false;
		}

		if (data == -1) {
			printf("\n%s() [%lu]: Got closedown signal, shutting down\n", __func__, pthread_self());
			running = false;
			continue;
		}
		if (data == ENTITY_READY_MAGIC) {
			/* from stream_id, find idx in array
			 *
			 * stream_id = ensemble_idx + idx * ensemble_size;
			 * idx = (stream_id - ensemble_idx) / ensemble_size
			 * % should be 0
			 */
			int id = (rx->sidw.s64 - ensemble_idx - ID_BASE_OFFSET)/ensemble_size;
			if (id > ENSEMBLE_SIZE_LIMIT) {
				printf("\n%s(): Error calculating idx (%d) from streamID (%" PRId64 ")\n",
					__func__, id, rx->sidw.s64);
			} else if (!remote_ready[id]) {
				remote_ready[id] = true;
				printf("\n%s(): id=%d signalled ready. ", __func__, id);
				for (int idx = 0; idx < ensemble_size; idx++)
					printf("%d=%s ", idx, remote_ready[idx] ? "R" : "W");
				printf("\n");
			}
		}
	}
	printf("%s(): thread %lu stopping\n", __func__, pthread_self());
	running = false;
	return NULL;
}

bool all_ready(void)
{
	bool res = true;
	for (int idx = 0; idx < ensemble_size; idx++) {
		if (!remote_ready[idx])
			res = false;
	}
	return res;
}

int main(int argc, char *argv[])
{
	argp_parse(&argp, argc, argv, 0, NULL, NULL);
	if (!valid_settings()) {
		fprintf(stderr, "Invalid settings, please fix\n");
		return -1;
	}
	/* If we log to file, append idx so we can compare files more easily */
	if (strlen(logfile) > 0)
		snprintf(logfile+strlen(logfile), sizeof(logfile), "-%u.csv", (unsigned short)ensemble_idx);

	print_conf();
	base.interval_ns = period_ns;

	struct nethandler *nh = nh_create_init(nic, 2*ensemble_size + 1, strlen(logfile)>0 ? logfile : NULL);
	nh_set_verbose(nh, verbose);
	nh_set_srp(nh, do_srp);

	struct channel *tx[ENSEMBLE_SIZE_LIMIT];
	pthread_t listeners[ENSEMBLE_SIZE_LIMIT];

	/* Create Channels, finding StreamID and address
	 *
	 *
	 *        +-----+      #1     +-----+
	 *    /-- |     |  ---------> |     |--\
	 * #0 |   |  0  |             |  1  |  | #3
	 *    \-> |     | <---------- |     |<-/
	 *        +-----+      #2     +-----+
	 *
	 * To avoid StreamID == 0; we add a base-offset
	 *
	 * Tx = idx + self * sz + ID_BASE_OFFSET
	 * Rx = self + idx * sz + ID_BASE_OFFSET
	 */
	for (int idx = 0; idx < ensemble_size; idx++) {
		int id = idx + ensemble_idx * ensemble_size + ID_BASE_OFFSET; /* avoid streamID 0 */
		base.dst[5] = id;
		base.stream_id = id;
		tx[idx] = chan_create_tx(nh, &base, true);
	}

	/* Prepare for starting Rx threads */
	running = true;

	/* create all relevenat Rx channel idx
	 * Rx = self + idx*sz
	 */
	for (int idx = 0; idx < ensemble_size; idx++) {
		int id = ensemble_idx + idx * ensemble_size + ID_BASE_OFFSET;
		base.dst[5] = id;
		base.stream_id = id;
		pthread_create(&listeners[idx], NULL, rx_drain, chan_create_rx(nh, &base, true));
	}

	nh_list_active_channels(nh);

	/* wait for all Tx channels to become ready */
	int ready_ctr = 0;
	do {
		ready_ctr = 0;
		for (int idx = 0; idx < ensemble_size; idx++) {
			if (chan_ready_timedwait(tx[idx], 100 * NS_IN_MS) == 0)
				ready_ctr++;
		}
	} while (ready_ctr != ensemble_size);

	printf("%s() all %d Tx channels ready, syncing ensemble to start.\n", __func__, ensemble_size);

	/* Signal ready, wait for all in ensemble to signal same */
	struct periodic_timer *pt = pt_init(0, period_ns, CLOCK_TAI);
	uint64_t data = ENTITY_READY_MAGIC;
	while (running) {
		pt_next_cycle(pt);
		for (int idx = 0; idx < ensemble_size; idx++)
			chan_send_now(tx[idx], &data);
		if (all_ready()) {
			break;
		}
		printf("-"); fflush(stdout);
	}
	printf("%s(): All ready, starting man capture\n", __func__);
	log_reset(nh->logger);

	/* Sending periodic signals */
	while (iterations != 0 && running) {
		iterations--;
		data = iterations;
		pt_next_cycle(pt);
		for (int idx = 0; idx < ensemble_size; idx++)
			chan_send_now(tx[idx], &data);
		printf("."); fflush(stdout);

	}
	printf("%s(): Loop done, signalling all threads just to be sure\n", __func__);
	/* Stop all listen workers */
	data = -1;
	for (int idx = 0; idx < ensemble_size; idx++)
		chan_send_now(tx[idx], &data);
	running = false;

	/* Just free the array, the channels are registred with the
	 * nethandler and will be properly closed down when it is
	 * destroyed
	 */
	nh_destroy(&nh);

	return 0;
}
