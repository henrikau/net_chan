#include <stdio.h>
#include <unistd.h>
#include <linux/net_tstamp.h>
#include <linux/if.h>
#include <netchan.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <linux/errqueue.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <sys/types.h>
#include <sys/socket.h>

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

int nc_create_tx_sock(struct nethandler *nh)
{
	if (!nh)
		return -EINVAL;

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
	if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &nh->tx_sock_prio, sizeof(nh->tx_sock_prio)) < 0) {
		ERROR(NULL, "%s(): failed setting socket priority (%d, %s)",
			__func__, errno, strerror(errno));
		goto err_out;
	}

	/* Set ETF-Qdisc clock field for socket */
	nh->txtime.clockid = CLOCK_TAI;
	nh->txtime.flags = SOF_TXTIME_REPORT_ERRORS | SOF_TXTIME_DEADLINE_MODE;
	if (setsockopt(sock, SOL_SOCKET, SO_TXTIME, &nh->txtime, sizeof(nh->txtime)))
		goto err_out;

	return sock;
err_out:
	close(sock);
	return -1;
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
