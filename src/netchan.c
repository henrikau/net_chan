/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <netchan.h>
#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/net_tstamp.h>
#include <netinet/ether.h>

#include <linux/ethtool.h>
#include <linux/sockios.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <netchan_srp_client.h>
#include <logger.h>
#include <tracebuffer.h>

/* #include <terminal.h> */

struct pipe_meta {
	uint64_t ts_rx_ns;
	uint64_t ts_recv_ptp_ns;
	uint32_t avtp_timestamp;
	uint8_t payload[0];
}__attribute__((packed));

/**
 * cb_priv: private data for callbacks
 *
 * Since we are using C, there's not partially evaluated functions
 * (currying) or templating creating custom functions on the go. Instead
 * we store per-callback related stuff in a standardized cb_priv struct
 * and create this automatically when creating new fifos
 *
 * @param sz: size of data to read/write
 * @param fd: filedescriptor to write to/read from
 * @param meta: metadata about the data
 */
struct cb_priv
{
	int sz;
	/* FIFO FD */
	int fd;

	/* meta-info about the stream  */
	struct pipe_meta meta;
};

/**
 * cb_entity: container for callbacks
 *
 * This is used to find the correct callback to process incoming frames.
 */
struct cb_entity {
	uint64_t stream_id;
	void *priv_data;
	int (*cb)(void *priv_data, struct avtpdu_cshdr *du);
};
static struct nethandler *_nh;

static bool do_srp = false;
static bool keep_cstate = false;
static bool use_tracebuffer = false;
static int break_us = -1;
static bool verbose = false;
static char nc_nic[IFNAMSIZ] = {0};
static bool enable_logging = false;
static bool enable_delay_logging = false;
static char nc_logfile[129] = {0};
static char nc_termdevice[129] = {0};
static int nc_hmap_size = 42;

int nc_set_nic(char *nic)
{
	strncpy(nc_nic, nic, IFNAMSIZ-1);
	if (verbose)
		printf("%s(): set nic to %s\n", __func__, nc_nic);
	return 0;
}

void nc_keep_cstate()
{
	keep_cstate = true;
	if (verbose)
		printf("%s(): keep CSTATE (probably bad for RT!)\n", __func__);
}

void nc_set_hmap_size(int sz)
{
	nc_hmap_size = sz;
}
void nc_use_srp(void)
{
	do_srp = true;
}
void nc_use_ftrace(void)
{
	use_tracebuffer = true;
}
void nc_breakval(int b_us)
{
	if (b_us > 0 && b_us < 1000000)
		break_us = b_us;
}

void nc_verbose(void)
{
	verbose = true;
}

void nc_set_logfile(const char *logfile)
{
	strncpy(nc_logfile, logfile, 128);
	enable_logging = true;
	if (verbose) {
		printf("%s(): set logfile to %s\n", __func__, nc_logfile);
	}
}

void nc_use_termtag(const char *devpath)
{
	if (!devpath || strlen(devpath) <= 0)
		return;

	/* basic sanity check
	 * exists?
	 * device?
	 */
	struct stat s;
	if (stat(devpath, &s) == -1) {
		printf("%s() Error stat on %s (%d, %s)\n",
			__func__, devpath, errno, strerror(errno));
		return;
	}

	strncpy(nc_termdevice, devpath, 128);
}
void nc_log_delay(void)
{
	enable_delay_logging = true;
}

int nc_get_chan_idx(char *name, const struct net_fifo *arr, int arr_size)
{
	for (int i = 0; i < arr_size; i++) {
		if (strncmp(name, arr[i].name, 32) == 0)
			return i;
	}
	return -1;
}

struct net_fifo * nc_get_chan(char *name, const struct net_fifo *arr, int arr_size)
{
	if (!name || !arr || arr_size < 1)
		return NULL;
	int idx = nc_get_chan_idx(name, arr, arr_size);
	if (idx == -1)
		return NULL;

	struct net_fifo *nf = malloc(sizeof(*nf));
	if (!nf)
		return NULL;
	*nf = arr[idx];
	return nf;
}

const struct net_fifo * nc_get_chan_ref(char *name, const struct net_fifo *arr, int arr_size)
{
	if (!name || !arr || arr_size < 1)
		return NULL;
	int idx = nc_get_chan_idx(name, arr, arr_size);
	if (idx == -1)
		return NULL;

	return &arr[idx];
}

int nc_rx_create(char *name, struct net_fifo *arr, int arr_size)
{
	struct channel *chan = chan_create_standalone(name, 0, arr, arr_size);
	if (!chan)
		return -1;

	return chan->fd_r;
}

static bool _valid_interval(struct nethandler *nh, uint64_t interval_ns, uint16_t sz)
{
	/* Smallest possible unit to send: no payload, only headers, IPG etc
	 * PREAMBLE+start 7+1
	 * Ether + CRC: 22
	 * AVTP: 24
	 * IPG: 12
	 */
	size_t min_sz = 8 * (sizeof(struct avtpdu_cshdr) + 22 + 12 + 7 + 1);
	size_t tx_sz = min_sz + sz*8;

	int ns_to_tx = min_sz * 1e9 / nh->link_speed;
	if (interval_ns <= ns_to_tx) {
		if (verbose)
			printf("Requested interval too short: %zd, minimum ns_to_tx: %d\n", interval_ns, ns_to_tx);
		return false;
	}
	if (interval_ns >= NS_IN_HOUR)
		return false;

	/* Test utilization */
	if ((tx_sz * 1e9/nh->link_speed) > interval_ns) {
		printf("Cannot send %zu bits (%d) in %lu ns\n", tx_sz, sz, interval_ns);
		return false;
	}
	return true;
}

static bool _valid_size(struct nethandler *nh, uint16_t sz)
{
	if (sz == 0)
		return false;

	if ((sz + 24 + 22) > 1522) {
		if (verbose)
			printf("Requested size too large (%d yields total framesize > MTU of 1522)\n", sz);
		return false;
	}
	return true;
}

struct channel * chan_create(struct nethandler *nh,
			unsigned char *dst,
			uint64_t stream_id,
			enum stream_class sc,
			uint16_t sz,
			uint64_t interval_ns)
{
	/* Validate */
	if (!nh || !dst)
		return NULL;
	if (!_valid_size(nh, sz) || !_valid_interval(nh, interval_ns, sz))
		return NULL;


	struct channel *ch = calloc(1, sizeof(*ch) + sz);
	if (!ch)
		return NULL;

	ch->pdu.subtype = AVTP_SUBTYPE_NETCHAN;
	ch->pdu.stream_id = htobe64(stream_id);
	ch->sidw.s64 = ch->pdu.stream_id;
	ch->pdu.sv = 1;
	ch->pdu.seqnr = 0xff;
	ch->payload_size = sz;

	/* It does not make sense to set the next-ts (socket is not
	 * fully configured) just yet.
	 *
	 * As long as it is 'sufficienty in the past', the first frame
	 * arriving should fly straight through. */
	ch->next_tx_ns = 0;
	ch->interval_ns = interval_ns;

	ch->nh = nh;
	memcpy(ch->dst, dst, ETH_ALEN);

	ch->sidw.s64 = stream_id;
	ch->sc = sc;
	ch->tx_sock = -1;

	/* Set default values, if we run with SRP, it will be updated
	 * once we've connected to mrpd
	 */
	switch (ch->sc) {
	case CLASS_A:
		ch->socket_prio = DEFAULT_CLASS_A_PRIO;
		break;
	case CLASS_B:
		ch->socket_prio = DEFAULT_CLASS_B_PRIO;
		break;
	}

	return ch;
}

struct channel *chan_create_standalone(char *name,
				bool tx_update,
				struct net_fifo *arr,
				int arr_size)
{
	if (!name || !arr || arr_size <= 0) {
		fprintf(stderr, "%s() no name, arr or arr_size sub-zero, aborting\n", __func__);
		return NULL;
	}

	if (verbose)
		printf("%s(): nic: %s\n", __func__, nc_nic);

	int idx = nc_get_chan_idx(name, arr, arr_size);
	if (idx < 0)
		return NULL;

	nh_create_init_standalone();
	if (!_nh)
		return NULL;

	if (verbose) {
		printf("%s(): creating new DU, idx=%d, dst=%s\n",
			__func__, idx,
			ether_ntoa((const struct ether_addr *)arr[idx].dst));
	}

	/*
	 * Missing frequency workaround: add a warning if freq >
	 * class_max_freq
	 */
	double freq = 1e9/(double)arr[idx].interval_ns;
	if (arr[idx].sc == CLASS_A && freq > 8000) {
		fprintf(stderr, "[WARNING]: Class A stream frequency larger than 8kHz, reserved bandwidth will be too low!\n");
	} else if (arr[idx].sc == CLASS_B && freq > 4000) {
		fprintf(stderr, "[WARNING]: Class B stream frequency larger than 4kHz, reserved bandwidth will be too low!\n");
	}

	if (verbose)
		printf("%s(): freq: %f (interval_ns=%lu)\n", __func__, freq, arr[idx].interval_ns);

	struct channel * ch = chan_create(_nh, arr[idx].dst, arr[idx].stream_id, arr[idx].sc, arr[idx].size, arr[idx].interval_ns);
	if (!ch)
		return NULL;

	strncpy(ch->name, name, 32);

	int pfd[2];
	if (pipe(pfd) == -1) {
		chan_destroy(&ch);
		return NULL;
	}
	ch->fd_r = pfd[0];
	ch->fd_w = pfd[1];

	struct ifreq req;
	snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", _nh->ifname);
	int res = ioctl(ch->nh->rx_sock, SIOCGIFINDEX, &req);
	if (res < 0) {
		fprintf(stderr, "%s(): Failed to get interface index for socket %d, %s\n",
			__func__, ch->nh->rx_sock, strerror(errno));
		chan_destroy(&ch);
		return NULL;
	}

	/* SRP common setup */
	if (do_srp)
		nc_srp_client_setup(ch);

	/* if tx, create socket  */
	if (tx_update) {
		ch->tx_sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
		if (ch->tx_sock == -1) {
			fprintf(stderr, "%s(): Failed creating tx-socket for channel (%lu) - %s\n",
				__func__, be64toh(ch->pdu.stream_id), strerror(errno));
			chan_destroy(&ch);
			return NULL;
		}

		/* Set destination address for outgoing traffict this DU */
		struct sockaddr_ll *sk_addr = &ch->sk_addr;
		sk_addr->sll_family = AF_PACKET;
		sk_addr->sll_protocol = htons(ETH_P_TSN);
		sk_addr->sll_halen = ETH_ALEN;
		sk_addr->sll_ifindex = req.ifr_ifindex;
		memcpy(sk_addr->sll_addr, ch->dst, ETH_ALEN);
		if (verbose)
			printf("%s(): sending to %s\n", __func__, ether_ntoa((struct ether_addr *)&ch->dst));

		if (do_srp) {
			if (!nc_srp_client_talker_setup(ch)) {
				chan_destroy(&ch);
				return NULL;
			}

		}
		/* Add ref to internal list for memory mgmt */
		nh_add_tx(ch->nh, ch);
	} else {

		/* if dst is multicast, add membership */
		if (ch->dst[0] == 0x01 && ch->dst[1] == 0x00 && ch->dst[2] == 0x5E) {
			if (verbose)
				printf("%s() receive data on a multicast group, adding membership\n", __func__);

			struct packet_mreq mr;
			memset(&mr, 0, sizeof(mr));
			mr.mr_ifindex = req.ifr_ifindex;
			mr.mr_type = PACKET_MR_PROMISC;
			if (setsockopt(ch->nh->rx_sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1) {
				fprintf(stderr, "%s(): failed setting multicast membership, may not receive frames! => %s\n",
					__func__, strerror(errno));
			}
		}
		/* Rx SRP subscribe */
		if (do_srp) {
			if (!nc_srp_client_listener_setup(ch)) {
				chan_destroy(&ch);
				return NULL;
			}
		}

		/* Add ref to internal list for memory mgmt */
		nh_add_rx(ch->nh, ch);

		/* trigger on incoming DUs and attach a generic callback
		 * and write data into correct pipe.
		 *
		 * data to feed through the pipe, we need to keep track of
		 * metadata such as timestamps etc, so we cannot copy just the
		 * payload, we need additional fields - thus a temp buffer that
		 * follows the pdu.
		 */
		ch->cbp = malloc(sizeof(struct cb_priv) + ch->payload_size);
		if (!ch->cbp) {
			chan_destroy(&ch);
			return NULL;
		}
		ch->cbp->fd = ch->fd_w;
		ch->cbp->sz = ch->payload_size;
		nh_reg_callback(ch->nh, arr[idx].stream_id, ch->cbp, nh_std_cb);

		/* Rx thread ready, wait for talker to start sending
		 *
		 * !!! WARNING: await_talker() BLOCKS !!!
		 */
		if (do_srp) {
			printf("%s(): Awaiting talker\n", __func__);
			fflush(stdout);
			await_talker(ch->ctx);
			send_ready(ch->ctx);
		}
	}

	return ch;
}

void chan_destroy(struct channel **ch)
{
	if (!*ch)
		return;

	if (do_srp)
		nc_srp_client_destroy((*ch));

	if ((*ch)->fd_r >= 0)
		close((*ch)->fd_r);
	if ((*ch)->fd_w >= 0)
		close((*ch)->fd_w);
	if ((*ch)->tx_sock >= 0) {
		close((*ch)->tx_sock);
		(*ch)->tx_sock = -1;
	}
	if ((*ch)->cbp)
		free((*ch)->cbp);

	free(*ch);
	*ch = NULL;
	if (verbose)
		printf("%s(): Channel destroyed\n", __func__);
}

int chan_update(struct channel *ch, uint32_t ts, void *data)
{
	if (!ch)
		return -ENOMEM;

	if (!data)
		return -ENOMEM;
	ch->pdu.seqnr++;
	ch->pdu.avtp_timestamp = htonl(ts);
	ch->pdu.tv = 1;
	ch->pdu.sdl = htons(ch->payload_size);
	memcpy(ch->payload, data, ch->payload_size);
	return 0;
}

int chan_send(struct channel *ch)
{
	if (!ch)
		return -ENOMEM;
	if (!ch->nh)
		return -EINVAL;
	if (ch->tx_sock == -1)
		return -EINVAL;

	int txsz = sendto(ch->tx_sock,
			&ch->pdu,
			sizeof(struct avtpdu_cshdr) + ch->payload_size,
			0,
			(struct sockaddr *) &ch->sk_addr,
			sizeof(ch->sk_addr));
	if (txsz < 0)
		perror("chan_send()");

	return txsz;
}

/*
 * ptp_target_delay_ns: absolute timestamp for PTP time to delay to
 * du: data-unit for netchan internals (need access to PTP fd)
 *
 * ptp_target_delay_ns is adjusted for correct class
 */
int64_t _delay(struct channel *du, uint64_t ptp_target_delay_ns)
{
	/* Calculate delay
	 * - take CLOCK_MONOTONIC ts and current PTP Time, find diff between the 2
	 * - set sleep to ptp_delay ts + monotonic_diff
	 */
	uint64_t now_ptp_ns = get_ptp_ts_ns(du->nh->ptp_fd);
	if (ptp_target_delay_ns < now_ptp_ns)
		return ptp_target_delay_ns - now_ptp_ns;

	/* NOTE: we assume both clocks run with same rate, i.e. that 1
	 * ns on PTP is of same length as 1 ns on CPU
	 */
	uint64_t rel_delay_ns = ptp_target_delay_ns - now_ptp_ns;

	struct timespec ts_cpu = {0}, ts_wakeup = {0};
	if (clock_gettime(CLOCK_MONOTONIC, &ts_cpu) == -1) {
		fprintf(stderr, "%s() FAILED (%d, %s)\n", __func__, errno, strerror(errno));
		return -1;
	}
	ts_cpu.tv_nsec += rel_delay_ns;
	ts_normalize(&ts_cpu);
	uint64_t cpu_target_delay_ns = ts_cpu.tv_sec * NS_IN_SEC + ts_cpu.tv_nsec;

	/* FIXME:
	 * track down why this is almost always ~200us late,
	 * cyclictest 4-8us latency on clock_nanosleep() wakeup error
	 * sudo cyclictest --duration=60 -p 30 -m -n -t 3 -a --policy=rr
	 */
	if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_cpu, NULL) == -1)
		printf("%s(): clock_nanosleep failed (%d, %s)\n", __func__, errno, strerror(errno));

	clock_gettime(CLOCK_MONOTONIC, &ts_wakeup);
	uint64_t cpu_wakeup_ns = ts_wakeup.tv_sec * NS_IN_SEC + ts_wakeup.tv_nsec;
	int64_t error_cpu_ns = cpu_target_delay_ns - cpu_wakeup_ns;

	if (enable_delay_logging)
		log_delay(du->nh->logger, ptp_target_delay_ns, cpu_target_delay_ns, cpu_wakeup_ns);

	if (use_tracebuffer) {
		char tbmsg[128] = {0};
		snprintf(tbmsg, 127, "_delay(), target=%lu, current=%lu, error=%ld (%s)",
			ptp_target_delay_ns,
			cpu_wakeup_ns,
			error_cpu_ns,
			error_cpu_ns < 0 ? "late" : "early");

		tb_tag(du->nh->tb, tbmsg);
	}

	if (verbose) {
		printf("%s(): PTP Target: %lu, actual: %lu, error: %.3f (us) (%s)\n",
			__func__, cpu_target_delay_ns, cpu_wakeup_ns,
			1.0 * error_cpu_ns / 1000,
			error_cpu_ns < 0 ? "late" : "early");
	}

	return error_cpu_ns;
}

int _chan_send_now(struct channel *ch, void *data, bool wait_class_delay)
{
	uint64_t ts_ns = get_ptp_ts_ns(ch->nh->ptp_fd);
	if (chan_update(ch, tai_to_avtp_ns(ts_ns), data)) {
		fprintf(stderr, "%s(): chan_update failed\n", __func__);
		return -1;
	}

	if (verbose)
		printf("%s(): data sent, capture_ts: %lu\n", __func__, ts_ns);
	if (enable_logging)
		log_tx(ch->nh->logger, &ch->pdu, ts_ns, ts_ns);

	int res = chan_send(ch);
	int err_ns = 150000;

	ts_ns += get_class_delay_bound_ns(ch);
	while (wait_class_delay && err_ns > 50000) {
		err_ns = _delay(ch, ts_ns);
	}

	return res;
}

int chan_send_now(struct channel *ch, void *data)
{
	return _chan_send_now(ch, data, false);
}

int chan_send_now_wait(struct channel *ch, void *data)
{
	return _chan_send_now(ch, data, true);
}

int _chan_read(struct channel *ch, void *data, bool read_delay)
{
	size_t rpsz = sizeof(struct pipe_meta) + ch->payload_size;

	int res = read(ch->fd_r, &ch->cbp->meta, rpsz);

	memcpy(data, &ch->cbp->meta.payload, ch->payload_size);

	/* Reconstruct PTP capture timestamp from sender */
	uint64_t lavtp = tai_to_avtp_ns(ch->cbp->meta.ts_recv_ptp_ns);
	if (lavtp < ch->cbp->meta.avtp_timestamp) {
		if (verbose)
			printf("%s() avtp_timestamp wrapped along the way!\n", __func__);
		lavtp += ((uint64_t)1<<32)-1;
	}
	int64_t avtp_diff = lavtp - ch->cbp->meta.avtp_timestamp;
	uint64_t ptp_capture = ch->cbp->meta.ts_recv_ptp_ns - avtp_diff;

	/* track E2E delay if --break is passed */
	if (break_us > 0 && (avtp_diff/1000)  > break_us) {
		char tbmsg[128] = {0};
		snprintf(tbmsg, 127, "E2E delay (%.3f us) exceeded break_value (%d)", (float)avtp_diff/1000, break_us);
		fprintf(stderr, "%s\n", tbmsg);
		tb_tag(_nh->tb, tbmsg);
		tb_close(_nh->tb);
		_nh->tb = NULL;
		nh_destroy(&_nh);
	}
	/* Extract timing-data from pipe, reconstruct avtp_timestamp,
	 * find diff since it was sent and calculate offset to determine
	 * length of sleep before moving on.
	 */
	if (read_delay) {
		switch (ch->sc) {
		case CLASS_A:
			ptp_capture += 2 * NS_IN_MS;
			break;
		case CLASS_B:
			ptp_capture += 50 * NS_IN_MS;
			break;
		}
		int64_t err = _delay(ch, ptp_capture);

		if (verbose) {
			printf("%s() Sample spent %ld ns from capture to recvmsg()\n",
				__func__, avtp_diff);
			printf("%s() Reconstructed timestamp: %lu\n",
				__func__, ptp_capture);
			printf("%s() Delayed to %lu, missed by %ld ns\n",
				__func__, ptp_capture, err);
		}
	}

	return res;
}

int chan_read(struct channel *ch, void *data)
{
	return _chan_read(ch, data, false);
}

int chan_read_wait(struct channel *ch, void *data)
{
	return _chan_read(ch, data, true);
}

void * chan_get_payload(struct channel *ch)
{
	if (!ch)
		return NULL;
	return (void *)ch->payload;
}


static int _nh_socket_setup_common(struct nethandler *nh)
{
	int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
	if (sock == -1) {
		perror("Failed opening socket");
		return -1;
	}

	struct ifreq req;
	snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", nh->ifname);
	if (ioctl(sock, SIOCGIFINDEX, &req) == -1) {
		fprintf(stderr, "%s(): Could not get interface index for %s: %s\n", __func__, nh->ifname, strerror(errno));
		return -1;
	}
	nh->sk_addr.sll_family = AF_PACKET;
	nh->sk_addr.sll_protocol = htons(ETH_P_TSN);
	nh->sk_addr.sll_halen = ETH_ALEN;
	nh->sk_addr.sll_ifindex = req.ifr_ifindex;

	/*
	 * Special case: "lo"
	 *
	 * The network-layer will drop incoming frames that originates
	 * from here meaning if we send a frame to ourselves, it will be
	 * dropped. Normally this makes sense, but in a testing-scenario
	 * we may want to feed the system artificial frames to verify
	 * response.
	 *
	 * if nic is indeed 'lo', assume that we are testing, so place
	 * it in promiscuous mode.
	 */
	if (strncmp(nh->ifname, "lo", 2) == 0) {
		if (ioctl(sock, SIOCGIFFLAGS, &req) == -1) {
			perror("Failed retrieveing flags for lo");
			return -1;
		}
		req.ifr_flags |= IFF_PROMISC;
		if (ioctl(sock, SIOCSIFFLAGS, &req) == -1) {
			fprintf(stderr, "%s(): Failed placing lo in promiscuous "\
				"mode, may not receive incoming data (tests may "\
				"fail)", __func__);
		}
	}
	return sock;
}

static unsigned int _get_link_speed_Mbps(int socket, const char *ifname)
{
	struct ifreq ifr;
	struct ethtool_cmd data;

	/* If we are opening lo, speed does not make sense, so just
	 * pretend it has 1 Gbps capacity. */
	if (strcmp(ifname, "lo") == 0) {
		return 1000;
	}

	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = &data;
	data.cmd = ETHTOOL_GSET;

	if (ioctl(socket, SIOCETHTOOL, &ifr) < 0) {
		fprintf(stderr, "Failed reading ethtool data (%s)\n", strerror(errno));
		return -1;
	}
	return (unsigned int)ethtool_cmd_speed(&data);
}

struct nethandler * nh_create_init(char *ifname, size_t hmap_size, const char *logfile)
{
	struct nethandler *nh = calloc(sizeof(*nh), 1);
	if (!nh)
		return NULL;
	nh->tid = 0;
	nh->hmap_sz = hmap_size;
	nh->hmap = calloc(sizeof(struct cb_entity), nh->hmap_sz);
	if (!nh->hmap) {
		free(nh);
		return NULL;
	}

	strncpy((char *)nh->ifname, ifname, IFNAMSIZ-1);

	nh->rx_sock = _nh_socket_setup_common(nh);
	if (nh->tid == 0)
		nh_start_rx(nh);

	/* lock memory, we don't pagefaults later */
	if (mlockall(MCL_CURRENT|MCL_FUTURE)) {
		fprintf(stderr, "%s(): failed locking memory (%d, %s)\n",
			__func__, errno, strerror(errno));
		nh_destroy(&nh);
		return NULL;
	}

	/* disable dma latency */
	if (!keep_cstate) {
		nh->dma_lat_fd = open("/dev/cpu_dma_latency", O_RDWR);
		if (nh->dma_lat_fd < 0) {
			fprintf(stderr, "%s(): failed opening /dev/cpu_dma_latency, (%d, %s)\n",
				__func__, errno, strerror(errno));
		} else {
			int lat_val = 0;
			int res = write(nh->dma_lat_fd, &lat_val, sizeof(lat_val));
			if (res < 1) {
				fprintf(stderr, "%s(): Failed writing %d to /dev/cpu_dma_latency (%d, %s)\n",
					__func__, lat_val, errno, strerror(errno));
			}
			if (verbose)
				printf("%s(): Disabled cstate on CPU\n", __func__);
		}
	}
	/*
	 * Open logfile if provided
	 */
	if (enable_logging) {
		nh->logger = log_create(logfile, enable_delay_logging);
		if (!nh->logger) {
			fprintf(stderr, "%s() Somethign went wrong when enabling logger, datalogging disabled\n", __func__);
			enable_logging = false;
		}
	}

	if (use_tracebuffer) {
		nh->tb = tb_open();
	}

	/* get PTP fd for timekeeping
	 *
	 * FIXME: properly handle error when opening (assume caller
	 * knows their hardware?)
	 */
	nh->ptp_fd = -1;
	if (strncmp(nh->ifname, "lo", 2) != 0) {
		nh->ptp_fd = get_ptp_fd(ifname);
		if (nh->ptp_fd < 0)
			fprintf(stderr, "%s(): failed getting FD for PTP on %s\n", __func__, ifname);
	}

	/* query link speed (need this to ensure that Tx-channels does
	 * not have too short periods)
	 */
	nh->link_speed = _get_link_speed_Mbps(nh->rx_sock, nh->ifname) * 1e6;

	/* if (strlen(nc_termdevice) > 0) */
	/* 	nh->ttys = term_open(nc_termdevice); */

	return nh;
}

int nh_create_init_standalone(void)
{
	/* avoid double-create */
	if (_nh != NULL)
		return -1;
	_nh = nh_create_init(nc_nic, nc_hmap_size, nc_logfile);
	if (_nh)
		return 0;
	return -1;
}


int nh_reg_callback(struct nethandler *nh,
		uint64_t stream_id,
		void *priv_data,
		int (*cb)(void *priv_data, struct avtpdu_cshdr *du))
{
	if (!nh || !nh->hmap_sz || !cb)
		return -EINVAL;

	int idx = stream_id % nh->hmap_sz;

	for (int i = 0; i < nh->hmap_sz && nh->hmap[idx].cb; i++)
		idx = (idx + 1) % nh->hmap_sz;

	if (nh->hmap[idx].cb)
		return -1;

	nh->hmap[idx].stream_id = stream_id;
	nh->hmap[idx].priv_data = priv_data;
	nh->hmap[idx].cb = cb;
	return 0;
}

int nh_std_cb(void *priv, struct avtpdu_cshdr *du)
{
	if (!priv || !du) {
		/* printf("%s(): callback, but priv or du was NULL\n", __func__); */
		return -EINVAL;
	}

	struct cb_priv *cbp = (struct cb_priv *)priv;
	if (cbp->fd <= 0) {
		/* printf("%s(): got priv and du, but no fd (pipe) set\n", __func__); */
		return -EINVAL;
	}

	/* copy payload in du into payload in pipe_meta */
	void *payload = (void *)du + sizeof(*du);
	memcpy(&cbp->meta.payload, payload, cbp->sz);

	int wsz = write(cbp->fd, (void *)&cbp->meta, sizeof(struct pipe_meta) + cbp->sz);
	if (wsz == -1) {
		perror("Failed writing to fifo");
		return -EINVAL;
	}
	if (use_tracebuffer && _nh) {
		char tbmsg[128] = {0};
		snprintf(tbmsg, 127, "wrote %d to pipe", wsz);
		tb_tag(_nh->tb, tbmsg);
	}

	return 0;
}


static int get_hm_idx(struct nethandler *nh, uint64_t stream_id)
{
	if (!nh)
		return -ENOMEM;

	int idx = stream_id % nh->hmap_sz;
	for (int i = 0; i < nh->hmap_sz; i++) {
		if (nh->hmap[idx].stream_id == stream_id && nh->hmap[idx].cb)
			return idx;

		idx = (idx + 1) % nh->hmap_sz;
	}
	return -1;
}


int nh_feed_pdu_ts(struct nethandler *nh, struct avtpdu_cshdr *cshdr,
		uint64_t rx_hw_ns,
		uint64_t recv_ptp_ns)
{
	if (!nh || !cshdr)
		return -EINVAL;
	int idx = get_hm_idx(nh, be64toh(cshdr->stream_id));

	if (use_tracebuffer) {
		char tbmsg[128] = {0};
		snprintf(tbmsg, 127, "feed_pdu_ts, feed to hmapidx=%d", idx);
		tb_tag(nh->tb, tbmsg);
	}

	if (idx >= 0) {
		if (verbose)
			printf("%s(): received msg, hmidx: %d\n", __func__, idx);
		struct cb_priv *cbp = nh->hmap[idx].priv_data;
		cbp->meta.ts_rx_ns = rx_hw_ns;
		cbp->meta.ts_recv_ptp_ns = recv_ptp_ns;
		cbp->meta.avtp_timestamp = ntohl(cshdr->avtp_timestamp);

		/* printf("%s(): ts_rx_ns=%lu, ts_recv_ptp_ns=%lu, avtp_timestamp=%u\n", */
		/* 	__func__, cbp->meta.ts_rx_ns, cbp->meta.ts_recv_ptp_ns, cbp->meta.avtp_timestamp); */
		return nh->hmap[idx].cb(cbp, cshdr);
	}

	/* no callback registred, though not exactly an FD-error */
	return -EBADFD;
}

int nh_feed_pdu(struct nethandler *nh, struct avtpdu_cshdr *cshdr)
{
	return nh_feed_pdu_ts(nh, cshdr, 0, 0);
}


static void * nh_runner(void *data)
{
	if (!data)
		return NULL;

	struct nethandler *nh = (struct nethandler *)data;
	if (nh->rx_sock <= 0)
		return NULL;

	/* set timeout (250ms) on socket in case we block and nothing arrives
	 * and we want to close down
	 */
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 250000;
	if (setsockopt(nh->rx_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) == -1) {
		printf("%s(): Could not set timeout on socket (%d): %s\n",
			__func__, nh->rx_sock, strerror(errno));
	}

	int enable_ts = 1;
	if (setsockopt(nh->rx_sock, SOL_SOCKET, SO_TIMESTAMPNS, &enable_ts, sizeof(enable_ts)) < 0) {
		fprintf(stderr, "%s(): failed enabling SO_TIMESTAMPNS on Rx socket (%d, %s)\n",
			__func__, errno, strerror(errno));
	}
	if (verbose)
		printf("%s() SO_TIMESTAMPNS enabled! -> %d\n", __func__, enable_ts);

	unsigned char buffer[1522] = {0};
	bool running = true;

	struct msghdr msg;
	struct iovec entry;
	struct sockaddr_in addr;
	struct {
		struct cmsghdr cm;
		char control[512];
	} control;

	while (running) {

		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &entry;
		msg.msg_iovlen = 1;
		entry.iov_base = buffer;
		entry.iov_len = sizeof(buffer);
		msg.msg_name = (caddr_t)&addr;
		msg.msg_namelen = sizeof(addr);
		msg.msg_control = &control;
		msg.msg_controllen = sizeof(control);

		int n = recvmsg(nh->rx_sock, &msg, 0);
		/* grab local timestamp now that we've received a msg */
		uint64_t recv_ptp_ns = get_ptp_ts_ns(nh->ptp_fd);
		uint64_t rx_hw_ns = 0;
		running = nh->running;
		if (n > 0) {
			/* Read HW Rx time  */
			struct cmsghdr *cmsg;
			for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
				if (cmsg->cmsg_type == SO_TIMESTAMPNS) {
					struct timespec *stamp = (struct timespec *)CMSG_DATA(cmsg);
					rx_hw_ns = stamp->tv_sec * 1e9 + stamp->tv_nsec;
				}
			}

			/* parse data */
			struct avtpdu_cshdr *du = (struct avtpdu_cshdr *)buffer;
			nh_feed_pdu_ts(nh, du, rx_hw_ns, recv_ptp_ns);

			/* we have all the timestamps, so we can safely
			 * log this /after/ the data has been passed
			 * on
			 */
			if (enable_logging)
				log_rx(nh->logger, du, rx_hw_ns, recv_ptp_ns);
		}
	}
	return NULL;
}

int nh_start_rx(struct nethandler *nh)
{
	if (!nh)
		return -1;
	if (nh->rx_sock == -1)
		return -1;
	nh->running = true;
	pthread_create(&nh->tid, NULL, nh_runner, nh);

	return 0;
}

void nh_stop_rx(struct nethandler *nh)
{
	if (nh && nh->running) {
		nh->running = false;
		if (nh->tid > 0) {
			/* once timeout expires, join */
			pthread_join(nh->tid, NULL);
			nh->tid = 0;
			nh->rx_sock = -1;
		}
	}
}

int _nh_get_len(struct channel *head)
{
	if (!head)
		return 0;
	int len = 0;
	do {
		head = head->next;
		len++;
	} while (head);
	return len;
}
int nh_get_num_tx(struct nethandler *nh)
{
	return _nh_get_len(nh->du_tx_head);
}

int nh_get_num_rx(struct nethandler *nh)
{
	return _nh_get_len(nh->du_rx_head);
}

int nh_add_tx(struct nethandler *nh, struct channel *du)
{
	if (!nh || !du)
		return -EINVAL;
	if (!nh->du_tx_head) {
		nh->du_tx_head = du;
		nh->du_tx_tail = du;
	} else {
		nh->du_tx_tail->next = du;
		nh->du_tx_tail = du;
	}
	return 0;
}

int nh_add_rx(struct nethandler *nh, struct channel *du)
{
	if (!nh || !du)
		return -EINVAL;

	if (!nh->du_rx_head) {
		nh->du_rx_head = du;
		nh->du_rx_tail = du;
	} else {
		nh->du_rx_tail->next = du;
		nh->du_rx_tail = du;
	}
	return 0;
}

void nh_destroy(struct nethandler **nh)
{
	if (*nh) {
		if (use_tracebuffer)
			tb_close((*nh)->tb);
		if ((*nh)->dma_lat_fd > 0)
			close((*nh)->dma_lat_fd);


		nh_stop_rx(*nh);

		if (enable_logging) {
			log_close_fp((*nh)->logger);
			free((*nh)->logger);
			(*nh)->logger = NULL;
		}

		/* close down and exit safely */
		if ((*nh)->hmap != NULL)
			free((*nh)->hmap);

		/* clean up TX PDUs */
		while ((*nh)->du_tx_head) {
			struct channel *ch = (*nh)->du_tx_head;
			(*nh)->du_tx_head = (*nh)->du_tx_head->next;
			chan_destroy(&ch);
		}

		/* clean up Rx PDUs */
		while ((*nh)->du_rx_head) {
			struct channel *ch = (*nh)->du_rx_head;
			(*nh)->du_rx_head = (*nh)->du_rx_head->next;
			chan_destroy(&ch);
		}

		/* Free memory */
		free(*nh);
	}
	*nh = NULL;
}

void nh_destroy_standalone()
{
	if (_nh)
		nh_destroy(&_nh);
}

uint64_t get_class_delay_bound_ns(struct channel *du)
{
	if (!du)
		return 0;
	switch (du->sc) {
	case CLASS_A:
		return 2*NS_IN_MS;
	case CLASS_B:
		return 50*NS_IN_MS;
	default:
		return 0;
	}
}
