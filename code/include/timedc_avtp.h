#pragma once
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#define DEFAULT_NIC "eth0"

/* Empty mac multicast (ip multicast should be appended (low order 23
 * bit to low order 23)
 */
#define DEFAULT_MAC {0x01, 0x00, 0x5E, 0x00, 0x00, 0x00}

struct timedc_avtp;
struct nethandler;

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
 * @param dstmac: remote end of incoming frames.
 *
 * @returns struct nethandler on success, NULL on error
 */
struct nethandler * nh_init(const unsigned char *ifname, size_t hmap_size, const unsigned char *dstmac);

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
