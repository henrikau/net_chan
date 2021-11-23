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
#include <unistd.h>
#include <time.h>

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
 */
struct cb_priv
{
	int sz;
	/* FIFO FD */
	int fd;
};

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
	struct timedc_avtp *next;

	pthread_t tx_tid;
	bool running;
	uint8_t dst[ETH_ALEN];
	int tx_sock;
	struct sockaddr_ll sk_addr;
	char name[32];

	/* private area for callback, embed directly i not struct to
	 * ease memory management. */
	struct cb_priv cbp;
	int fd_w;
	int fd_r;

	/* payload */
	uint16_t payload_size;

	struct avtpdu_cshdr pdu;
	unsigned char payload[0];
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

struct nethandler {
	struct timedc_avtp *du_tx_head;
	struct timedc_avtp *du_tx_tail;

	struct timedc_avtp *du_rx_head;
	struct timedc_avtp *du_rx_tail;

	/* network */
	int tx_sock;
	int rx_sock;
	struct sockaddr_ll sk_addr;
	char ifname[IFNAMSIZ];
	bool running;
	pthread_t tid;

	/* hashmap, chan_id -> cb_entity  */
	size_t hmap_sz;
	struct cb_entity *hmap;
};
static struct nethandler *_nh;

int nf_get_chan_idx(char *name, const struct net_fifo *arr, int arr_size)
{
	for (int i = 0; i < arr_size; i++) {
		if (strncmp(name, arr[i].name, 32) == 0)
			return i;
	}
	return -1;
}

struct net_fifo * nf_get_chan(char *name, const struct net_fifo *arr, int arr_size)
{
	if (!name || !arr || arr_size < 1)
		return NULL;
	int idx = nf_get_chan_idx(name, arr, arr_size);
	if (idx == -1)
		return NULL;

	struct net_fifo *nf = malloc(sizeof(*nf));
	if (!nf)
		return NULL;
	*nf = arr[idx];
	return nf;
}

const struct net_fifo * nf_get_chan_ref(char *name, const struct net_fifo *arr, int arr_size)
{
	if (!name || !arr || arr_size < 1)
		return NULL;
	int idx = nf_get_chan_idx(name, arr, arr_size);
	if (idx == -1)
		return NULL;

	return &arr[idx];
}

void * nf_tx_worker(void *data)
{
	struct timedc_avtp *du = (struct timedc_avtp *)data;
	if (!du)
		return NULL;

	char *buf = malloc(du->payload_size);
	if (!buf)
		return NULL;
	while (du->running) {

		int sz = read(du->fd_r, buf, du->payload_size);
		if (sz == -1) {
			perror("Failed reading data from pipe");
			continue;
		}
		if (pdu_send_now(du, buf) == -1)
			perror("failed sending data!");
	}

	free(buf);
	return NULL;
}

int nf_tx_create(char *name, struct net_fifo *arr, int arr_size)
{
	printf("%s(): starting netfifo Tx end\n", __func__);
	struct timedc_avtp *du = pdu_create_standalone(name, 1, arr, arr_size);
	if (!du)
		return -1;

	nh_add_tx(_nh, du);

	/* start thread to block on fd_r, return fd_w */
	du->running = true;
	pthread_create(&du->tx_tid, NULL, nf_tx_worker, du);
	printf("%s(): thread spawned (tid=%ld), ready to send data\n", __func__, du->tx_tid);
	return du->fd_w;
}

int nf_rx_create(char *name, struct net_fifo *arr, int arr_size)
{
	struct timedc_avtp *du = pdu_create_standalone(name, 0, arr, arr_size);
	if (!du)
		return -1;

	nh_add_rx(_nh, du);

	return du->fd_r;
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
	pdu->pdu.stream_id = htobe64(stream_id);
	pdu->pdu.sv = 1;
	pdu->pdu.seqnr = 0xff;
	pdu->payload_size = sz;
	pdu->nh = nh;
	memcpy(pdu->dst, dst, ETH_ALEN);

	pdu->tx_tid = 0;
	pdu->tx_sock = -1;
	pdu->running = false;
	return pdu;
}

int nf_set_nic(char *nic)
{
	strncpy(nf_nic, nic, IFNAMSIZ-1);
	printf("%s(): set nic to %s\n", __func__, nf_nic);
	return 0;
}

struct timedc_avtp *pdu_create_standalone(char *name,
					bool tx_update,
					struct net_fifo *arr,
					int arr_size)
{
	if (!name || !arr || arr_size <= 0)
		return NULL;
	printf("%s(): nic: %s\n", __func__, nf_nic);

	int idx = nf_get_chan_idx(name, arr, arr_size);
	if (idx < 0)
		return NULL;

	nh_init_standalone();
	if (!_nh)
		return NULL;

	printf("%s(): creating new DU, idx=%d, dst=%s\n", __func__, idx, ether_ntoa((const struct ether_addr *)arr[idx].dst));
	struct timedc_avtp * du = pdu_create(_nh, arr[idx].dst, arr[idx].stream_id, arr[idx].size);

	if (!du)
		return NULL;

	strncpy(du->name, name, 32);

	int pfd[2];
	if (pipe(pfd) == -1) {
		pdu_destroy(&du);
		return NULL;
	}
	du->fd_r = pfd[0];
	du->fd_w = pfd[1];

	/* if tx, create socket  */
	if (tx_update) {
		du->tx_sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
		if (du->tx_sock == -1) {
			fprintf(stderr, "%s(): Failed creating tx-socket for PDU (%lu) - %s\n",
				__func__, be64toh(du->pdu.stream_id), strerror(errno));
			pdu_destroy(&du);
			return NULL;
		}

		/* Set destination address for outgoing traffic t this DU */
		struct ifreq req;
		snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", _nh->ifname);
		int res = ioctl(du->tx_sock, SIOCGIFINDEX, &req);
		if (res < 0) {
			perror("Failed to get interface index");
			pdu_destroy(&du);
			return NULL;
		}
		struct sockaddr_ll *sk_addr = &du->sk_addr;
		sk_addr->sll_family = AF_PACKET;
		sk_addr->sll_protocol = htons(ETH_P_TSN);
		sk_addr->sll_halen = ETH_ALEN;
		sk_addr->sll_ifindex = req.ifr_ifindex;
		memcpy(sk_addr->sll_addr, du->dst, ETH_ALEN);
		printf("%s(): sending to %s\n", __func__, ether_ntoa((struct ether_addr *)&du->dst));
	} else {
		/* trigger on incoming DUs and attach a generic callback
		 * and write data into correct pipe
		 */
		du->cbp.fd = du->fd_w;
		du->cbp.sz = du->payload_size;
		nh_reg_callback(du->nh, arr[idx].stream_id, &du->cbp, nh_std_cb);
	}

	return du;
}

void pdu_destroy(struct timedc_avtp **pdu)
{
	if (!*pdu)
		return;

	/* close down tx-threads */
	if ((*pdu)->tx_tid > 0) {
		(*pdu)->running = false;
		unsigned char *d = malloc((*pdu)->payload_size);
		memset(d, 0, (*pdu)->payload_size);
		write((*pdu)->fd_w, d, (*pdu)->payload_size);

		usleep(1000);
		pthread_join((*pdu)->tx_tid, NULL);
		(*pdu)->tx_tid = 0;
		free(d);
	}

	if ((*pdu)->fd_r >= 0)
		close((*pdu)->fd_r);
	if ((*pdu)->fd_w >= 0)
		close((*pdu)->fd_w);
	if ((*pdu)->tx_sock >= 0) {
		close((*pdu)->tx_sock);
		(*pdu)->tx_sock = -1;
	}
	free(*pdu);
	*pdu = NULL;
}

int pdu_update(struct timedc_avtp *pdu, uint32_t ts, void *data)
{
	if (!pdu)
		return -ENOMEM;

	if (!data)
		return -ENOMEM;
	pdu->pdu.seqnr++;
	pdu->pdu.avtp_timestamp = ts;
	pdu->pdu.tv = 1;
	memcpy(pdu->payload, data, pdu->payload_size);
	return 0;
}

int pdu_send(struct timedc_avtp *du)
{
	if (!du)
		return -ENOMEM;
	if (!du->nh)
		return -EINVAL;
	if (du->tx_sock == -1)
		return -EINVAL;

	int txsz = sendto(du->tx_sock,
			&du->pdu,
			sizeof(struct avtpdu_cshdr) + du->payload_size,
			0,
			(struct sockaddr *) &du->sk_addr,
			sizeof(du->sk_addr));
	if (txsz < 0)
		perror("pdu_send()");

	return txsz;
}

int pdu_send_now(struct timedc_avtp *du, void *data)
{
		struct timespec tv;
		int cres = clock_gettime(CLOCK_TAI, &tv);
		if (cres == -1) {
			perror("failed reading clock");
			return -1;
		}

		uint32_t ts_ns = (uint32_t)(tv.tv_sec * 1e9 + tv.tv_nsec);
		if (pdu_update(du, ts_ns, data)) {
			printf("%s(): pdu_update failed\n", __func__);
			return -1;
		}

		return pdu_send(du);
}

void * pdu_get_payload(struct timedc_avtp *pdu)
{
	if (!pdu)
		return NULL;
	return (void *)pdu->payload;
}


/* DEPRECATED, should not store this in nethandler */
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
		printf("%s(): Could not get interface index for %s: %s\n", __func__, nh->ifname, strerror(errno));
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
			perror("Failed placing lo in promiscuous mode, will not receive incoming data (tests may fail)");
			return -1;
		}
	}
	return sock;
}

struct nethandler * nh_init(char *ifname, size_t hmap_size)
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

	return nh;
}

int nh_init_standalone(void)
{
	/* avoid double-create */
	if (_nh != NULL)
		return -1;
	_nh = nh_init(nf_nic, nf_hmap_size);
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

	void *payload = (void *)du + sizeof(*du);
	int wsz = write(cbp->fd, payload, cbp->sz);

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

int nh_feed_pdu(struct nethandler *nh, struct avtpdu_cshdr *cshdr)
{
	if (!nh || !cshdr)
		return -EINVAL;
	int idx = get_hm_idx(nh, be64toh(cshdr->stream_id));

	if (idx >= 0)
		return nh->hmap[idx].cb(nh->hmap[idx].priv_data, cshdr);

	/* no callback registred, though not exactly an FD-error */
	return -EBADFD;
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
	unsigned char buffer[1522] = {0};
	bool running = true;
	while (running) {
		int n = recv(nh->rx_sock, buffer, 1522, 0);
		running = nh->running;
		if (n > 0) {
			struct avtpdu_cshdr *du = (struct avtpdu_cshdr *)buffer;
			/* printf("%s() got data (stream_id=%ld, size=%d), feeding pdu to callback\n", */
			/* 	__func__, be64toh(du->stream_id), n); */
			nh_feed_pdu(nh, du);
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

void nf_stop_rx(struct nethandler *nh)
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

int _nh_get_len(struct timedc_avtp *head)
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

int nh_add_tx(struct nethandler *nh, struct timedc_avtp *du)
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

int nh_add_rx(struct nethandler *nh, struct timedc_avtp *du)
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
		nf_stop_rx(*nh);

		/* close down and exit safely */
		if ((*nh)->hmap != NULL)
			free((*nh)->hmap);

		/* clean up TX PDUs */
		while ((*nh)->du_tx_head) {
			struct timedc_avtp *pdu = (*nh)->du_tx_head;
			(*nh)->du_tx_head = (*nh)->du_tx_head->next;
			pdu_destroy(&pdu);
		}

		/* clean up Rx PDUs */
		while ((*nh)->du_rx_head) {
			struct timedc_avtp *pdu = (*nh)->du_rx_head;
			(*nh)->du_rx_head = (*nh)->du_rx_head->next;
			pdu_destroy(&pdu);
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
