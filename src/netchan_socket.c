#include <stdio.h>
#include <unistd.h>
#include <linux/net_tstamp.h>
#include <netchan.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <linux/errqueue.h>
#include <poll.h>

int nc_create_rx_sock(void)
{
	int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
	if (sock < 0) {
		fprintf(stderr, "%s(): Failed creating socket: %s\n", __func__, strerror(errno));
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
		printf("%s(): Could not set timeout on socket (%d): %s\n",
			__func__, sock, strerror(errno));
		close(sock);
		return -1;
	}

	int enable_ts = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS, &enable_ts, sizeof(enable_ts)) < 0) {
		fprintf(stderr, "%s(): failed enabling SO_TIMESTAMPNS on Rx socket (%d, %s)\n",
			__func__, errno, strerror(errno));
		close(sock);
		return -1;
	}
	return sock;
}

int nc_create_tx_sock(struct channel *ch)
{
	if (!ch)
		return -EINVAL;

	int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
	if (sock < 0) {
		fprintf(stderr, "%s(): Failed creating Tx-socket: %s\n", __func__, strerror(errno));
		return -1;
	}

	/* Set socket priority option (for sending to the right socket)
	 *
	 * FIXME: allow for outside config of socket prio (see
	 * scripts/setup_nic.sh)
	 */
	if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &ch->tx_sock_prio, sizeof(ch->tx_sock_prio)) < 0) {
		fprintf(stderr, "%s(): failed setting socket priority (%d, %s)\n",
			__func__, errno, strerror(errno));
		goto err_out;
	}

	/* Set ETF-Qdisc clock field for socket */
	ch->txtime.clockid = CLOCK_TAI;
	ch->txtime.flags = SOF_TXTIME_REPORT_ERRORS | SOF_TXTIME_DEADLINE_MODE;
	if (setsockopt(sock, SOL_SOCKET, SO_TXTIME, &ch->txtime, sizeof(ch->txtime)))
		goto err_out;

	/* Set destination address for outgoing traffict this DU */
	ch->sk_addr.sll_family   = AF_PACKET;
	ch->sk_addr.sll_protocol = htons(ETH_P_TSN);
	ch->sk_addr.sll_halen    = ETH_ALEN;
	ch->sk_addr.sll_ifindex  = ch->nh->ifidx;
	memcpy(&ch->sk_addr.sll_addr, ch->dst, ETH_ALEN);

	return sock;
err_out:
	close(sock);
	return -1;
}

int nc_handle_sock_err(int sock)
{
	struct pollfd p_fd = {
		.fd = sock,
	};
	int err = poll(&p_fd, 1, 0);

	if (err == 1 && p_fd.revents == POLLERR) {
		printf("%s(): Need to process errors\n", __func__);
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

		if (recvmsg(sock, &msg, MSG_ERRQUEUE) != -1) {
			struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
			while (cmsg != NULL) {
				struct sock_extended_err *serr = (void *) CMSG_DATA(cmsg);
				if (serr->ee_origin == SO_EE_ORIGIN_TXTIME) {
					uint64_t tstamp = ((__u64) serr->ee_data << 32) + serr->ee_info;
					switch(serr->ee_code) {
					case SO_EE_CODE_TXTIME_INVALID_PARAM:

						fprintf(stderr, "packet with tstamp %"PRIu64" dropped due to invalid params\n", tstamp);
						return -1;
					case SO_EE_CODE_TXTIME_MISSED:
						fprintf(stderr, "packet with tstamp %"PRIu64" dropped due to missed deadline\n", tstamp);
						return -1;
					default:
						return -1;
					}
				}
				cmsg = CMSG_NXTHDR(&msg, cmsg);

			}
		}
	}
	return 0;
}
