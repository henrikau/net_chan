#pragma once
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <linux/if_ether.h>

#include <srp/mrp_client.h>

/* --------------------------
 * Main TimedC values
 */
#define NETFIFO_RX(x) struct timedc_avtp * x ## _du = pdu_create_standalone(#x, 0, net_fifo_chans, ARRAY_SIZE(net_fifo_chans))
#define NETFIFO_TX(x) struct timedc_avtp * x ## _du = pdu_create_standalone(#x, 1, net_fifo_chans, ARRAY_SIZE(net_fifo_chans))

#define WRITE(x,v) pdu_send_now(x ## _du, v)
#define READ(x,v) pdu_read(x ## _du, v)

/* Deprecated
 *    - Use NETFIFO_(R|T)X instad
 */
#define NETFIFO_TX_CREATE(x)						\
	nf_tx_create((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans));
#define NETFIFO_RX_CREATE(x)						\
	nf_rx_create((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans));


/* Empty mac multicast (ip multicast should be appended (low order 23
 * bit to low order 23)
 */
#define DEFAULT_MCAST {0x01, 0x00, 0x5E, 0x00, 0x00, 0x00}

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

	/* Size of payload, 0..1500 bytes */
	uint16_t size;

	/* Minimum distance between each frame, in Hz */
	uint8_t freq;

	/* Name of net_fifo (used by macros to create variable)
	 * It expects a callback
	 */
	char name[32];
};

/* Rename experimental type to TimedC subtype for now */
#define AVTP_SUBTYPE_TIMEDC 0x7F

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

struct timedc_avtp;
struct nethandler;

int nf_set_nic(char *nic);
void nf_set_hmap_size(int sz);
void nf_use_srp(void);
void nf_verbose(void);

#define ARRAY_SIZE(x) (x != NULL ? sizeof(x) / sizeof(x[0]) : -1)

/* Get idx of a channel based on the name
 */
int nf_get_chan_idx(char *name, const struct net_fifo *arr, int arr_size);
#define NF_CHAN_IDX(x) (nf_get_chan_idx((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans)))

struct net_fifo * nf_get_chan(char *name, const struct net_fifo *arr, int arr_size);
const struct net_fifo * nf_get_chan_ref(char *name, const struct net_fifo *arr, int arr_size);

int nf_tx_create(char *name, struct net_fifo *arr, int arr_size);
int nf_rx_create(char *name, struct net_fifo *arr, int arr_size);

#define NF_GET(x) (nf_get_chan((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans)))
#define NF_GET_REF(x) (nf_get_chan_ref((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans)))

/**
 * pdu_create - create and initialize a new PDU.
 *
 * The common workflow is that a PDU is created once for a stream and
 * then updated before sending. If the traffic is so dense that the
 * network layer has not finished with it before a new must be
 * assembled, caller should create multiple PDUs.
 *
 * For convenience, the flow is more like:
 * CREATE
 * while (1)
 *    UPDATE -> SEND
 * DESTROY
 *
 * @param nh nethandler container
 * @param dst destination address for this PDU
 * @param stream_id 64 bit unique value for the stream allotted to our channel.
 * @param sz: size of data to transmit
 *
 * @returns the new PDU or NULL upon failure.
 */
struct timedc_avtp * pdu_create(struct nethandler *nh,
				unsigned char *dst,
				uint64_t stream_id,
				uint16_t sz);


/**
 * pdu_create_standalone - create pdu using internal refs as much as possible
 *
 * Intended to be used when retrieving values from struct net_fifo and
 * other magic constants in net_fifo.h
 *
 * @param name : name of net_fifo
 * @param tx : tx or rx end of net_fifo
 * @param arr : net_fifo array of values
 * @param arr_size : size of net_fifo array
 *
 * @returns new pdu, NULL on error
 */
struct timedc_avtp *pdu_create_standalone(char *name,
					bool tx,
					struct net_fifo *arr,
					int arr_size);

/**
 * pdu_get_payload
 *
 * @param pdu AVTP dataunit
 * @returns pointer to payload
 */
void * pdu_get_payload(struct timedc_avtp *pdu);

/**
 * pdu_destroy : clean up and destroy a pdu
 *
 * @param pdu: pointer to pdu
 */
void pdu_destroy(struct timedc_avtp **pdu);

/**
 * pdu_update : take an existing PDU and update timestamp and data and make it ready for Tx
 *
 * The function expects the size of data to be the same size as when it was created
 *
 * @param pdu: pdu to update
 * @param ts: avtp timestamp (lower 32 bit of PTP timestamp)
 * @param data: data to copy into payload (size is fixed from pdu_create())
 *
 * @returns 0 on success, errno on failure.
 */
int pdu_update(struct timedc_avtp *pdu, uint32_t ts, void *data);

/**
 * pdu_send : send payload of TimedC data unit
 *
 * This will extract the AVTP payload from the PDU and send it to the
 * correct destination MAC.
 *
 * @params: pdu
 * @returns: 0 on success negative value on error.
 */
int pdu_send(struct timedc_avtp *pdu);

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
int pdu_send_now(struct timedc_avtp *du, void *data);

/**
 * pdu_read : read data from incoming pipe attached to DU
 *
 * @param du: data container
 * @param data: memory to store received data to
 *
 * @return bytes received or -1 on error
 */
int pdu_read(struct timedc_avtp *du, void *data);

/**
 * nh_init - initialize nethandler
 *
 * @param ifname: NIC to attach to
 * @param hmap_size: sizeof incoming frame hashmap
 *
 * @returns struct nethandler on success, NULL on error
 */
struct nethandler * nh_init(char *ifname, size_t hmap_size);

/**
 * nh_init_standalone - create a standalone instance of nethandler
 *
 * This creates a 'hidden' nethandler instance kept by the library. It
 * is intended to be used alongside the various macros (in particular
 * NETFIFO_(T|R)X_CREATE()) to hide away resource management etc.
 *
 * It will use the values stored in nf_nic (see nf_set_nic) and
 * nf_hmap_size.
 *
 * @params : void
 * @returns: 0 on success, -1 on error
 */
int nh_init_standalone(void);


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
void nf_stop_rx(struct nethandler *nh);

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
 * @param: du: new TimedC Data-unit
 */
int nh_add_tx(struct nethandler *nh, struct timedc_avtp *du);
int nh_add_rx(struct nethandler *nh, struct timedc_avtp *du);

/**
 * nh_destroy: safely destroy nethandler. If _rx is running, it will be stopped.
 *
 * @param nh: indirect ref to nh pointer (caller's ref will be NULL'd)
 */
void nh_destroy(struct nethandler **nh);

/**
 * nh_destroy_standalone: destroy singular nethandler created by nh_init_standalone()
 *
 * @param void
 * @return: void
 */
void nh_destroy_standalone();
