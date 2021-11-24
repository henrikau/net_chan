#pragma once
#include "test_net_fifo.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ether.h>

#include <linux/if_ether.h>
#include <linux/if.h>
#include <linux/if_packet.h>
#include <sys/ioctl.h>
#include <unistd.h>

static inline int helper_create_socket(int nfc_idx, struct sockaddr_ll *sk_addr)
{
	int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));

	/* Get nic idx */
	struct ifreq ifr = {0};
	if (snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", "lo") != 2) {
		printf("%s(): FAILED writing ifr_name ('lo') to ifreq\n", __func__);
		return -1;
	}

	if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
		printf("%s(): ioctl failed: %s (nic=%s)\n",__func__, strerror(errno), ifr.ifr_name);
		return -1;
	}

	/* Set target address */
	sk_addr->sll_family = AF_PACKET;
	sk_addr->sll_protocol = htons(ETH_P_TSN);
	sk_addr->sll_halen = ETH_ALEN;
	sk_addr->sll_ifindex = ifr.ifr_ifindex;
	memcpy(&sk_addr->sll_addr, net_fifo_chans[nfc_idx].dst, ETH_ALEN);

	return sock;
}

static inline int helper_send_8byte(int nfc_idx, uint64_t data)
{
	struct sockaddr_ll sk_addr = {0};
	int sock = helper_create_socket(nfc_idx, &sk_addr);

	char buffer[1500] = {0};
	struct avtpdu_cshdr *cshdr = (struct avtpdu_cshdr *)buffer;
	uint64_t *d = (uint64_t *)(buffer + sizeof(*cshdr));

	cshdr->subtype = AVTP_SUBTYPE_TIMEDC;
	cshdr->stream_id = htobe64(net_fifo_chans[nfc_idx].stream_id);
	*d = data;

	int txsz = sendto(sock, buffer,
			sizeof(*cshdr) + sizeof(uint64_t), 0,
			(struct sockaddr *) &sk_addr,
			sizeof(sk_addr));
	if (txsz == -1) {
		printf("%s(): Failed sending to remote via 'lo', %s\n", __func__, strerror(errno));
		return -1;
	}

	printf("%s(): txsz=%d\n", __func__, txsz);
	close(sock);
	return txsz - sizeof(*cshdr);
}

static inline int helper_recv(int idx, char *data, int retries, const char *iface)
{
	int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_TSN));

	struct ifreq req = {0};
	if (snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", iface) != strlen(iface)) {
		printf("%s(): FAILED copyting ifname (%s) to ifreq\n", __func__, iface);
		return -1;
	}

	if (ioctl(sock, SIOCGIFFLAGS, &req) == -1) {
		perror("Failed retrieveing flags for lo");
		return -1;
	}
	req.ifr_flags |= IFF_PROMISC;
	if (ioctl(sock, SIOCSIFFLAGS, &req) == -1) {
		perror("Failed placing lo in promiscuous mode, will not receive incoming data (tests may fail)");
		return -1;
	}

	/* Set a short timeout in case we're not sending as expected, let test return */
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1000000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

	char buffer[1500] = {0};
	while (retries-- > 0) {
		int rxsz = recv(sock, buffer, 1522, 0);
		if (rxsz < 0) {
			printf("recv() failed, retries=%d, %s\n", retries, strerror(errno));
			continue;
		}
		struct ether_header *hdr = (struct ether_header *)buffer;
		struct avtpdu_cshdr *cshdr = (struct avtpdu_cshdr *)((void *)&buffer[0] + sizeof(*hdr));
		size_t sz = net_fifo_chans[idx].size;

		if (be64toh(cshdr->stream_id) == net_fifo_chans[idx].stream_id &&
			rxsz == (sizeof(*hdr) + sizeof(*cshdr) + sz)) {
			char *d = &buffer[0] + sizeof(*hdr) + sizeof(*cshdr);
			memcpy(data, d, sz);
			close(sock);
			return sz;
		}
	}
	close(sock);
	return 0;
}
