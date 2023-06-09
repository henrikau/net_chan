/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
/**
 * \package netchan
 *
 * Reliable, or deterministic **network channel** (*net_chan*) is a logical
 * construct that can be added to a distributed system to provide
 * deterministic and reliable connections. The core idea is to make it
 * simple to express the traffic for a channel in a concise, provable
 * manner and provide constructs for creating and using the channels.
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <linux/if_ether.h>

#include <ptp_getclock.h>

#include <linux/if_packet.h>	/* sk_addr */

/* --------------------------
 * Main NetChan Macros
 *
 * These macros rely heavily upon a sort-of singleton approach. If you
 * are concerned with interference from other threads, then avoid the
 * macros and use the non-standalone functions.
 */
#define NETCHAN_RX(x) struct channel * x ## _du = \
		chan_create_standalone(#x, 0, net_fifo_chans, \
		ARRAY_SIZE(net_fifo_chans))
#define NETCHAN_TX(x) struct channel * x ## _du = \
		chan_create_standalone(#x, 1, net_fifo_chans, \
		ARRAY_SIZE(net_fifo_chans))

#define WRITE(x,v) pdu_send_now(x ## _du, v)
#define WRITE_WAIT(x,v) pdu_send_now_wait(x ## _du, v)
#define READ(x,v) pdu_read(x ## _du, v)
#define READ_WAIT(x,v) pdu_read_wait(x ## _du, v)

#define CLEANUP() nh_destroy_standalone()


/* Empty mac multicast (ip multicast should be appended (low order 23
 * bit to low order 23)
 */
#define DEFAULT_MCAST {0x01, 0x00, 0x5E, 0x00, 0x00, 0x00}

enum {
	DEFAULT_CLASS_A_PRIO = 3,
	DEFAULT_CLASS_B_PRIO = 2
};

enum stream_class {
	CLASS_A,
	CLASS_B
};


/* StreamID u64 to bytearray wrapper */
union stream_id_wrapper {
	uint64_t s64;
	uint8_t s8[8];
};

/**
 * struct net_fifo
 *
 * This struct is used by client to populate a set of channels, by
 * calling nh_create_default, it will read "net_fifo.h" expected to be
 * in the standard include-path and create all the channels.
 *
 * The channels will be assigned to a static, global scope.
 */
struct net_fifo
{
	/* dst: address to which the talker will publish
	 *
	 * This can be any valid MAC-address:
	 * - unicast
	 * - multicast (starts with 01:00:5e)
	 * - broadcast (ff:ff:ff:ff:ff:ff)
	 */
	uint8_t dst[ETH_ALEN];
	uint64_t stream_id;

	/*
	 * Indicate if stream should run as class A or B
	 *
	 * Will recover the actual PCP prio values from SRP, if run
	 * without SRP, it will use default values (A:3, B:2)
	 *
	 * Note: when using WRITE_WAIT and READ_WAIT, thread will block
	 * until capture_time + class_max_delay has passed.
	 * For class A:  2ms
	 *     class B: 50ms
	 */
	enum stream_class sc;

	/* Size of payload, 0..1500 bytes */
	uint16_t size;

	/* Minimum distance between each frame, in ns (1/freq) */
	uint64_t interval_ns;

	/* Name of net_fifo (used by macros to create variable)
	 * It expects a callback
	 */
	char name[32];
};

/* Rename experimental type to NETCHAN subtype for now */
#define AVTP_SUBTYPE_NETCHAN 0x7F

/**
 * AVB Transport Protocol Data Unit Common Header
 */
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
 * netchan_avtp - Container for fifo/lvchan over TSN/AVB
 *
 * Internal bits including the payload itself that a channel will send
 * between tasks.
 *
 * @pdu: wrapper to AVTP Data Unit, common header
 * @payload_size: num bytes for the payload
 * @payload: grows at the end of the struct
 */
struct channel
{
	struct nethandler *nh;
	struct channel *next;


	bool running;

	struct sockaddr_ll sk_addr;
	uint8_t dst[ETH_ALEN];

	/* iface name */
	char name[32];


	/*
	 * Each outgoing stream has its own socket, with corresponding
	 * SRP attributes etc.
	 */
	int tx_sock;
	pthread_t tx_tid;

	/*
	 * payload (avtpdu_cshdr) uses htobe64 encoded streamid, we need
	 * it as a byte array for mrp, so keep an easy ref to it here
	 */
	union stream_id_wrapper sidw;

	/*
	 * Each outgoing stream is mapped ot its own socket, so it makes
	 * sense to keep track of this here. Similarly, each incoming
	 * stream has to keep track of which talker to subscribe to, so
	 * keep context for this here..
	 *
	 * MRP client has its own section for talker and listener, with
	 * dedicated fields for strem_id, mac etc.
	 */
	struct mrp_ctx *ctx;
	struct mrp_domain_attr *class_a;
	struct mrp_domain_attr *class_b;
	enum stream_class sc;
	int socket_prio;

	/* private area for callback, embed directly i not struct to
	 * ease memory management. */
	struct cb_priv *cbp;
	int fd_w;
	int fd_r;

	/* payload size */
	uint16_t payload_size;

	/*
	 * To enforce the channel frequency, keep track of next time
	 * this channel is eligble to transmit.
	 *
	 * The channel will schedule the packet to transmit at
	 *
	 * Common case (tx frequency is not higher than reserved):
	 *
	 *      last_tx     next_tx   t_now
	 * -------|---------|---------|------>
	 *
	 * We can send immediately.
	 *
	 *
	 *  If we overproduce:
	 *
	 *        last_tx   t_now    next_tx
	 * -------|---------|--------|------>
	 *
	 * we have to wait before we can send as next_tx is still in the future.
	 */
	uint64_t interval_ns;
	uint64_t next_tx_ns;

	/* The currently active packet entering/leaving is tacked onto
	 * the end, payload is of size '->payload_size'
	 */
	struct avtpdu_cshdr pdu;
	unsigned char payload[0];
};

struct nethandler;

int nc_set_nic(char *nic);
void nc_keep_cstate();
void nc_set_hmap_size(int sz);
void nc_use_srp(void);
void nc_use_ftrace(void);
void nc_breakval(int break_us);
void nc_verbose(void);
void nc_use_termtag(const char *devpath);
void nc_set_logfile(const char *logfile);
void nc_log_delay(void);

#define ARRAY_SIZE(x) (x != NULL ? sizeof(x) / sizeof(x[0]) : -1)

/* Get idx of a channel based on the name
 */
int nc_get_chan_idx(char *name, const struct net_fifo *arr, int arr_size);
#define NC_CHAN_IDX(x) (nc_get_chan_idx((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans)))

struct net_fifo * nc_get_chan(char *name, const struct net_fifo *arr, int arr_size);
const struct net_fifo * nc_get_chan_ref(char *name, const struct net_fifo *arr, int arr_size);

int nc_rx_create(char *name, struct net_fifo *arr, int arr_size);

#define NC_GET(x) (nc_get_chan((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans)))
#define NC_GET_REF(x) (nc_get_chan_ref((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans)))

/**
 * chan_create - create and initialize a new channel.
 *
 * The common workflow is that a channel is created once for a stream and
 * then updated before sending. If the traffic is so dense that the
 * network layer has not finished with it before a new must be
 * assembled, caller should create multiple channels..
 *
 * For convenience, the flow is more like:
 * CREATE
 * while (1)
 *    UPDATE -> SEND
 * DESTROY
 *
 * @param nh nethandler container
 * @param dst destination address for this channel
 * @param stream_id 64 bit unique value for the stream allotted to our channel.
 * @param cl: stream class this stream belongs to, required to set correct socket prio
 * @param sz: size of data to transmit
 * @param interval_ns: the minimum time between subsequent frames (1/freq)
 *
 * @returns the new channel or NULL upon failure.
 */
struct channel * chan_create(struct nethandler *nh,
			unsigned char *dst,
			uint64_t stream_id,
			enum stream_class sc,
			uint16_t sz,
			uint64_t interval_ns);

/**
 * chan_create_standalone - create a new channel using internal refs as much as possible
 *
 * Intended to be used when retrieving values from struct net_fifo and
 * other magic constants in net_fifo.h
 *
 * @param name : name of net_fifo
 * @param tx : tx or rx end of net_fifo
 * @param arr : net_fifo array of values
 * @param arr_size : size of net_fifo array
 *
 * @returns new channel, NULL on error
 */
struct channel *chan_create_standalone(char *name,
				bool tx,
				struct net_fifo *arr,
				int arr_size);

/**
 * pdu_get_payload
 *
 * @param pdu AVTP dataunit
 * @returns pointer to payload
 */
void * pdu_get_payload(struct channel *pdu);

/**
 * pdu_destroy : clean up and destroy a pdu
 *
 * @param pdu: pointer to pdu
 */
void pdu_destroy(struct channel **pdu);

/**
 * pdu_update : take an existing PDU and update timestamp and data and make it ready for Tx
 *
 * The function expects the size of data to be the same size as when it was created
 *
 * @param pdu: pdu to update
 * @param ts: avtp timestamp (lower 32 bit of PTP timestamp)
 * @param data: data to copy into payload (size is fixed from chan_create())
 *
 * @returns 0 on success, errno on failure.
 */
int pdu_update(struct channel *pdu, uint32_t ts, void *data);

/**
 * pdu_send : send payload of netchan data unit
 *
 * This will extract the AVTP payload from the PDU and send it to the
 * correct destination MAC.
 *
 * @params: pdu
 * @returns: 0 on success negative value on error.
 */
int pdu_send(struct channel *pdu);

/**
 * pdu_send_now: update and send pdu *now*
 *
 * This helper function will take a DU, increment seqnr, set timestamp
 * to time *now* and send it.
 *
 * @param pdu: data container to send
 * @param data: new data to copy into field and send.
 *
 * @return 0 on success, negative on error
 */
int pdu_send_now(struct channel *du, void *data);

/**
 * pdu_send_now_wait - update and send PDU, and wait for class delay
 *
 * When using this function, caller will be blocked for 2ms or 50ms
 * depending on class before continuing. This gives a synchronized wait
 * approach to the semantics.
 *
 * @param pdu: data container to send
 * @param data: new data to copy into field and send.
 *
 * @return 0 on success, negative on error
 */
int pdu_send_now_wait(struct channel *du, void *data);

/**
 * pdu_read : read data from incoming pipe attached to DU
 *
 * @param du: data container
 * @param data: memory to store received data to
 *
 * @return bytes received or -1 on error
 */
int pdu_read(struct channel *du, void *data);

/**
 * pdu_read_wait : read data from incoming pipe attached to DU and wait for class delay
 *
 * When using this function, caller will be delayed until capture time +
 * class delay has passed. This expects the clocks for both writer and
 * reader to be synchronized properly.
 *
 * @param du: data container
 * @param data: memory to store received data to
 *
 * @return bytes received or -1 on error
 */
int pdu_read_wait(struct channel *du, void *data);

/**
 * nh_create_init - create and initialize nethandler
 *
 * @param ifname: NIC to attach to
 * @param hmap_size: sizeof incoming frame hashmap
 *
 * @returns struct nethandler on success, NULL on error
 */
struct nethandler * nh_create_init(char *ifname, size_t hmap_size, const char *logfile);

/**
 * nh_create_init_standalone - create and initialize a standalone instance of nethandler
 *
 * This creates a 'hidden' nethandler instance kept by the library. It
 * is intended to be used alongside the various macros (in particular
 * NETCHAN_(T|R)X_CREATE()) to hide away resource management etc.
 *
 * It will use the values stored in nc_nic (see nc_set_nic) and
 * nc_hmap_size.
 *
 * @params : void
 * @returns: 0 on success, -1 on error
 */
int nh_create_init_standalone(void);


/**
 * nh_reg_callback - Register a callback for a given stream_id
 *
 * Whenever a new frame arrives with the registred streamID, the
 * provided callback will be invoked with the captured pdu
 *
 * Note: this is done from the receiver thread, so cb should should be
 * non-blocking and fast.
 *
 * @param nh nethandler container
 * @param stream_id Unique StreamID for the network
 * @param priv_data: private data used by the callback (to keep state between invocations)
 * @param cb: callback function pointer
 *
 * @returns negative (-EINVAL) on error, 0 on success
 */
int nh_reg_callback(struct nethandler *nh,
		uint64_t stream_id,
		void *priv_data,
		int (*cb)(void *priv_data, struct avtpdu_cshdr *du));


/**
 * nethandler standard callback
 *
 * This is the *standard* callback function. You can create your own if
 * you like, but this is what the framework will use when creating new
 * FIFOs, lvchans etc.
 *
 * @param data: private data field
 * @param du: incoming data unit from the network layer.
 *
 * @returns: bytes written, negative code on error
 */
int nh_std_cb(void *data, struct avtpdu_cshdr *du);

/**
 * nh_feed_pdu - feed a avtpdu to nethandler which will be passed to relevant callback
 *
 * note: current implementation only supports one callback per StreamID,
 * if multiple callbacks are required for a streamid, then the callback
 * registred with nethandler must implement the multiplexing.
 *
 * @param nh: nethandler container
 * @param avtp_du: received pdu
 *
 * @returns
 */
int nh_feed_pdu(struct nethandler *nh, struct avtpdu_cshdr *du);
int nh_feed_pdu_ts(struct nethandler *nh, struct avtpdu_cshdr *du,
		uint64_t rx_hw_ns,
		uint64_t recv_ptp_ns);

/**
 * nh_start_rx - start receiver thread
 *
 * This will filter out and grab all TSN ethertype frames coming in on
 * the socket. PDUs will known StreamIDs will be fed to registred
 * callbacks.
 *
 * @param nh nethandler container
 * @returns 0 on success, negative on error
 */
int nh_start_rx(struct nethandler *nh);
void nh_stop_rx(struct nethandler *nh);

/**
 * nh_get_num_(tx|rx) : get the number of Tx or Rx pipes registred
 *
 * @param: nh nethandler container
 * @returns: number of registered channels
 */
int nh_get_num_tx(struct nethandler *nh);
int nh_get_num_rx(struct nethandler *nh);

/**
 * nh_add_(tx|rx) - add a new outgoing or incoming channel
 *
 * nh_add_tx: channels that send data *from* this node
 * nh_add_rx: channels that receive data from outside and return to a task *in* this node.
 *
 * Note: this is expected to be static, so we don't have the option to
 * remove single items, only when completely destroying the handler.
 *
 * @param: nh nethandler container
 * @param: du: new NetChan Data-unit
 */
int nh_add_tx(struct nethandler *nh, struct channel *du);
int nh_add_rx(struct nethandler *nh, struct channel *du);

/**
 * nh_destroy: safely destroy nethandler. If _rx is running, it will be stopped.
 *
 * @param nh: indirect ref to nh pointer (caller's ref will be NULL'd)
 */
void nh_destroy(struct nethandler **nh);

/**
 * nh_destroy_standalone: destroy singular nethandler created by nh_create_init_standalone()
 *
 * @param void
 * @return: void
 */
void nh_destroy_standalone();

/**
 * get_class_delay_ns(): get the correct delay offset for the current class
 *
 * class A: 2 ms
 * class B: 50 ms
 * TAS: 100 us (not implemented yet)
 *
 * @param du: NetChan data unit container
 * @returns: guaranteed delay bound
 */
uint64_t get_class_delay_bound_ns(struct channel *du);

#ifdef __cplusplus
}
#endif
