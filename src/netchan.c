/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <netchan.h>
#include <netchan_srp_client.h>
#include <logger.h>
#include <tracebuffer.h>

#include <stdbool.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/net_tstamp.h>
#include <linux/if_vlan.h>
#include <linux/net.h>
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
#define GUARD pthread_mutex_lock(&ch->guard)
#define UNGUARD pthread_mutex_unlock(&ch->guard)

int nc_get_chan_idx(char *name, const struct channel_attrs *attrs, int arr_size)
{
	for (int i = 0; i < arr_size; i++) {
		if (strncmp(name, attrs[i].name, 32) == 0)
			return i;
	}
	return -1;
}

struct channel_attrs * nc_get_chan(char *name, const struct channel_attrs *attrs, int sz)
{
	if (!name || !attrs || sz < 1)
		return NULL;

	int idx = nc_get_chan_idx(name, attrs, sz);
	if (idx == -1)
		return NULL;

	struct channel_attrs *ca = malloc(sizeof(*ca));
	if (!ca)
		return NULL;
	*ca = attrs[idx];
	return ca;
}

const struct channel_attrs * nc_get_chan_ref(char *name, const struct channel_attrs *attrs, int sz)
{
	if (!name || !attrs || sz < 1)
		return NULL;
	int idx = nc_get_chan_idx(name, attrs, sz);
	if (idx == -1)
		return NULL;

	return &attrs[idx];
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
		INFO(NULL, "Requested interval too short: %zd, minimum ns_to_tx: %d\n", interval_ns, ns_to_tx);
		return false;
	}
	if (interval_ns >= NS_IN_HOUR)
		return false;

	/* Test utilization */
	if ((tx_sz * 1e9/nh->link_speed) > interval_ns) {
		INFO(NULL, "Cannot send %zu bits (%d) in %lu ns\n", tx_sz, sz, interval_ns);
		return false;
	}
	return true;
}

static bool _valid_size(struct nethandler *nh, uint16_t sz)
{
	if (sz == 0)
		return false;

	if ((sz + 24 + 22) > 1522) {
		INFO(NULL, "Requested size too large (%d yields total framesize > MTU of 1522)\n", sz);
		return false;
	}
	return true;
}

static void _chan_avtpdu_init(struct channel *ch, uint64_t stream_id)
{
	ch->pdu.subtype = AVTP_SUBTYPE_NETCHAN;
	ch->pdu.stream_id = htobe64(stream_id);
	ch->pdu.sv = 1;
	ch->pdu.seqnr = 0xff;
	ch->sidw.s64 = stream_id;
}

static void _chan_set_streamclass(struct channel *ch,
				enum stream_class sc,
				uint64_t interval_ns)
{
	ch->sc = sc;
	struct srp *srp = ch->nh->srp;

	/* Set default values, if we run with SRP, it will be updated
	 * once we've connected to mrpd
	 */
	switch (ch->sc) {
	case SC_TAS:
		ch->pcp_prio = srp->prio_tas;
		break;
	case SC_CLASS_A:
		ch->pcp_prio = srp->prio_a;
		if (interval_ns < 125*NS_IN_US)
			WARN(ch, "Class A stream frequency larger than 8kHz, reserved bandwidth will be too low!\n");
		break;
	case SC_CLASS_B:
		ch->pcp_prio = srp->prio_b;
		if (interval_ns < 250*NS_IN_US)
			WARN(ch, "Class B stream frequency larger than 4kHz, reserved bandwidth will be too low!\n");
		break;
	}
}

#define PREAMBLE_SZ		7
#define SFD_SZ			1
#define CRC_SZ			4
#define IPG_SZ			12
#define L1_SZ			(PREAMBLE_SZ + SFD_SZ + CRC_SZ + IPG_SZ)


/*
 * _chan_create - create and initialize a new channel.
 *
 * Not that this is the bare basics for a channel, neither Rx nor Tx will be properly 
 *
 * The common workflow is that a channel is created once for a stream and
 * then updated before sending. If the traffic is so dense that the
 * network layer has not finished with it before a new must be
 * assembled, caller should create multiple channels..
 *
 * @param nh nethandler container
 * @param attrs channel attributes describing this stream
 *
 * @returns the new channel or NULL upon failure.
 */
static struct channel * _chan_create(struct nethandler *nh,
				struct channel_attrs *attrs)
{
	/* Validate */
	if (!nh || !attrs)
		return NULL;

	if (!_valid_size(nh, attrs->size) || !_valid_interval(nh, attrs->interval_ns, attrs->size))
		return NULL;

	struct channel *ch = calloc(1, sizeof(*ch) + attrs->size);
	if (!ch)
		return NULL;
	ch->nh = nh;
	ch->payload_size = attrs->size;
	ch->full_size = L1_SZ + sizeof(struct ethhdr) + 4 + sizeof(struct avtpdu_cshdr) + ch->payload_size;
	ch->stopping = false;

	DEBUG(ch, "payload_size=%d, full_size=%d", ch->payload_size, ch->full_size);

	_chan_avtpdu_init(ch, attrs->stream_id);

	/* Create state guard */
	pthread_mutexattr_t mtx_attr;
	pthread_mutexattr_init(&mtx_attr);
	pthread_mutexattr_setprotocol(&mtx_attr, PTHREAD_PRIO_INHERIT);
	if (pthread_mutex_init(&ch->guard, &mtx_attr) != 0) {
		ERROR(ch, "Failed initializing channel guard, aborting (%d : %s)", errno, strerror(errno));
		chan_destroy(&ch);
		return NULL;
	}

	/* It does not make sense to set the next-ts (socket is not
	 * fully configured) just yet, leave this at 0 from calloc
	 *
	 * As long as it is 'sufficienty in the past', the first frame
	 * arriving should fly straight through.
	 */
	ch->interval_ns = attrs->interval_ns;

	memcpy(ch->dst, attrs->dst, ETH_ALEN);

	/* the streamclass is used for more than just SRP, it also tries
	 * to steer traffic to different local classes as we use more
	 * than one shaper.
	 */
	_chan_set_streamclass(ch, attrs->sc, attrs->interval_ns);

	ch->tx_sock = -1;

	/* Set up pipes for Rx/Tx */
	int pfd[2];
	if (pipe(pfd) == -1) {
		ERROR(ch, "Unable to create pipe-pair during setup (%d: %s)", errno, strerror(errno));
		chan_destroy(&ch);
		return NULL;
	}
	ch->fd_r = pfd[0];
	ch->fd_w = pfd[1];


	pthread_mutex_init(&ch->ready_mtx, NULL);
	pthread_cond_init(&ch->ready_cond, NULL);

	return ch;
}

struct channel *chan_create_tx(struct nethandler *nh, struct channel_attrs *attrs)
{
	if (!nh || !attrs)
		return NULL;

	struct channel *ch = _chan_create(nh, attrs);
	if (!ch)
		return NULL;

	switch(attrs->sc) {
	case SC_TAS:
		if (!nc_create_tas_tx_sock(ch)) {
			ERROR(ch, "Failed creating TAS Tx socket for channel");
			UNGUARD;
			chan_destroy(&ch);
			return NULL;
		}
		ch->use_so_txtime = true;
		break;
	case SC_CLASS_A:
	case SC_CLASS_B:
		if (!nc_create_cbs_tx_sock(ch)) {
			ERROR(ch, "Failed creating CBS Tx socket for channel");
			UNGUARD;
			chan_destroy(&ch);
			return NULL;
		}
		break;
	}

	/* We are ready to send, the first attempt should fly straight through */
	ch->next_tx_ns = tai_get_ns();

	/* Add ref to internal list for memory mgmt */
	nh_add_tx(nh, ch);

	/* Announce the talker to the network */
	if (nh->use_srp) {
		if (!nc_srp_new_talker(ch)) {
			ERROR(ch, "%s() Failed setting up SRP for talker.");
			chan_destroy(&ch);
			return NULL;
		}
	} else {
		/* If we are in SRP-mode, this will be done by the SRP monitor
		 * thread when the first listener notifies us that it is
		 * ready.
		 *
		 * Likewise, if all listeneres leaves, ready will be cleared by
		 * the same monitor.
		 */
		ch->ready = true;
	}

	INFO(ch, "Tx channel created (sending to %s).", ether_ntoa((struct ether_addr *)&ch->dst));

	return ch;
}


struct channel *chan_create_rx(struct nethandler *nh, struct channel_attrs *attrs)
{
	if (!nh || !attrs)
		return NULL;
	struct channel *ch = _chan_create(nh, attrs);
	if (!ch)
		return NULL;

	if (ch->dst[0] == 0x01 && ch->dst[1] == 0x00 && ch->dst[2] == 0x5E) {
		DEBUG(ch, "receive data on a multicast group, adding membership");

		struct packet_mreq mr;
		memset(&mr, 0, sizeof(mr));
		mr.mr_ifindex = ch->nh->ifidx;
		mr.mr_type = PACKET_MR_PROMISC;
		if (setsockopt(ch->nh->rx_sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1) {
			WARN(ch, "failed setting multicast membership, may not receive frames! => %d: %s",
				errno, strerror(errno));
		}
	}

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
		UNGUARD;
		chan_destroy(&ch);
		return NULL;
	}

	ch->cbp->fd = ch->fd_w;
	ch->cbp->sz = ch->payload_size;

	/* Add ref to internal list for memory mgmt */
	nh_add_rx(ch->nh, ch);
	nh_reg_callback(ch->nh, ch->sidw.s64, ch->cbp, nh_std_cb);

	/* Listener will be marked ready by SRP monitor thread, but  */
	if (!ch->nh->use_srp)
		ch->ready = true;

	return ch;
}

bool chan_ready(struct channel *ch)
{
	if (!ch)
		return false;
	return ch->ready;
}

int chan_ready_timedwait(struct channel *ch, uint64_t timeout_ns)
{
	struct timespec ts;

	if (!ch)
		return -EINVAL;

	if (!ch->nh || !ch->nh->running || ch->stopping)
		return -EINVAL;

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += timeout_ns;
	ts_normalize(&ts);

	/* Fastpath, we are ready, so avoid CV dance */
	if (ch->ready)
		return 0;

	pthread_mutex_lock(&ch->ready_mtx);
        int ret  = pthread_cond_timedwait(&ch->ready_cond, &ch->ready_mtx, &ts);
	pthread_mutex_unlock(&ch->ready_mtx);

	return (ret == 0 ? 0 : -ret);
}

bool chan_stop(struct channel *ch)
{
	/* We cannot use chan_valid() here, as we *may* stop the channel
	 * before SRP is up and running (thus ready := false)
	 */
	if (!ch || !ch->nh) {
		printf("%s(): invalid channel\n", __func__);
		return false;
	}
	ch->ready = false;

	/* No need to stop twice. */
	if (ch->stopping)
		return false;
	ch->stopping = true;

	if (ch->nh->use_srp) {
		if (ch->tx_sock >= 0)
			nc_srp_remove_talker(ch);
		else
			nc_srp_remove_listener(ch);
	}


	/* FIXME: abort current blocking reads to ch->fd_r but without
	 * destroying the pipe.
	 *
	 * We have marked channel as !ready, so read() will detect this
	 * and close down.
	 */
	char *dummy = (char *)calloc(1, ch->payload_size);
	write(ch->fd_w, dummy, ch->payload_size);

	return true;
}

static void _chan_destroy(struct channel **ch, bool unlink)
{
	chan_stop(*ch);

	if ((*ch)->fd_r >= 0)
		close((*ch)->fd_r);
	if ((*ch)->fd_w >= 0)
		close((*ch)->fd_w);

	/* Must remove channel from Tx or Rx list */
	if ((*ch)->tx_sock >= 0) {
		if (unlink)
			nh_remove_tx(*ch);
		close((*ch)->tx_sock);
		(*ch)->tx_sock = -1;
	} else {
		if (unlink)
			nh_remove_rx(*ch);
	}

	if ((*ch)->cbp)
		free((*ch)->cbp);

	free(*ch);
	*ch = NULL;
}

void chan_destroy(struct channel **ch)
{

	if (!*ch)
		return;
	_chan_destroy(ch, true);
}

bool chan_valid(struct channel *ch)
{
	if (!ch || !ch->nh)
		return false;

	if (!ch->ready)
		return false;

	/* Must always have a Rx socket */
	if (ch->nh->rx_sock < 0)
		return false;

	/* if tx; no cbp and valid interval */
	if (ch->tx_sock > 0) {
		if (ch->cbp || ch->interval_ns <= 0)
			return false;
	} else {
		/* Rx *must* have callback-buffer */
		if (!ch->cbp)
			return false;
	}

	/* Invalid StreamID (not set) */
	if (ch->sidw.s64 == 0 || ch->pdu.stream_id == 0)
		return false;

	/* payload size */
	if (ch->payload_size <= 0)
		return false;

	/* fd_w, fd_r */
	if (ch->fd_w == 0 && ch->fd_r == 0)
		return false;

	return true;
}

int chan_update(struct channel *ch, uint64_t ts, void *data)
{
	if (!chan_valid(ch))
		return -ENOMEM;

	if (!data)
		return -ENOMEM;

	ch->sample_ns = ts;
	ch->pdu.seqnr++;
	ch->pdu.avtp_timestamp = htonl(tai_to_avtp_ns(ts));
	ch->pdu.tv = 1;
	ch->pdu.sdl = htons(ch->payload_size);
	memcpy(ch->payload, data, ch->payload_size);
	return 0;
}

void chan_dump_state(struct channel *ch)
{
	if (!chan_valid(ch)) {
		WARN(ch, "Invalid channel");
		return;
	}
	double freq = 1e9 / (double)ch->interval_ns;
	printf("%18s : %s\n", "iface", ch->nh->ifname);
	printf("%18s : %.0f Mbps\n", "link speed", ch->nh->link_speed / 1e6);
	printf("%18s : %"PRIu64" ns (%.1f Hz)\n", "period", ch->interval_ns, freq);
	printf("%18s : %s\n", "use_so_txtime", ch->use_so_txtime ? "true" : "false");
	printf("%18s : %d\n", "so_priority", ch->tx_sock_prio);
	printf("%18s : 0x%04x\n", "use_deadline_mode", ch->txtime.flags & SOF_TXTIME_DEADLINE_MODE);
	printf("%18s : 0x%04x\n", "receive_errors", ch->txtime.flags & SOF_TXTIME_REPORT_ERRORS);
	printf("%18s : %02x:%02x:%02x:%02x:%02x:%02x\n", "MAC",
		ch->dst[0], ch->dst[1], ch->dst[2],
		ch->dst[3], ch->dst[4], ch->dst[5]);
	printf("%18s : %d\n", "clkid", ch->txtime.clockid);
	printf("%18s : 0x%04x\n", "flags", ch->txtime.flags);
	printf("%18s : %d\n", "pl size", ch->payload_size);
}

int wait_for_tx_slot(struct channel *ch)
{
	if (!chan_valid(ch))
		return -EINVAL;
	uint64_t ts_tai = tai_get_ns();

	/* No need to wait */
	if (ts_tai > ch->next_tx_ns)
		return 0;

	struct timespec ts = {
		.tv_sec = ch->next_tx_ns / 1000000000,
		.tv_nsec = ch->next_tx_ns % 1000000000,
	};
	ts_normalize(&ts);

	/* account for offload to NIC and wakeup accuracy */
	ts_subtract_ns(&ts, 100000);

	return clock_nanosleep(CLOCK_TAI, TIMER_ABSTIME, &ts, NULL);
}

/* FIXME: Deprecated, only left as placeholder for later */
int chan_send(struct channel *ch, uint64_t *tx_ns)
{
	if (!chan_valid(ch) || ch->tx_sock == -1)
		return -EINVAL;
	if (!ch->ops)
		return -EINVAL;
	return ch->ops->send_at(ch, tx_ns);
}
int chan_send_now(struct channel *ch, void *data)
{
	if (!chan_valid(ch) || !ch->ops)
		return -EINVAL;
	return ch->ops->send_now(ch, data);
}

int chan_send_now_wait(struct channel *ch, void *data)
{
	if (!chan_valid(ch) || !ch->ops)
		return -EINVAL;
	return ch->ops->send_now_wait(ch, data);
}

int64_t chan_delay(struct channel *du, uint64_t ptp_target_delay_ns)
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
		WARN(du, "%s() FAILED (%d, %s)", __func__, errno, strerror(errno));
		return -1;
	}
	ts_cpu.tv_nsec += rel_delay_ns;
	ts_normalize(&ts_cpu);
	uint64_t cpu_target_delay_ns = ts_cpu.tv_sec * NS_IN_SEC + ts_cpu.tv_nsec;

	if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_cpu, NULL) == -1)
		WARN(du, "%s(): clock_nanosleep failed (%d, %s)", __func__, errno, strerror(errno));

	clock_gettime(CLOCK_MONOTONIC, &ts_wakeup);
	uint64_t cpu_wakeup_ns = ts_wakeup.tv_sec * NS_IN_SEC + ts_wakeup.tv_nsec;
	int64_t error_cpu_ns = cpu_target_delay_ns - cpu_wakeup_ns;

	log_wakeup_delay(du->nh->logger, ptp_target_delay_ns, cpu_target_delay_ns, cpu_wakeup_ns);
	if (du->nh->tb) {
		tb_tag(du->nh->tb, "_delay(), target=%lu, current=%lu, error=%ld (%s)",
			ptp_target_delay_ns,
			cpu_wakeup_ns,
			error_cpu_ns,
			error_cpu_ns < 0 ? "late" : "early");
	}

	INFO(du, "%s(): PTP Target: %lu, actual: %lu, error: %.3f (us) (%s)",
		__func__, cpu_target_delay_ns, cpu_wakeup_ns,
		1.0 * error_cpu_ns / 1000,
		error_cpu_ns < 0 ? "late" : "early");

	return error_cpu_ns;
}



uint64_t chan_time_to_tx(struct channel *ch)
{
	/* Invalid channel, -1 will yield a value as  */
	if (!chan_valid(ch))
		return UINT64_MAX;

	uint64_t tai_now = tai_get_ns();

	/* If next_tx_ns is in the past, we can send right now */
	return tai_now > ch->next_tx_ns ? 0 : ch->next_tx_ns - tai_now;
}

int _chan_read(struct channel *ch, void *data, bool read_delay)
{
	if (!chan_valid(ch) || ch->stopping)
		return -EINVAL;

	size_t rpsz = sizeof(struct pipe_meta) + ch->payload_size;

	/*
	 * Ingress point: Read data from pipe
	 */
	int res = read(ch->fd_r, &ch->cbp->meta, rpsz);
	if (res < 0) {
		ERROR(ch, "read() from channel FAILED (%d: %s)", errno, strerror(errno));
		return res;
	}

	/* See if we have moved to an invalid state while blocking for a
	 * value (chan_stop() uses this trick to kick us out of a
	 * blocking read)
	 */
	if (!chan_valid(ch))
		return -EINVAL;

	memcpy(data, &ch->cbp->meta.payload, ch->payload_size);

	/* Reconstruct PTP capture timestamp from sender */
	uint64_t lavtp = tai_to_avtp_ns(ch->cbp->meta.ts_recv_ptp_ns);
	if (lavtp < ch->cbp->meta.avtp_timestamp) {
		INFO(ch, "avtp_timestamp wrapped along the way");
		lavtp += ((uint64_t)1<<32)-1;
	}
	int64_t avtp_diff = lavtp - ch->cbp->meta.avtp_timestamp;
	uint64_t ptp_capture = ch->cbp->meta.ts_recv_ptp_ns - avtp_diff;

	/* track E2E delay if --break is passed */
	if (ch->nh->ftrace_break_us > 0 && (avtp_diff/1000)  > ch->nh->ftrace_break_us) {
		tb_tag(ch->nh->tb, "E2E delay (%.3f us) exceeded break_value (%d)", (float)avtp_diff/1000, ch->nh->ftrace_break_us);

		/* This is done by nh_destroy(), but we want to remove
		 * as much noise from the rest of the trace as possible,
		 * so bail early
		 */
		tb_close(ch->nh->tb);
		ch->nh->tb = NULL;
		nh_destroy(&ch->nh);
		return res;
	}

	/* Extract timing-data from pipe, reconstruct avtp_timestamp,
	 * find diff since it was sent and calculate offset to determine
	 * length of sleep before moving on.
	 */
	if (read_delay) {
		int64_t err = chan_delay(ch, ptp_capture + ch->sc);
		INFO(ch, "%s() Sample spent %ld ns from capture to recvmsg() (reconstructed ts: %lu, missed by %ld ns",
			__func__, avtp_diff, ptp_capture + ch->sc, err);
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

static unsigned int _get_link_speed_Mbps(struct nethandler *nh)
{
	if (!nh || nh->rx_sock <= 0)
		return -1;
	struct ifreq ifr;
	struct ethtool_cmd data;

	/* If we are opening lo, speed does not make sense, so just
	 * pretend it has 1 Gbps capacity. */
	if (nh->is_lo)
		return 1000;

	strncpy(ifr.ifr_name, nh->ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = &data;
	data.cmd = ETHTOOL_GSET;

	if (ioctl(nh->rx_sock, SIOCETHTOOL, &ifr) < 0) {
		WARN(NULL, "Failed reading ethtool data (%s)", strerror(errno));
		return -1;
	}
	return (unsigned int)ethtool_cmd_speed(&data);
}

static int _nh_net_setup(struct nethandler *nh, const char *ifname)
{
	if (!nh || !ifname || strlen(ifname) < 1)
		return -1;
	strncpy((char *)nh->ifname, ifname, IFNAMSIZ-1);

	nh->rx_sock = nc_create_rx_sock(ifname);

	struct ifreq req;
	snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", ifname);
	if (ioctl(nh->rx_sock, SIOCGIFINDEX, &req) == -1) {
		WARN(NULL, "Could not get interface index for %s, %d: %s\n", nh->ifname, errno, strerror(errno));
		return -1;
	}
	nh->ifidx = req.ifr_ifindex;
	nh->is_lo = strncmp(nh->ifname, "lo", 2) == 0;

	/* Don't bother about MAC for lo */
	if (!nh->is_lo) {
		if (ioctl(nh->rx_sock, SIOCGIFHWADDR, &req) == -1) {
			ERROR(NULL, "%s(): Failed reading HW-addr from %s: %s",
				__func__, nh->ifname, strerror(errno));
			return -1;
		}
		memcpy(nh->mac, req.ifr_hwaddr.sa_data, 6);
	}
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
	if (nh->is_lo) {
		if (ioctl(nh->rx_sock, SIOCGIFFLAGS, &req) == 0) {
			req.ifr_flags |= IFF_PROMISC;
			if (ioctl(nh->rx_sock, SIOCSIFFLAGS, &req) == -1) {
				WARN(NULL, "%s(): Failed placing lo in promiscuous " \
					"mode, may not receive incoming data (tests may " \
					"fail)", __func__);
			}
		}
	}

	/*
	 * Query link speed (need this to ensure that any Tx-channels
	 * associated with this NIC does not have too short periods)
	 *
	 * If NIC is lo or if the query fails, assume 1Gbps as this is
	 * the most common speed.
	 */
	nh->link_speed = _get_link_speed_Mbps(nh) * 1e6;

	return 0;
}


static void * nh_runner(void *data)
{
	if (!data)
		return NULL;

	struct nethandler *nh = (struct nethandler *)data;
	if (nh->rx_sock <= 0)
		return NULL;

	unsigned char buffer[1522] = {0};

	struct sockaddr_in addr;
	struct iovec entry = {0};
	entry.iov_base = buffer;
	entry.iov_len = sizeof(buffer);

	struct {
		struct cmsghdr cm;
		char control[512];
	} control;

	struct msghdr msg = {
		.msg_iov = &entry,
		.msg_iovlen = 1,
		.msg_name = (void *) &addr,
		.msg_namelen = sizeof(addr),
		.msg_control = &control,
		.msg_controllen = sizeof(control),
	};

	bool running = true;
	while (running) {

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
			struct ethhdr *hdr = (struct ethhdr *)buffer;
			struct avtpdu_cshdr *du = (struct avtpdu_cshdr *)((void *)buffer + sizeof(*hdr));
			if (ntohs(hdr->h_proto) == 0x22f0) {
				if (nh_feed_pdu_ts(nh, du, rx_hw_ns, recv_ptp_ns) == 0) {
					/*
					 * We have all the timestamps, so we can
					 * safely log this /after/ the data has
					 * been passed on.
					 *
					 * Only log for known StreamIDs
					 */
					log_rx(nh->logger, du, rx_hw_ns, recv_ptp_ns);
				}
			}
		}
	}
	return NULL;
}

/*
 * nh_start_rx - start receiver thread
 *
 * This will filter out and grab all TSN ethertype frames coming in on
 * the socket. PDUs will known StreamIDs will be fed to registred
 * callbacks.
 *
 * @param nh nethandler container
 * @returns 0 on success, negative on error
 */
static int _nh_start_rx(struct nethandler *nh)
{
	nh->running = true;

	if (pthread_create(&nh->tid, NULL, nh_runner, nh)) {
		nh->tid = 0;
		nh->running = false;
		return -1;
	}
	return 0;
}

static void _nh_stop_rx(struct nethandler *nh)
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

static int _nh_enable_rt_measures(struct nethandler *nh)
{
	int res = 0;
	/* lock memory, we don't pagefaults later */
	if (mlockall(MCL_CURRENT|MCL_FUTURE)) {
		ERROR(NULL, "%s(): failed locking memory (%d, %s)",
			__func__, errno, strerror(errno));
		res = -1;
	}

	/* disable dma latency, this avoids c-state transitions */
	nh->dma_lat_fd = open("/dev/cpu_dma_latency", O_RDWR);
	if (nh->dma_lat_fd >= 0) {
		int lat_val = 0;
		int wres = write(nh->dma_lat_fd, &lat_val, sizeof(lat_val));
		if (wres < 1) {
			WARN(NULL, "%s(): Failed writing %d to /dev/cpu_dma_latency (%d, %s)",
				__func__, lat_val, errno, strerror(errno));
			res = -2;
		} else {
			DEBUG(NULL, "%s(): Disabled cstate on CPU", __func__);
		}
	}

	return res;
}

struct nethandler * nh_create_init(const char *ifname, size_t hmap_size, const char *logfile)
{
	if (!ifname || !hmap_size)
		return NULL;

	struct nethandler *nh = calloc(sizeof(*nh), 1);
	if (!nh)
		return NULL;
	nh->tx_tas_sock_prio = DEFAULT_TX_TAS_SOCKET_PRIO;
	nh->tx_cbs_sock_prio = DEFAULT_TX_CBS_SOCKET_PRIO;
	nh->hmap_sz = hmap_size;
	nh->hmap = calloc(sizeof(struct cb_entity), nh->hmap_sz);
	if (!nh->hmap) {
		free(nh);
		nh = NULL;
		goto out;
	}

	if (_nh_net_setup(nh, ifname)) {
		ERROR(NULL, "%s(): failed setting up network, aborting", __func__);
		nh_destroy(&nh);
		goto out;

	}

	if (_nh_start_rx(nh)) {
		ERROR(NULL, "%s(): failed starting Rx-handler", __func__);
		nh_destroy(&nh);
		goto out;
	}

	if (_nh_enable_rt_measures(nh))
		WARN(NULL, "%s() Failed enabling RT measures, not fatal but performance may not be optimal", __func__);

	/* First setup of SRP, only the container,  */
	if (!nc_srp_init(nh)) {
		WARN(NULL, "%s() Failed creating basic SRP container", __func__);
		nh_destroy(&nh);
		goto out;
	}

	/*
	 * Open logfile if provided
	 */
	if (logfile && strlen(logfile) > 0) {
		nh->logger = log_create(logfile);
		if (!nh->logger)
			ERROR(NULL, "%s() Something went wrong when enabling logger, datalogging disabled", __func__);
	}

	/* get PTP fd for timekeeping
	 *
	 * FIXME: properly handle error when opening (assume caller
	 * knows their hardware?)
	 */
	nh->ptp_fd = get_ptp_fd(ifname);
	if (nh->ptp_fd < 0 && !nh->is_lo) {
		ERROR(NULL, "%s(): failed getting FD for PTP on %s (%s), aborting.",
			__func__, ifname, strerror(errno));
		nh_destroy(&nh);
		goto out;
	}

out:
	return nh;
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

	/* Hashmap is full */
	if (nh->hmap[idx].cb)
		return -ENOMEM;

	nh->hmap[idx].stream_id = stream_id;
	nh->hmap[idx].priv_data = priv_data;
	nh->hmap[idx].cb = cb;
	return 0;
}

/*
 * we arrive here from nh_feed_pdu_ts()
 */
int nh_std_cb(void *priv, struct avtpdu_cshdr *du)
{
	if (!priv || !du)
		return -EINVAL;

	struct cb_priv *cbp = (struct cb_priv *)priv;
	if (cbp->fd <= 0)
		return -EINVAL;

	/* copy payload in du into payload in pipe_meta */
	void *payload = (void *)du + sizeof(*du);
	memcpy(&cbp->meta.payload, payload, cbp->sz);

	/*
	 * Egress-point: Publish data to awaiting listener
	 */
	int wsz = write(cbp->fd, (void *)&cbp->meta, sizeof(struct pipe_meta) + cbp->sz);
	if (wsz == -1) {
		perror("Failed writing to fifo");
		return -EINVAL;
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

	tb_tag(nh->tb, "feed_pdu_ts, feed to hmapidx=%d", idx);

	if (idx >= 0) {
		struct cb_priv *cbp = nh->hmap[idx].priv_data;
		cbp->meta.ts_rx_ns = rx_hw_ns;
		cbp->meta.ts_recv_ptp_ns = recv_ptp_ns;
		cbp->meta.avtp_timestamp = ntohl(cshdr->avtp_timestamp);

		/* Unless otherwise configured, standard callback
		 * (nh_std_cb) is used
		 */
		return nh->hmap[idx].cb(cbp, cshdr);
	}

	/* no callback registred, though not exactly an FD-error */
	return -EBADFD;
}

int nh_feed_pdu(struct nethandler *nh, struct avtpdu_cshdr *cshdr)
{
	return nh_feed_pdu_ts(nh, cshdr, 0, 0);
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

static int _nh_unlink_ch(struct channel *head, struct channel *rem)
{
	if (!head || !rem)
		return -ENOMEM;
	while (head) {
		if (head->next == rem) {
			head->next = rem->next;
			return 0;
		}
		head = head->next;
	}
	return -EINVAL;
}
int nh_remove_tx(struct channel *ch)
{
	if (!ch || !ch->nh)
		return -ENOMEM;

	/* Since we don't use an empty channel to mark the head, but
	 * instead point directly to first element, we cannot use the
	 * double-deref trick to optimize removal. We are stuck with a
	 * clumsy test for head
	 */
	if (ch->nh->du_tx_head == ch) {
		ch->nh->du_tx_head = ch->nh->du_tx_head->next;
	} else if (_nh_unlink_ch(ch->nh->du_tx_head, ch) < 0) {
		return -1;
	}

	ch->next = NULL;
	ch->nh = NULL;

	return 0;
}

int nh_remove_rx(struct channel *ch)
{
	if (!ch || !ch->nh)
		return -ENOMEM;

	/* Since we don't use an empty channel to mark the head, but
	 * instead point directly to first element, we cannot use the
	 * double-deref trick to optimize removal. We are stuck with a
	 * clumsy test for head
	 */
	if (ch->nh->du_rx_head == ch) {
		ch->nh->du_rx_head = ch->next;
	} else if (_nh_unlink_ch(ch->nh->du_rx_head, ch) < 0) {
		return -1;
	}

	ch->next = NULL;
	ch->nh = NULL;
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

struct channel *get_tx_chan_from_sid(struct nethandler *nh, union stream_id_wrapper stream)
{
	struct channel *ch = nh->du_tx_head;
	while (ch) {
		if (ch->sidw.s64 == stream.s64)
			return ch;
		ch = ch->next;
	}
	return NULL;
}

struct channel *get_rx_chan_from_sid(struct nethandler *nh, union stream_id_wrapper stream)
{
	/* All Rx-channels have an entry in the callbach hashmap.
	 *
	 * This won't give us the chanel (just the opaque callback area
	 * for the pipe), but it's the fastes way to determine if its a
	 * known stream.
	 */
	if (get_hm_idx(nh, stream.s64) < 0 || !nh)
		return NULL;

	struct channel *ch = nh->du_rx_head;
	while (ch) {
		if (ch->sidw.s64 == stream.s64)
			return ch;
		ch = ch->next;
	}
	return NULL;
}

bool nh_notify_talker_Lnew(struct nethandler *nh, union stream_id_wrapper stream, int state)
{
	struct channel *talker = get_tx_chan_from_sid(nh, stream);
	if (!talker)
		return false;
	INFO(talker, "%s() Found remote listener, state=%d\n", __func__, state);

	talker->ready = true;
	return true;
}
bool nh_notify_talker_Lleaving(struct nethandler *nh, union stream_id_wrapper stream, int state)
{
	struct channel *talker = get_tx_chan_from_sid(nh, stream);
	if (!talker)
		return false;

	/* FIXME: make sure we only have a single listener before
	 * closing down. */
	talker->ready = false;

	return true;
}

bool nh_notify_listener_Tnew(struct nethandler *nh, union stream_id_wrapper stream, uint8_t *mac_addr)
{
	struct channel *listener = get_rx_chan_from_sid(nh, stream);
	if (!listener)
		return false;

	/* FIXME: Verify mac_addr to dst */

	if (nc_mrp_send_ready(nh->srp, stream)) {
		INFO(listener, "%s(): Listener ready", __func__);
		listener->ready = true;
	}

	return listener->ready;
}

bool nh_notify_listener_Tleave(struct nethandler *nh, union stream_id_wrapper stream, uint8_t *mac_addr)
{
	struct channel *listener = get_rx_chan_from_sid(nh, stream);
	if (!listener) {
		DEBUG(NULL, "%s() %lu no match", __func__, stream.s64);
		return false;
	}
	/* FIXME: verify mac_addr to dst */

	if (nc_mrp_send_leave(nh->srp, stream)) {
		INFO(listener, "%s(): Listener waiting for talker (it left)", __func__);
		listener->ready = false;
	}
	return true;
}

void nh_set_verbose(struct nethandler *nh, bool verbose)
{
	if (!nh)
		return;
	nh->verbose = verbose;
}

void nh_set_srp(struct nethandler *nh, bool use_srp)
{
	if (!nh)
		return;
	if (use_srp && !nh->use_srp) {
		INFO(NULL, "%s() enabling SRP", __func__);
		if (!nc_srp_setup(nh)) {
			ERROR(NULL, "%s() Cannot enable SRP!", __func__);
			return;
		}
	}
	nh->use_srp = use_srp;

	/* Create monitor thread that grabs incoming messages, extract
	 * SID and dst and matches to Rx/Tx channels */

}

void nh_enable_ftrace(struct nethandler *nh)
{
	if (!nh || nh->tb)
		return;
	nh->tb = tb_open();
}
void nh_set_trace_breakval(struct nethandler *nh, int break_us)
{
	if (!nh)
		return;
	if (break_us <= 0 || break_us > 1000000)
		nh->ftrace_break_us = -1;
	nh->ftrace_break_us = break_us;
	nh_enable_ftrace(nh);
}

bool nh_set_tx_prio(struct nethandler *nh, enum stream_class sc, int tx_prio)
{
	if (!nh || tx_prio < 0 || tx_prio > 8)
		return false;

	/* We do not want to change the Tx priorities once channels are
	 * created.
	 */
	if (nh->du_tx_head != NULL)
		return false;

	switch (sc) {
	case SC_TAS:
		nh->tx_tas_sock_prio = tx_prio;
		break;
	case SC_CLASS_A:
	case SC_CLASS_B:
		nh->tx_cbs_sock_prio = tx_prio;
		break;
	}
	return true;
}

void nh_rotate_logs(struct nethandler *nh)
{
	log_flush_and_rotate(nh->logger);
}

void nh_stop(struct nethandler *nh)
{
	struct channel *ch = nh->du_tx_head;
	while (ch) {
		chan_stop(ch);
		ch = ch->next;
	}

	ch = nh->du_rx_head;
	while (ch) {
		chan_stop(ch);
		ch = ch->next;
	}
}

void nh_destroy(struct nethandler **nh)
{
	if (*nh) {
		/* Signal Rx thread and ask nh_runner to close down
		 *
		 * Rx-socket is created with timeout, so even if it does
		 * not receive any frames, it will time out and detect
		 * that running is false.
		 */
		_nh_stop_rx(*nh);

		if ((*nh)->tb)
			tb_close((*nh)->tb);
		if ((*nh)->dma_lat_fd > 0)
			close((*nh)->dma_lat_fd);

		if ((*nh)->logger) {
			log_flush_and_rotate((*nh)->logger);
			log_destroy((*nh)->logger);
			(*nh)->logger = NULL;
		}

		/* close down and exit safely */
		if ((*nh)->hmap != NULL)
			free((*nh)->hmap);

		/* clean up TX PDUs */
		while ((*nh)->du_tx_head) {
			struct channel *ch = (*nh)->du_tx_head;
			(*nh)->du_tx_head = (*nh)->du_tx_head->next;
			_chan_destroy(&ch, false);
		}

		/* clean up Rx PDUs */
		while ((*nh)->du_rx_head) {
			struct channel *ch = (*nh)->du_rx_head;
			(*nh)->du_rx_head = (*nh)->du_rx_head->next;
			_chan_destroy(&ch, false);
		}

		if ((*nh)->use_srp)
			nc_srp_teardown((*nh));

		/* Free memory */
		free(*nh);
	}
	*nh = NULL;
}

uint64_t get_class_delay_bound_ns(struct channel *du)
{
	if (!du)
		return 0;
	switch (du->sc) {
	case SC_TAS:
		return 100 * NS_IN_US;
	case SC_CLASS_A:
		return 2*NS_IN_MS;
	case SC_CLASS_B:
		return 50*NS_IN_MS;
	}
	return 0;
}

void chan_print_details(struct channel *ch)
{
	if (!ch)
		return;
	printf("%s %s sid=0x%08lx, sz=%d, interval=%.3f ms\n",
		ch->nh->use_srp ? "[SRP]" : "     ",
		ch->tx_sock == -1 ? "Rx" : "Tx",
		ch->sidw.s64,
		ch->payload_size,
		(double)ch->interval_ns / 1e6);
}
void nh_list_active_channels(struct nethandler *nh)
{
	if (!nh)
		return;
	nh_debug(NULL, NC_INFO, "listing active channels\n");
	nh_debug(NULL, NC_INFO, "Rx channels:\n");
	struct channel *curr = nh->du_rx_head;
	while (curr) {
		chan_print_details(curr);
		curr = curr->next;
	}
	nh_debug(NULL, NC_INFO, "Tx channels:\n");
	curr = nh->du_tx_head;
	while (curr) {
		chan_print_details(curr);
		curr = curr->next;
	}
}

static const char *(loglevel_map[]) = {
	"DEBUG",
	"INFO",
	"WARNING",
	"ERROR"
};

int nh_debug(struct channel *ch, enum nc_loglevel loglevel, const char *fmt, ...)
{
	/* Don't print anything below WARNING if we're not in verbose
	 * mode (only works if channel is set)
	 */
	if (ch && (!ch->nh->verbose && loglevel < NC_WARN))
		return 0;

	time_t timer = time(NULL);
	struct tm *tminfo = localtime(&timer);

	char buffer[256] = {0};
	int idx = 0;

	idx += strftime(&buffer[idx], sizeof(buffer)-idx, "%Y-%m-%d %H:%M:%S", tminfo);
	if (ch)
		idx += snprintf(&buffer[idx], sizeof(buffer)-idx, " [0x%08lx]", ch->sidw.s64);
	idx += snprintf(&buffer[idx], sizeof(buffer)-idx, " %s", loglevel_map[loglevel]);
	idx += snprintf(&buffer[idx], sizeof(buffer)-idx, " ");

	va_list args;
	va_start(args, fmt);
	vsnprintf(&buffer[idx], sizeof(buffer)-idx, fmt, args);
	va_end(args);

	if (loglevel >= NC_WARN)
		return fprintf(stderr, "%s\n", buffer);

	return printf("%s\n", buffer);
}
