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
#include <pthread.h>

#include <ptp_getclock.h>

#include <linux/if_packet.h>	/* sk_addr */
#include <linux/net_tstamp.h>	/* sock_txtime */
#include <stdarg.h>


enum stream_class {
	SC_TAS = 100 * NS_IN_US,
	SC_CLASS_A = 2 * NS_IN_MS,
	SC_CLASS_B = 50 * NS_IN_MS
};

enum nc_loglevel {
	NC_DEBUG=0,
	NC_INFO,
	NC_WARN,
	NC_ERROR
};

#define DEBUG(ch, ...) nh_debug((ch), NC_DEBUG, __VA_ARGS__)
#define INFO(ch, ...)  nh_debug((ch), NC_INFO, __VA_ARGS__)
#define WARN(ch, ...)  nh_debug((ch), NC_WARN, __VA_ARGS__)
#define ERROR(ch, ...) nh_debug((ch), NC_ERROR, __VA_ARGS__)


/* StreamID u64 to bytearray wrapper */
union stream_id_wrapper {
	uint64_t s64;
	uint8_t s8[8];
};

#define DEFAULT_TX_TAS_SOCKET_PRIO 3
#define DEFAULT_TX_CBS_SOCKET_PRIO 2

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
 * channel send operations
 *
 * 
 *
 */
struct channel;
struct chan_send_ops {
	/**
	 * send_at : send current payload of netchan data at specified timestamp (if applicable)
	 *
	 * This will extract the AVTP payload from the PDU managed by channel and send it to the
	 * correct destination MAC.
	 *
	 * If the timestamp is sufficiently into the future, this function /may/ block.
	 *
	 * @params: ch
	 * @params: *tx_ns: timestamp for when the frame was physically sent (requires HW support)
	 * @returns: 0 on success negative value on error.
	 */
	int (*send_at)(struct channel *ch, uint64_t *tx_ns);
	int (*send_at_wait)(struct channel *ch, uint64_t *tx_ns);

	/**
	 * send_now: update and send data *now*
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
	int (*send_now)(struct channel *ch, void *data);

	/**
	 * send_now_wait - update and send PDU from channel, and wait for class delay
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
	int (*send_now_wait)(struct channel *ch, void *data);
};

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

	/* Guardian mutex of internal state.
	 */
	pthread_mutex_t guard;

	/*
	 * payload (avtpdu_cshdr) uses htobe64 encoded streamid, we need
	 * it as a byte array for mrp, so keep an easy ref to it here
	 */
	union stream_id_wrapper sidw;

	/*
	 * Only relevant in SRP mode!
	 *
	 * A Tx channel will keep track of remote listeners. Once this
	 * reaches 0, the channel will stop transmitting and ready will
	 * be cleared. This is done by the SRP monitor thread managed by
	 * nethandler.
	 */
	int remote_listener;
	enum stream_class sc;

	/* Active priority (PCP) for this channel/stream class
	 * throughout the network.
	 *
	 * *not* to be confused with tx_sock_prio (which is used to select the correct Qdisc)
	 */
	int pcp_prio;

	/* Flags indicating if channel is ready and/or in the process of
	 * being stopped.
	 *
	 * Especially when using SRP, a channel can be !ready and also
	 * be in the process of being stopped
	 *
	 * A stopped (tx) channel is still a tx-channel, but it should
	 * not be announced to the network while it is !ready
	 */
	bool ready;
	bool stopping;

	/* Only relevant in SRP mode!
	 *
	 * Both Rx and Tx channels must wait until at least one remote
	 * is attached to the stream. This is monitored and managed by
	 * the SRP monitor thread managed by nethandler.
	 */
	pthread_mutex_t ready_mtx;
	pthread_cond_t ready_cond;

	/*
	 * Each outgoing stream has its own socket, with corresponding
	 * SRP attributes etc.
	 *
	 * Prio is the priority used to steer the traffic to the correct
	 * mqprio queue.
	 */
	int tx_sock;
	int tx_sock_prio;
	bool use_so_txtime;

	/* Send-ops, depending on the selected Tx stream class (TAS or CBS)
	 */
	struct chan_send_ops *ops;

	/* private area for callback, embed directly i not struct to
	 * ease memory management. */
	struct cb_priv *cbp;
	int fd_w;
	int fd_r;

	/* payload size */
	uint16_t payload_size;
	uint16_t full_size;

	/*
	 * To enforce the channel frequency, keep track of next time
	 * this channel is eligble to transmit.
	 *
	 * This behavior is identical for both TAS and CBS
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

	/* time of current sample
	 *
	 * This is used to derinve avtp_timestamp and will signal eitehr
	 * the capture time or the intended presentation time of the
	 * data.
	 */
	uint64_t sample_ns;


	/* The currently active packet entering/leaving is tacked onto
	 * the end, payload is of size '->payload_size'
	 */
	struct avtpdu_cshdr pdu;
	unsigned char payload[0];
};

/* netchan_srp_client needs ref to stream_id_wrapper and channel, so
 * incluide after these structs.
 */
#include <netchan_srp_client.h>

struct nethandler {
	struct channel *du_tx_head;
	struct channel *du_tx_tail;

	struct channel *du_rx_head;
	struct channel *du_rx_tail;

	bool verbose;
	bool use_srp;

	/* Priority used when creating a new socket, either CBS or TAS.
	 * We currently only allow 2 mqprio Qdiscs to handle this,
	 * either TAS or CBS. Class A and B are thus multiplexed onto
	 * the same socket!
	 *
	 * In the future where we have NICs with *3* HW queues for TAS,
	 * A, and B, this may change.
	 */
	int tx_tas_sock_prio;
	int tx_cbs_sock_prio;

	/* We have one Rx socket, but each datastream will have its own
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
	 * A nethandler handles the SRP connection
	 *
	 * All new Tx channels will send a talker announce message and wait for at least one
	 *
	 * Similarly, each incoming stream has to keep track of which
	 * talker to subscribe to, so keep context for this here..
	 *
	 * MRP client has its own section for talker and listener, with
	 * dedicated fields for strem_id, mac etc.
	 */
	struct srp *srp;
	int socket_prio;	/* Active priority (PCP) for this channel */



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

	/*
	 * Tag tracebuffer, useful to tag trace when frames have arrived
	 * etc to pinpoint delays etc.
	 */
	int ftrace_break_us;
	FILE *tb;

	/*
	 * datalogger, used by both Tx and Rx
	 */
	struct logc *logger;

	/* hashmap, chan_id -> cb_entity  */
	size_t hmap_sz;
	struct cb_entity *hmap;
};

/* socket helpers
 */
int nc_create_rx_sock(const char *ifname);
bool nc_create_tas_tx_sock(struct channel *ch);
bool nc_create_cbs_tx_sock(struct channel *ch);
int nc_handle_sock_err(int sock, int ptp_fd);

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
 * WARNING: when SRP is enabled, then channel will not be ready (and
 * usable) until the other end has connected. This function will return
 * immediately, but the caller must ensure that the channel is ready using either:
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
 * WARNING: when SRP is enabled, then channel will not be ready (and
 * usable) until the other end has connected. This function will return
 * immediately, but the caller must ensure that the channel is ready using either:
 * - chan_ready()
 * - chan_ready_timedwait()
 *
 * @param nh nethandler container
 * @param attrs netfiro channel attributes
 *
 * @returns new channel or NULL on error
 */
struct channel *chan_create_rx(struct nethandler *nh, struct channel_attrs *attrs);

/**
 * chan_ready(): test to see if the channel is ready for use
 *
 * This is mostly relevant when using SRP as chan_create_(r|t)x() will
 * block awaiting for the other end of the channel. A common approach
 * will be to create the channel asynchronously and wait for all
 * channels to become ready.
 *
 * As long as a channel is not ready, neither chan_send() nor
 * chan_read() will succeed.
 *
 * @param channel container
 * @return true if channel ready, false otherwise
 */
bool chan_ready(struct channel *ch);

/**
 * chan_ready_timedwait() wait for channel to become ready with a timeout
 *
 * If the channel is not ready within a set timeout, -EINTR is
 * returned. Once the channel is marked ready, the function will return
 * immediately with 0 and chan_ready() will evaluate to true.
 *
 * @params channel container
 * @params timeout_ns timeout before -ETIMEDOUT (-110) is returned
 * @return 0 on successful wait, negative on error
 */
int chan_ready_timedwait(struct channel *ch, uint64_t timeout_ns);

/**
 * chan_stop() stop channel, but do not destroy it.
 *
 * @param channel container
 * @return true if channel was successfully stopped
 */
bool chan_stop(struct channel *ch);

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
 * @param ts: capture/presentation timestamp
 * @param data: data to copy into payload (size is fixed from chan_create())
 *
 * @returns 0 on success, errno on failure.
 */
int chan_update(struct channel *ch, uint64_t ts, void *data);

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
 * For CBS, a better approach is periodic timer (pt_init / pt_next_cycle)
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
 * chan_dleay() delay a channel for the specified number of ns
 *
 * FIXME: we are slowly moving towards a situation where we expect
 *	  phc2sys to synchronize the system clock, so this function
 *	  should be ripe for simplification.
 *
 * @param ptp_target_delay_ns: absolute timestamp for PTP time to delay to
 * @param du: data-unit for netchan internals (need access to PTP fd)
 *
 * @returns: the delay error (in ns)
 */
int64_t chan_delay(struct channel *du, uint64_t ptp_target_delay_ns);

/**
 * Simpel wrappers to channel-ops, soon to be @deprecated
 *
 * NOTE!! chan_send_now() and chan_send_now_wait() will call chan_update() first
 */
int chan_send(struct channel *ch, uint64_t *tx_ns);
int chan_send_now(struct channel *ch, void *data);
int chan_send_now_wait(struct channel *ch, void *data);

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
 * chan_read : read data from incoming channel.
 *
 * @param ch: channel
 * @param data: memory to store received data to
 *
 * @return bytes received or -EINVAL on error
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
 * nh_set_verbose() - set or disable verbose logging
 *
 * @param: nh nethandler container
 * @param: verbose bool flag
 */
void nh_set_verbose(struct nethandler *nh, bool verbose);

/**
 * nh_set_srp() - set or disable SRP
 *
 * @param: nh nethandler container
 * @param: srp bool flag
 */
void nh_set_srp(struct nethandler *nh, bool use_srp);

/**
 * nh_set_trace_breakval() - set breakvalue for tracebuffer.
 *
 * This will also enable logging to the kernel tracebuffer, and when a
 * break-value has been encountered, stop tracing and abort, allowing
 * the user to extract the tracebuffer to determine the cause of the
 * excessive delay.
 *
 * If break_us <= 0, tracebuffer and break is disabled.
 * If break_us > 1e6, tracebuffer is also disabled since a tracebuffer seldom holds several seconds worth of tracing data.
 *
 * @param: nh nethandler container
 * @param: break_us: int, if delay is longer than break_us, stop tracing.
 * flag
 */
void nh_set_trace_breakval(struct nethandler *nh, int break_us);

/**
 * nh_enable_ftrace() - expose ftrace control
 *
 * @params: nh nethandler container
 */
void nh_enable_ftrace(struct nethandler *nh);

/**
 * nh_set_tx_prio() - set Tx socket prio
 *
 * Default prio: 3
 *
 * Note: this is the *socket* priority, i.e. the priority used to select
 * the correct mqprio socket, *not* the SRP priority for a given stream
 * class.
 *
 * The prio will be used for all sockets and must be called *before* any
 * Tx sockets are created. Once there are active Tx-sockets, it will not
 * be possible to change the priority.
 *
 * @param: nh nethandler container
 * @param: stream_class sc the target streamclass
 * @param: tx_prio int socket priority
 */
bool nh_set_tx_prio(struct nethandler *nh, enum stream_class sc, int tx_prio);

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
 * nh_notify_talker_Lnew() Notify a talker that a new Listener has arrived
 *
 * @params: nethandler container
 * @params: stream announced stream_id
 * @params: state reported state
 *
 * @return: true if stream was found and notified successfully
 */
bool nh_notify_talker_Lnew(struct nethandler *nh, union stream_id_wrapper stream, int state);

/**
 * nh_notify_talker_Lleaving() Notify a talker that a Listener is leaving
 *
 * @params: nethandler container
 * @params: stream announced stream_id
 * @params: state reported state
 *
 * @return: true if stream was found and notified successfully
 */
bool nh_notify_talker_Lleaving(struct nethandler *nh, union stream_id_wrapper stream, int state);

/**
 * nh_notify_listener_Tnew() notify listener that a new talker is available
 *
 * @params: nethandler container
 * @params: stream announce stream_id
 * @params: mac_addr multicast address for stream
 *
 * @returns: true if stream was successfully notified
 */
bool nh_notify_listener_Tnew(struct nethandler *nh, union stream_id_wrapper stream, uint8_t *mac_addr);

/**
 * nh_notify_listener_Tleave() notify listener that the talker has left
 *
 * @params: nethandler container
 * @params: stream announce stream_id
 * @params: mac_addr multicast address for stream
 *
 * @returns: true if stream was successfully notified
 */
bool nh_notify_listener_Tleave(struct nethandler *nh, union stream_id_wrapper stream, uint8_t *mac_addr);

/**
 * nh_stop() Stop all channels managed by nethandlerj
 *
 * This function does not tear down the system, but marks all channels
 * as stopping and not ready. If SRP is enabled, this will also
 * unannouce all streams.
 *
 * After this has been called, calls to a channels send() and read() will fail.
 *
 * @nh: nethandler container
 */
void nh_stop(struct nethandler *nh);

/**
 * nh_rotate_logs() rotate logs to save current logset and start capturing new
 *
 * The logger has an upper limit to how much data it can
 * capture. Currently this is set to 1.080.000 entries (6 hours of a
 * single 50Hz channel). With more channels and/or higher rate, the
 * timespan is affected accordingly.
 *
 * To accomodate longer trace-periods, we can rotate the logs and
 * instead create multiple logfiles. These are named incrementally
 * (-0.csv, -1.csv etc)
 *
 * WARNING: when calling nh_rotate_logs(), the generated I/O activity
 * can affect the real-time performance of the running system.
 *
 * @params nh nethandler container
 */
void nh_rotate_logs(struct nethandler *nh);


/**
 * nh_destroy: safely destroy nethandler. If _rx is running, it will be stopped.
 *
 * @param nh: indirect ref to nh pointer (caller's ref will be NULL'd)
 */
void nh_destroy(struct nethandler **nh);

void nh_list_active_channels(struct nethandler *nh);

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

/**
 * nh_debug() Debug print routine
 *
 * This routine shold be the preferred way of printing information to
 * the console. It will automatically add a timestamp and streamID (if
 * available) to the result, making it easier to connect log-entries
 * accross a distributed system.
 *
 * @param ch channel container
 * @param loglevel debug loglevel
 * @param fmt string format
 *
 * @returns number of bytes written, -1 on error
 */
int nh_debug(struct channel *ch, enum nc_loglevel loglevel, const char *fmt, ...);


#ifdef __cplusplus
}
#endif
