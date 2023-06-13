#include <stdio.h>
#include <unistd.h>
#include <linux/net_tstamp.h>
#include <netchan.h>
#include <netinet/ether.h>
#include <arpa/inet.h>

int nc_create_rx_sock(void)
{
	int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
	if (sock == -1) {
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
		return -1;
	}

	int enable_ts = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS, &enable_ts, sizeof(enable_ts)) < 0) {
		fprintf(stderr, "%s(): failed enabling SO_TIMESTAMPNS on Rx socket (%d, %s)\n",
			__func__, errno, strerror(errno));
		return -1;
	}
	return sock;
}

int nc_create_tx_sock(struct channel *ch)
{
	int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
	if (sock == -1)
		return -1;

	/* Set destination address for outgoing traffict this DU */
	ch->sk_addr.sll_family   = AF_PACKET;
	ch->sk_addr.sll_protocol = htons(ETH_P_TSN);
	ch->sk_addr.sll_halen    = ETH_ALEN;
	ch->sk_addr.sll_ifindex  = ch->nh->ifidx;
	memcpy(&ch->sk_addr.sll_addr, ch->dst, ETH_ALEN);

	ch->txtime.clockid = CLOCK_TAI;
	ch->txtime.flags = SOF_TXTIME_DEADLINE_MODE | SOF_TXTIME_REPORT_ERRORS;

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

	if (setsockopt(sock, SOL_SOCKET, SO_TXTIME, &ch->txtime, sizeof(ch->txtime)))
		goto err_out;


	return sock;
err_out:
	close(sock);
	return -1;
}
