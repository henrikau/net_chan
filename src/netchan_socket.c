#include <stdio.h>
#include <unistd.h>
#include <linux/net_tstamp.h>
#include <linux/if.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <linux/errqueue.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netchan.h>
#include <logger.h>
#include <tracebuffer.h>

int nc_create_rx_sock(const char *ifname)
{
	/* Can only get promiscous to work reliably for raw sockets  */
	int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sock < 0) {
		ERROR(NULL, "%s(): Failed creating socket: %s", __func__, strerror(errno));
		return -1;
	}

	/*
	 * Set timeout (250ms) on socket in case we block and nothing arrives
	 * and we want to close down
	 */
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 250000;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) == -1) {
		WARN(NULL, " Could not set timeout on socket (%d): %s",
			__func__, sock, strerror(errno));
		close(sock);
		return -1;
	}

	int enable_ts = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS, &enable_ts, sizeof(enable_ts)) < 0) {
		ERROR(NULL, "%s(): failed enabling SO_TIMESTAMPNS on Rx socket (%d, %s)",
			__func__, errno, strerror(errno));
		close(sock);
		return -1;
	}

	/* Bind to device and open in promiscous mode */
	struct ifreq ifr = {0};
	strncpy((char *)ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(sock, SIOCGIFINDEX, &ifr)) {
		ERROR(NULL, "%s(): Failed retrieving idx for %s", __func__, ifname);
		close(sock);
		return -1;
	}
	int ifidx = ifr.ifr_ifindex;

	struct sockaddr_ll sock_address = {0};
	sock_address.sll_family = AF_PACKET;
	sock_address.sll_protocol = htons(ETH_P_ALL);
	sock_address.sll_ifindex = ifidx;
	if (bind(sock, (struct sockaddr *)&sock_address, sizeof(sock_address))) {
		ERROR(NULL, "%s(): bind failed (%s)", __func__, strerror(errno));
		return -1;
	}

	struct packet_mreq mreq = {0};
	mreq.mr_ifindex = ifidx;
	mreq.mr_type = PACKET_MR_PROMISC;
	mreq.mr_alen = 6;
	if (setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
			(void *)&mreq, (socklen_t)sizeof(mreq)) < 0) {
		ERROR(NULL, "%s(): Failed adding membership for promiscous mode (%s)", __func__, strerror(errno));
		close(sock);
		return -1;
	}

	return sock;
}

static int _nc_create_tx_sock(struct channel *ch)
{
	int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
	if (sock < 0) {
		ERROR(NULL, "%s(): Failed creating Tx-socket: %s", __func__, strerror(errno));
		return -1;
	}

	/* Set socket priority option (for sending to the right socket)
	 *
	 * FIXME: allow for outside config of socket prio (see
	 * scripts/setup_nic.sh)
	 */
	if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &ch->tx_sock_prio, sizeof(ch->tx_sock_prio)) < 0) {
		ERROR(NULL, "%s(): failed setting socket priority (%d, %s)",
			__func__, errno, strerror(errno));
		return -1;
	}

	/* Set destination address for outgoing traffict this DU */
	ch->sk_addr.sll_family   = AF_PACKET;
	ch->sk_addr.sll_protocol = htons(ETH_P_TSN);
	ch->sk_addr.sll_halen    = ETH_ALEN;
	ch->sk_addr.sll_ifindex  = ch->nh->ifidx;
	memcpy(&ch->sk_addr.sll_addr, ch->dst, ETH_ALEN);

	return sock;
}

static int _tas_send_at(struct channel *ch, uint64_t *tx_ns)
{
	/*
	 * Look at next planned Tx.
	 * - If the Tx slot opened up in the past, we can send immediately
	 * - If tx is in the future, use this as base and postpone tx
	 * - Update next_tx
	 *	   o Increment next_tx until next_tx is larger than tai
	 *
	 * txtime must be a bit into the future, otherwise it will be
	 * rejected by the qdisc ETF scheduler
	 */
	uint64_t tai_now = tai_get_ns() + 50 * NS_IN_US;
	uint64_t txtime = tai_now > ch->next_tx_ns ? tai_now : ch->next_tx_ns;
	if (tx_ns && *tx_ns > tai_now && *tx_ns < (tai_now + ch->next_tx_ns))
		txtime = *tx_ns;

	do {
		ch->next_tx_ns = txtime + ch->interval_ns;
	} while (ch->next_tx_ns < tai_now);

	/* Add control msg with txtime  */
	struct msghdr msg = {0};
	struct iovec iov  = {0};

	/* payload and destination */
	iov.iov_base = &ch->pdu;
	iov.iov_len = sizeof(struct avtpdu_cshdr) + ch->payload_size;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = (struct sockaddr *)&ch->sk_addr;
	msg.msg_namelen = sizeof(ch->sk_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	/* Set TxTime in socket */
	char control[(CMSG_SPACE(sizeof(uint64_t)))] = {0};
	msg.msg_control = &control;
	msg.msg_controllen = sizeof(control);

	struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
	cm->cmsg_level = SOL_SOCKET;
	cm->cmsg_type = SCM_TXTIME;
	cm->cmsg_len = CMSG_LEN(sizeof(__u64));
	*((__u64 *) CMSG_DATA(cm)) = txtime;

	if (tx_ns)
		*tx_ns = txtime;

	int txsz = sendmsg(ch->tx_sock, &msg, 0);
	if (txsz < 1) {
		tb_tag(ch->nh->tb, "[0x%08lx] Failed sending msg (%d)", ch->sidw.s64, errno);
		if (nc_handle_sock_err(ch->tx_sock, ch->nh->ptp_fd) < 0)
			return -1;
	} else {
		log_tx(ch->nh->logger, &ch->pdu, ch->sample_ns, txtime, txtime);
	}

	/* Report the size of the payload to the usesr, the AVTPDU
	 * header is 'invisible'
	 */
	return txsz - sizeof(struct avtpdu_cshdr);

}
static int _tas_send_at_wait(struct channel *ch, uint64_t *tx_ns)
{
	int res = _tas_send_at(ch, tx_ns);
	if (res < 0)
		goto out;
	*tx_ns += get_class_delay_bound_ns(ch);

	while(chan_delay(ch, *tx_ns) > 50*NS_IN_US) ;

out:
	return res;
}

static int _tas_send_now(struct channel *ch, void *data)
{
	uint64_t ts_ns = real_get_ns();
	if (chan_update(ch, ts_ns, data)) {
		ERROR(ch, "%s(): chan_update failed", __func__);
		return -1;
	}

	return _tas_send_at(ch, NULL);
}

static int _tas_send_now_wait(struct channel *ch, void *data)
{
	uint64_t ts_ns = real_get_ns();
	if (chan_update(ch, ts_ns, data)) {
		ERROR(ch, "%s(): chan_update failed", __func__);
		return -1;
	}

	return _tas_send_at_wait(ch, NULL);
}

static int _cbs_send_at(struct channel *ch, uint64_t *tx_ns)
{
	uint64_t ts_now = real_get_ns();

	/* If tx_ns is set and is far enough into the future, sleep. */
	if (tx_ns && (*tx_ns-(100 * NS_IN_US)) > ts_now) {
		struct timespec ts_cpu = {
			.tv_sec = 0,
			.tv_nsec = *tx_ns - 100 * NS_IN_US,
		};

		ts_normalize(&ts_cpu);
		if (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts_cpu, NULL) == -1) {
			WARN(ch, "%s() Failed waiting before Tx! (%d : %s)\n",
				__func__, errno, strerror(errno));
		}
	}
	return sendto(ch->tx_sock,
		&ch->pdu,
		sizeof(struct avtpdu_cshdr) + ch->payload_size,
		0,
		(struct sockaddr *) &ch->sk_addr,
		sizeof(ch->sk_addr)) - sizeof(struct avtpdu_cshdr);
}

static int _cbs_send_at_wait(struct channel *ch, uint64_t *tx_ns)
{
	int res = _cbs_send_at(ch, tx_ns);
	if (res < 0)
		return res;

	*tx_ns += get_class_delay_bound_ns(ch);
	while(chan_delay(ch, *tx_ns) > 50*NS_IN_US) ;

	return res;
}

static int _cbs_send_now(struct channel *ch, void *data)
{
	uint64_t ts_ns = real_get_ns();
	if (chan_update(ch, ts_ns, data)) {
		ERROR(ch, "%s(): chan_update failed", __func__);
		return -1;
	}

	return _cbs_send_at(ch, NULL);
}

static int _cbs_send_now_wait(struct channel *ch, void *data)
{
	uint64_t ts_ns = real_get_ns();
	if (chan_update(ch, ts_ns, data)) {
		ERROR(ch, "%s(): chan_update failed", __func__);
		return -1;
	}

	return _cbs_send_at_wait(ch, NULL);
}

static struct chan_send_ops tas_ops = {
	.send_at       = _tas_send_at,
	.send_at_wait  = _tas_send_at_wait,
	.send_now      = _tas_send_now,
	.send_now_wait = _tas_send_now_wait,
};


static struct chan_send_ops cbs_ops = {
	.send_at       = _cbs_send_at,
	.send_at_wait  = _cbs_send_at_wait,
	.send_now      = _cbs_send_now,
	.send_now_wait = _cbs_send_now_wait,
};

bool nc_create_cbs_tx_sock(struct channel *ch)
{
	if (!ch)
		return false;
	ch->tx_sock_prio = ch->nh->tx_cbs_sock_prio;
	ch->ops = &cbs_ops;
	ch->tx_sock = _nc_create_tx_sock(ch);
	return ch->tx_sock > 0;
}

bool nc_create_tas_tx_sock(struct channel *ch)
{
	if (!ch)
		return false;

	ch->tx_sock_prio = ch->nh->tx_tas_sock_prio;
	ch->tx_sock = _nc_create_tx_sock(ch);
	if (ch->tx_sock < 0)
		return false;

	/* Set ETF-Qdisc clock field for socket
	 *
	 * Do NOT set SOF_TXTIME_DEADLINE_MODE, this will cause sch_etf
	 * to disregard SO_TXTIME and instead send frame with now +
	 * delta.
	 */
	ch->txtime.clockid = CLOCK_TAI;
	ch->txtime.flags = SOF_TXTIME_REPORT_ERRORS;
	if (setsockopt(ch->tx_sock, SOL_SOCKET, SO_TXTIME, &ch->txtime, sizeof(ch->txtime))) {
		close(ch->tx_sock);
		ch->tx_sock = -1;
		return false;
	}

	ch->ops = &tas_ops;

	return true;
}

int nc_handle_sock_err(int sock, int ptp_fd)
{
	struct pollfd p_fd = {
		.fd = sock,
	};
	int err = poll(&p_fd, 1, 0);

	if (err == 1 && (p_fd.revents & POLLERR)) {
		uint8_t msg_control[CMSG_SPACE(sizeof(struct sock_extended_err))];
		unsigned char err_buffer[2048];

		struct iovec iov = {
			.iov_base = err_buffer,
			.iov_len = sizeof(err_buffer)
		};
		struct msghdr msg = {
			.msg_iov = &iov,
			.msg_iovlen = 1,
			.msg_control = msg_control,
			.msg_controllen = sizeof(msg_control)
		};
		int64_t tai_ns = tai_get_ns();
		if (recvmsg(sock, &msg, MSG_ERRQUEUE) != -1) {
			struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
			while (cmsg != NULL) {
				struct sock_extended_err *serr = (void *) CMSG_DATA(cmsg);
				if (serr->ee_origin == SO_EE_ORIGIN_TXTIME) {
					/* The scheduled TxTime */
					uint64_t txtime_ns = ((uint64_t) serr->ee_data << 32) + serr->ee_info;
					double ptp_ts_ns = (double)get_ptp_ts_ns(ptp_fd);
					const char *reason;

					switch(serr->ee_code) {
					case SO_EE_CODE_TXTIME_INVALID_PARAM:
						reason = "invalid params";
						break;
					case SO_EE_CODE_TXTIME_MISSED:
						reason = "missed deadline";
						break;
					default:
						return -1;
					}
					ERROR(NULL, "[%"PRId64"] dropped, %s. TX to TAI: %.6f, PTP to TAI: %.6f",
						txtime_ns,
						reason,
						((double)txtime_ns - tai_ns)/1e9,
						(ptp_ts_ns - tai_ns)/1e9);

				}
				cmsg = CMSG_NXTHDR(&msg, cmsg);
			}
		}
	}
	return 0;
}
