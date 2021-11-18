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
	struct ifreq ifr;
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", nf_nic);
	ioctl(sock, SIOCGIFINDEX, &ifr);

	/* Set target address */
	sk_addr->sll_family = AF_PACKET;
	sk_addr->sll_protocol = htons(ETH_P_TSN);
	sk_addr->sll_halen = ETH_ALEN;
	sk_addr->sll_ifindex = ifr.ifr_ifindex;
	memcpy(&sk_addr->sll_addr, net_fifo_chans[nfc_idx].mcast, ETH_ALEN);

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
	close(sock);
	return txsz - sizeof(*cshdr);
}
