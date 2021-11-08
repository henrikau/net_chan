#pragma once
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <linux/if_ether.h>

#define DEFAULT_NIC "eth0"

/* Empty mac multicast (ip multicast should be appended (low order 23
 * bit to low order 23)
 */
#define DEFAULT_MAC {0x01, 0x00, 0x5E, 0x00, 0x00, 0x00}

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
	/* mcast: address of multicast group the talker will publish
	 * to
	 */
	uint8_t mcast[ETH_ALEN];
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

struct timedc_avtp;
struct nethandler;


#define NETFIFO_RX(x) pdu_create_standalone(x, 0, net_fifo_chans, ARRAY_SIZE(net_fifo_chans), (unsigned char *)nf_nic, nf_hmap_size)
#define NETFIFO_TX(x) pdu_create_standalone(x, 1, net_fifo_chans, ARRAY_SIZE(net_fifo_chans), (unsigned char *)nf_nic, nf_hmap_size)

#define ARRAY_SIZE(x) (x != NULL ? sizeof(x) / sizeof(x[0]) : -1)

/* Get idx of a channel based on the name
 */
int nf_get_chan_idx(char *name, const struct net_fifo *arr, int arr_size);
#define NF_CHAN_IDX(x) (nf_get_chan_idx((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans)))

struct net_fifo * nf_get_chan(char *name, const struct net_fifo *arr, int arr_size);
const struct net_fifo * nf_get_chan_ref(char *name, const struct net_fifo *arr, int arr_size);

#define NF_GET(x) (nf_get_chan((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans)))
#define NF_GET_REF(x) (nf_get_chan_ref((x), net_fifo_chans, ARRAY_SIZE(net_fifo_chans)))

#define FREQ_TO_MS(x) (1000 / x)

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
 * @param sz: expexted size of data. Once set, larger datasets cannot be sent via this PDU (allthough smaller is possible)
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
 * @param nic : NIC we are bound to (if nethandler must be created)
 * @param hmap_size : size of hmap in nethandler
 *
 * @returns new pdu, NULL on error
 */
struct timedc_avtp *pdu_create_standalone(char *name,
					bool tx,
					struct net_fifo *arr,
					int arr_size,
					unsigned char *nic,
					int hmap_size);

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
 * @param pdu: pdu to update
 * @param ts: avtp timestamp (lower 32 bit of PTP timestamp)
 * @param data: data to copy into payload, max payload_size from pdu_create)
 * @param sz: size of payload to copy
 *
 * @returns 0 on success, errno on failure.
 */
int pdu_update(struct timedc_avtp *pdu, uint32_t ts, void *data, size_t sz);

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
 * nh_init - initialize nethandler
 *
 * @param ifname: NIC to attach to
 * @param hmap_size: sizeof incoming frame hashmap
 *
 * @returns struct nethandler on success, NULL on error
 */
struct nethandler * nh_init(unsigned char *ifname, size_t hmap_size);

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
		int (*cb)(void *priv_data, struct timedc_avtp *pdu));


/**
 * nh_feed_pdu - feed a pdu to nethandler which will be passed to relevant callback
 *
 * note: current implementation only supports one callback per StreamID,
 * if multiple callbacks are required for a streamid, then the callback
 * registred with nethandler must implement the multiplexing.
 *
 * @param nh: nethandler container
 * @param pdu: received pdu
 *
 * @returns
 */
int nh_feed_pdu(struct nethandler *nh, struct timedc_avtp *pdu);

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

/**
 * nh_destroy: safely destroy nethandler. If _rx is running, it will be stopped.
 *
 * @param nh: indirect ref to nh pointer (caller's ref will be NULL'd)
 */
void nh_destroy(struct nethandler **nh);
