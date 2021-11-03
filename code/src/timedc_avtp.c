#include <timedc_avtp.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_packet.h>
#include <netinet/ether.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <signal.h>

/* Rename experimental type to TimedC subtype for now */
#define AVTP_SUBTYPE_TIMEDC 0x7F


struct avtpdu_cshdr {
	uint8_t subtype;

	/* Network-order, bitfields reversed */
	uint8_t tv:1;		/* timestamp valid, 4.4.4. */
	uint8_t fsd:2;		/* format specific data */
	uint8_t mr:1;		/* medica clock restart */
	uint8_t version:3;	/* version, 4.4.3.4*/
	uint8_t sv:1;		/* stream_id valid, 4.4.4.2 */

	/* 4.4.4.6, inc. for each msg in stream, wrap from 0xff to 0x00,
	 * start at arbitrary position.
	 */
	uint8_t seqnr;

	/* Network-order, bitfields reversed */
	uint8_t tu:1;		/* timestamp uncertain, 4.4.4.7 */
	uint8_t fsd_1:7;

	/* EUI-48 MAC address + 16 bit unique ID
	 * 1722-2016, 4.4.4.8, 802.1Q-2014, 35.2.2.8(.2)
	 */
	uint64_t stream_id;

	/* gPTP timestamp, in ns, derived from gPTP, lower 32 bit,
	 * approx 4.29 timespan */
	uint32_t avtp_timestamp;
	uint32_t fsd_2;

	uint16_t sdl;		/* stream data length (octets/bytes) */
	uint16_t fsd_3;
} __attribute__((packed));

/**
 * timedc_avtp - Container for fifo/lvchan over TSN/AVB
 *
 * Internal bits including the payload itself that a channel will send
 * between tasks.
 *
 * @pdu: wrapper to AVTP Data Unit, common header
 * @payload_size: num bytes for the payload
 * @payload: grows at the end of the struct
 */
struct timedc_avtp
{
	struct nethandler *nh;

	uint8_t dst[ETH_ALEN];

	/* payload */
	uint16_t payload_size;

	struct avtpdu_cshdr pdu;
	unsigned char payload[0];
} __attribute__((__packed__));

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
	struct sockaddr_ll sk_addr;

	/* hashmap, chan_id -> cb_entity  */
	size_t hmap_sz;
	struct cb_entity *hmap;
};
static struct nethandler *_nh;

int get_chan_idx(char *name, const struct net_fifo *arr, int arr_size)
{
	for (int i = 0; i < arr_size; i++) {
		if (strncmp(name, arr[i].name, 32) == 0)
			return i;
	}
	return -1;
}


struct timedc_avtp * pdu_create(struct nethandler *nh,
				unsigned char *dst,
				uint64_t stream_id,
				uint16_t sz)
{
	struct timedc_avtp *pdu = malloc(sizeof(*pdu) + sz);
	if (!pdu)
		return NULL;

	memset(pdu, 0, sizeof(*pdu));
	pdu->pdu.subtype = AVTP_SUBTYPE_TIMEDC;
	pdu->pdu.stream_id = stream_id;
	pdu->payload_size = sz;
	pdu->nh = nh;
	memcpy(pdu->dst, dst, ETH_ALEN);

	return pdu;
}

struct timedc_avtp *pdu_create_standalone(char *name,
					bool tx_update,
					struct net_fifo *arr,
					int arr_size,
					unsigned char *nic,
					int hmap_size)
{
	if (!name || !arr || arr_size <= 0)
		return NULL;
	int idx = get_chan_idx(name, arr, arr_size);
	if (idx < 0)
		return NULL;
	if (!_nh) {
		_nh = nh_init(nic, hmap_size);
		if (!_nh)
			return NULL;
	}
	return pdu_create(_nh, arr[idx].mcast, arr[idx].stream_id, arr[idx].size);
}

void pdu_destroy(struct timedc_avtp **pdu)
{
	if (!*pdu)
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

int pdu_send(struct timedc_avtp *pdu)
{
	if (!pdu)
		return -ENOMEM;
	return -EINVAL;		/* not implemented yet */
}

void * pdu_get_payload(struct timedc_avtp *pdu)
{
	if (!pdu)
		return NULL;
	return (void *)pdu->payload;
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

	return sock;
}

struct nethandler * nh_init(unsigned char *ifname,
			size_t hmap_size)
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
	/* printf("Availalbe idx for %lu at %d\n", stream_id, idx); */
	nh->hmap[idx].stream_id = stream_id;
	nh->hmap[idx].priv_data = priv_data;
	nh->hmap[idx].cb = cb;

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
			nh_feed_pdu(nh, pdu);
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

	/* FIXME: must be done on each pdu */
/*
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
*/
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
