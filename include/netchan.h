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
#include <linux/if.h>		/* IFNAMSIZ */
#include <stdio.h>		/* FILE* */


#include <ptp_getclock.h>

#include <linux/if_packet.h>	/* sk_addr */
#include <linux/net_tstamp.h>	/* sock_txtime */


/* --------------------------
 * Main NetChan Macros
 *
 * These macros rely heavily upon a sort-of singleton approach. If you
 * are concerned with interference from other threads, then avoid the
 * macros and use the non-standalone functions.
 */
#define NETCHAN_RX(x) struct channel * x ## _du = \
		chan_create_standalone(#x, 0, nc_channels, \
		ARRAY_SIZE(nc_channels))
#define NETCHAN_TX(x) struct channel * x ## _du = \
		chan_create_standalone(#x, 1, nc_channels, \
		ARRAY_SIZE(nc_channels))

#define WRITE(x,v) chan_send_now(x ## _du, v)
#define WRITE_WAIT(x,v) chan_send_now_wait(x ## _du, v)
#define READ(x,v) chan_read(x ## _du, v)
#define READ_WAIT(x,v) chan_read_wait(x ## _du, v)

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
	CLASS_A = 2 * NS_IN_MS,
	CLASS_B = 50 * NS_IN_MS
};

/* StreamID u64 to bytearray wrapper */
union stream_id_wrapper {
	uint64_t s64;
	uint8_t s8[8];
};

/**
 * @struct channel_attrs
 *
 * @brief This struct is a conventient container for a channels relevant
 * attributes such as destination address, stream ID, size etc
 *
 * @var channel_attrs:dst

 * Member dst is the destination address for this channel. For a talker,
 * this is where the data should be sent to, for a listener, this is
 * either its own address or a multicast group it should subscribe to
 */
struct channel_attrs
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

	/* Name of channel (used by macros to create variable)
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

	struct sockaddr_ll sk_addr;
	uint8_t dst[ETH_ALEN];

	/*
	 * payload (avtpdu_cshdr) uses htobe64 encoded streamid, we need
	 * it as a byte array for mrp, so keep an easy ref to it here
	 */
	union stream_id_wrapper sidw;

	/*
	 * Each channel is mapped to either an Rx or or it has its own Tx socket
	 *
	 * Similarly, each incoming stream has to keep track of which
	 * talker to subscribe to, so keep context for this here..
	 *
	 * MRP client has its own section for talker and listener, with
	 * dedicated fields for strem_id, mac etc.
	 */
	struct mrp_ctx *ctx;
	struct mrp_domain_attr *class_a; /* PCP prio A attributes */
	struct mrp_domain_attr *class_b; /* PCP prio B attributes */
	enum stream_class sc;
	int socket_prio;	/* Active priority (PCP) for this channel */

	/*
	 * Each outgoing stream has its own socket, with corresponding
	 * SRP attributes etc.
	 *
	 * Prio is the priority used to steer the traffic to the correct
	 * mqprio queue.
	 */
	int tx_sock;
	int tx_sock_prio;


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
	struct sock_txtime txtime;

	/* The currently active packet entering/leaving is tacked onto
	 * the end, payload is of size '->payload_size'
	 */
	struct avtpdu_cshdr pdu;
	unsigned char payload[0];
};

struct nethandler {
	struct channel *du_tx_head;
	struct channel *du_tx_tail;

	struct channel *du_rx_head;
	struct channel *du_rx_tail;

	/*
	 * We have one Rx socket, but each datastream will have its own
	 * SRP context, look to netchan_avtp for SRP related fields.
	 */
	int rx_sock;
	unsigned int link_speed;

	const char ifname[IFNAMSIZ];
	bool is_lo;
	int ifidx;
	char mac[6];
	bool running;
	pthread_t tid;

	/*
	 * fd for PTP device to retrieve timestamp.
	 *
	 * We expect ptp daemon to run and sync the clock on the NIC
	 * with the network. We do not assume that the network time is
	 * synched to CLOCK_REALTIME.
	 *
	 * To avoid opening and closing the device multiple times, keep
	 * a ref here.
	 */
	int ptp_fd;

	/* reference to cpu_dma_latency, once opened and set to 0,
	 * computer /should/ refrain from entering high cstates
	 *
	 * See: https://access.redhat.com/articles/65410
	 */
	int dma_lat_fd;

	/* terminal TTY, used if we want to tag event to the output
	 * serial port (for attaching a scope or some external timing
	 * measure).
	 */
	int ttys;
	/*
	 * Tag tracebuffer, useful to tag trace when frames have arrived
	 * etc to pinpoint delays etc.
	 */
	FILE *tb;

	/*
	 * datalogger, used by both Tx and Rx
	 */
	struct logc *logger;

	/* hashmap, chan_id -> cb_entity  */
	size_t hmap_sz;
	struct cb_entity *hmap;
};

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
void nc_tx_sock_prio(int prio);

/* socket helpers
 */
int nc_create_rx_sock(const char *ifname);
int nc_create_tx_sock(struct channel *ch);
int nc_handle_sock_err(int sock);

#define ARRAY_SIZE(x) (x != NULL ? sizeof(x) / sizeof(x[0]) : -1)

/* Get idx of a channel based on the name
 */
int nc_get_chan_idx(char *name, const struct channel_attrs *attrs, int arr_size);
#define NC_CHAN_IDX(x) (nc_get_chan_idx((x), nc_channels, ARRAY_SIZE(nc_channels)))

struct channel_attrs * nc_get_chan(char *name, const struct channel_attrs *attrs, int arr_size);
const struct channel_attrs * nc_get_chan_ref(char *name, const struct channel_attrs *nc_channels, int arr_size);

/**
 * @deprecated
 */
int nc_rx_create(char *name, struct channel_attrs *attrs, int arr_size);

#define NC_GET(x) (nc_get_chan((x), nc_channels, ARRAY_SIZE(nc_channels)))
#define NC_GET_REF(x) (nc_get_chan_ref((x), nc_channels, ARRAY_SIZE(nc_channels)))

/**
 * chan_create_tx create a Tx channel
 *
 * Expects a valid attribute list to be supplied containing
 * valid stream_id, stream_class, payload size and minimum interval
 * between frames.
 *
 * For convenience, the flow is more like:
 * CREATE
 * while (1)
 *    UPDATE -> SEND
 * DESTROY
 *
 * @param nh nethandler container
 * @param attrs channel attributes
 *
 * @returns new channel or NULL on error
 */
struct channel *chan_create_tx(struct nethandler *nh, struct channel_attrs *attrs);

/**
 * chan_create_rx create a Tx channel
 *
 * Expects a valid attribute list to be supplied containing
 * valid stream_id, stream_class, payload size and minimum interval
 * between frames.
 *
 * For convenience, the flow is more like:
 * CREATE
 * while (1)
 *    READ -> CONSUME
 * DESTROY
 *
 * @param nh nethandler container
 * @param attrs netfiro channel attributes
 *
 * @returns new channel or NULL on error
 */
struct channel *chan_create_rx(struct nethandler *nh, struct channel_attrs *attrs);

/**
 * chan_create_standalone - create a new channel using internal refs as much as possible
 *
 * This is a multiplexing function primarily inteded to be used with the
 * macros. In time, this function will be deprecated, use
 * nh_create_init() and chan_create_tx() and chan_create_rx() instead.
 *
 * @param name : name of channel in attribute list
 * @param tx : flag indicating if channel is tx or rx
 * @param arr : channel_attrs array of values
 * @param arr_size : size of channel_attrs array
 *
 * @returns new channel, NULL on error
 */
struct channel *chan_create_standalone(char *name,
				bool tx,
				struct channel_attrs *attrs,
				int arr_size);


/**
 * chan_destroy : clean up and destroy a channel
 *
 * @param ch: pointer to channel
 */
void chan_destroy(struct channel **ch);

/**
 * chan_valid() basic test to determine if channel is valid
 *
 * The function performs the most basic of tests, not aiming to provide
 * a comprehensive test, but rather a fairly thorough first screening.
 *
 * @param: ch
 * @returns true for valid channels
 */
bool chan_valid(struct channel *ch);

/**
 * chan_update : take an existing channel and update timestamp and data and make it ready for Tx
 *
 * The function expects the size of data to be the same size as when it was created
 *
 * @param ch: channel to update
 * @param ts: avtp timestamp (lower 32 bit of PTP timestamp)
 * @param data: data to copy into payload (size is fixed from chan_create())
 *
 * @returns 0 on success, errno on failure.
 */
int chan_update(struct channel *ch, uint32_t ts, void *data);

/**
 * chan_dump_state: Dump internal state about channel and nethandler
 *
 * This is primarily a part of the debugging toolset and is not required
 * for normal use.
 *
 * @param ch: channel
 * @returns: void
 */
void chan_dump_state(struct channel *ch);

/**
 * wait_for_tx_slot(): Sleep until Tx is ready
 *
 * To help align clocks, do a clock_nanosleep() until the next tx-slot has arrived
 *
 * Note: The ETC Qdisc scheduler prohibits tx-time too close to actual
 * time, so the client must be woken up a bit prior to this. This
 * function will therefore wait for approx 100us before the next period
 * slot. If the client runs with periods < 100us or a tighter deadline
 * is needed, the client should handle its own blocking.
 *
 * @param ch: channel
 * @returns : 0 on success, negative on error
 */
int wait_for_tx_slot(struct channel *ch);

/**
 * chan_get_payload : Return a pointer to the most recent payload in the channel
 *
 * @param pdu AVTP dataunit
 * @returns pointer to payload
 */
void * chan_get_payload(struct channel *);


/**
 * chan_send : send current payload of netchan data unit
 *
 * This will extract the AVTP payload from the PDU managed by channel and send it to the
 * correct destination MAC.
 *
 * @params: ch
 * @params: *tx_ns: timestamp for when the frame was physically sent (requires HW support)
 * @returns: 0 on success negative value on error.
 */
int chan_send(struct channel *ch, uint64_t *tx_ns);

/**
 * chan_send_now: update and send data *now*
 *
 * Provided the Tx budget is not exhausted, the data will be sent
 * immideately. Note that based on the expected interval (i.e. CBS
 * bandwidth), the function will block until the budget is available.
 *
 * Use the function chan_time_to_tx() to determine elibility to transmit.
 *
 * @param chan: channel to send from
 * @param data: new data to copy into field and send.
 *
 * @return 0 on success, negative on error
 */
int chan_send_now(struct channel *ch, void *data);

/**
 * chan_time_to_tx : ns left until a new frame can be sent.
 *
 * If the channel is invalid, and extreme value (UINT64_MAX) is returned
 * to indicate that the channel will only be ready in a *very* long
 * time.
 *
 * @params ch: active channel
 * @returns ns left until budget replenished or 0 if ready for Tx
 */
uint64_t chan_time_to_tx(struct channel *ch);

/**
 * chan_send_now_wait - update and send PDU from channel, and wait for class delay
 *
 * When using this function, caller will be blocked for 2ms or 50ms
 * depending on stream class before continuing. This gives a synchronized wait
 * approach to the semantics.
 *
 * @param chan: active channel
 * @param data: new data to copy into field and send.
 *
 * @return 0 on success, negative on error
 */
int chan_send_now_wait(struct channel *ch, void *data);

/**
 * chan_read : read data from incoming channel.
 *
 * @param ch: channel
 * @param data: memory to store received data to
 *
 * @return bytes received or -1 on error
 */
int chan_read(struct channel *ch, void *data);

/**
 * chan_read_wait : read data from incoming channel and wait for class delay before returning.
 *
 * When using this function, caller will be delayed until capture time +
 * class delay has passed. This expects the clocks for both writer and
 * reader to be synchronized properly.
 *
 * @param ch: channel
 * @param data: memory to store received data to
 *
 * @return bytes received or -1 on error
 */
int chan_read_wait(struct channel *ch, void *data);

/**
 * nh_create_init - create and initialize nethandler
 *
 * @param ifname: NIC to attach to
 * @param hmap_size: sizeof incoming frame hashmap
 *
 * @returns struct nethandler on success, NULL on error
 */
struct nethandler * nh_create_init(const char *ifname, size_t hmap_size, const char *logfile);

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
 * nh_remove_(tx|rx) - remove channel
 *
 * This function will only remve the erference to a channel from the
 * nethandler, it will not /destroy/ the channel!
 *
 * @param ch: channel to remove
 * @return: 0 on sucess, negative on error
 */
int nh_remove_tx(struct channel *ch);
int nh_remove_rx(struct channel *ch);

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
