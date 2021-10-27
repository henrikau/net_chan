#include <common.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/ether.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <signal.h>

struct cb_entity {
	uint64_t stream_id;
	void *priv_data;
	int (*cb)(void *priv_data, struct timedc_avtp *pdu);
};

struct nethandler {
	/* receiver */
	int tx_sock;
	int rx_sock;
	bool running;
	pthread_t tid;
        uint8_t src_mac[ETH_ALEN]; /* MAC of local NIC */
	uint8_t dst_mac[ETH_ALEN];
	struct sockaddr_ll sk_addr;

	/* hashmap, chan_id -> cb_entity  */
	size_t hmap_sz;
	struct cb_entity *hmap;
};

struct timedc_avtp * pdu_create(uint64_t stream_id, uint16_t sz)
{
	struct timedc_avtp *pdu = malloc(sizeof(*pdu)+sz);
	if (!pdu)
		return NULL;

	memset(pdu, 0, sizeof(*pdu));
	pdu->pdu.subtype = AVTP_SUBTYPE_TIMEDC;
	pdu->pdu.stream_id = stream_id;
	pdu->payload_size = sz;
	return pdu;
}

void pdu_destroy(struct timedc_avtp **pdu)
{
	if(!*pdu)
		return;
	free(*pdu);
	*pdu = NULL;
}

int pdu_update(struct timedc_avtp *pdu, uint32_t ts, void *data, size_t sz)
{
	if (!pdu)
		return -ENOMEM;

	if (sz > pdu->payload_size)
		return -EMSGSIZE;

	if (!data)
		return -ENOMEM;

	pdu->pdu.avtp_timestamp = ts;
	pdu->pdu.tv = 1;
	memcpy(pdu->payload, data, sz);
	return 0;
}

static int _nh_socket_setup_common(struct nethandler *nh, const unsigned char *ifname)
{
	int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
	if (sock == -1) {
		perror("Failed opening socket");
		return -1;
	}

	struct ifreq req;
	snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", ifname);
	if (ioctl(sock, SIOCGIFINDEX, &req) == -1) {
		perror("Could not get interface index");
		return -1;
	}

	nh->sk_addr.sll_family = AF_PACKET;
	nh->sk_addr.sll_protocol = htons(ETH_P_TSN);
	nh->sk_addr.sll_halen = ETH_ALEN;
	nh->sk_addr.sll_ifindex = req.ifr_ifindex;

	memcpy(nh->sk_addr.sll_addr, nh->dst_mac, ETH_ALEN);

	return sock;
}

struct nethandler * nh_init(const unsigned char *ifname,
			size_t hmap_size,
			const unsigned char *dst_mac)
{
	struct nethandler *nh = calloc(sizeof(*nh), 1);
	if (!nh)
		return NULL;

	nh->hmap_sz = hmap_size;
	nh->hmap = calloc(sizeof(struct cb_entity), nh->hmap_sz);
	if (!nh->hmap) {
		free(nh);
		return NULL;
	}
	memcpy(nh->dst_mac, dst_mac, ETH_ALEN);
	nh->tx_sock = _nh_socket_setup_common(nh, ifname);
	nh->rx_sock = _nh_socket_setup_common(nh, ifname);

	return nh;
}

int nh_reg_callback(struct nethandler *nh,
		uint64_t stream_id,
		void *priv_data,
		int (*cb)(void *priv_data, struct timedc_avtp *pdu))
{
	if (!nh || !nh->hmap_sz || !cb)
		return -EINVAL;

	int idx = stream_id % nh->hmap_sz;

	for (int i = 0; i < nh->hmap_sz && nh->hmap[idx].cb; i++)
		idx = (idx + 1) % nh->hmap_sz;

	if (nh->hmap[idx].cb) {
		printf("Found no available slots (stream_id=%lu, idx=%d -> %p)\n", stream_id, idx, nh->hmap[idx].cb);
		return -1;
	}
	printf("Availalbe idx for %lu at %d\n", stream_id, idx);
	nh->hmap[idx].stream_id = stream_id;
	nh->hmap[idx].priv_data = priv_data;
	nh->hmap[idx].cb = cb;

	return 0;
}

static int get_hm_idx(struct nethandler *nh, uint64_t stream_id)
{
	for (int i = 0; i < nh->hmap_sz; i++) {
		int idx = (idx + i) % nh->hmap_sz;
		if (nh->hmap[idx].stream_id == stream_id && nh->hmap[idx].cb)
			return idx;
	}
	return -1;
}

int nh_feed_pdu(struct nethandler *nh, struct timedc_avtp *pdu)
{
	if (!nh || !pdu)
		return -EINVAL;
	int idx = get_hm_idx(nh, pdu->pdu.stream_id);
	if (idx >= 0)
		return nh->hmap[idx].cb(nh->hmap[idx].priv_data, pdu);

	/* no callback registred, though not exactly an FD-error< */
	return -EBADFD;
}

static void * nh_runner(void *data)
{
	if (!data)
		return NULL;

	struct nethandler *nh = (struct nethandler *)data;
	if (nh->rx_sock == -1)
		return NULL;

	unsigned char buffer[1522] = {0};
	nh->running = true;
	while (nh->running) {
		int n = recv(nh->rx_sock, buffer, 1522, 0);
		if (n > 0) {
			struct timedc_avtp *pdu = (struct timedc_avtp *)buffer;
			printf("Got packet with stream_id %lu\n", pdu->pdu.stream_id);
		}

		printf("\n\nrecv() returned with %d\n\n", n);
		nh->running = false;
	}
	return NULL;
}

int nh_start_rx(struct nethandler *nh)
{
	if (!nh)
		return -1;
	if (nh->rx_sock == -1)
		return -1;

	if (nh->dst_mac[0] == 0x01 &&
		nh->dst_mac[1] == 0x00 &&
		nh->dst_mac[2] == 0x5e) {
		struct packet_mreq mreq;
		mreq.mr_ifindex = nh->sk_addr.sll_ifindex;
		mreq.mr_type = PACKET_MR_MULTICAST;
		mreq.mr_alen = ETH_ALEN;
		memcpy(&mreq.mr_address, nh->dst_mac, ETH_ALEN);

		if (setsockopt(nh->rx_sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
				&mreq, sizeof(struct packet_mreq)) < 0) {
			perror("Couldn't set PACKET_ADD_MEMBERSHIP");
			nh->running = false;
			return -1;
		}
	}

	pthread_create(&nh->tid, NULL, nh_runner, nh);

	return 0;
}

void nh_destroy(struct nethandler **nh)
{
	if (*nh) {
		(*nh)->running = false;
		if ((*nh)->tid > 0) {
			/* This will abort the entire thread, should rather send a frame to self to wake recv() */
			pthread_kill((*nh)->tid, SIGINT);
			pthread_join((*nh)->tid, NULL);
		}
		/* close down and exit safely */
		if ((*nh)->hmap != NULL)
			free((*nh)->hmap);

		/* Free memory */
		free(*nh);
	}
	*nh = NULL;
}
