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
	struct avtpdu_cshdr pdu;

	/* payload */
	uint16_t payload_size;
	unsigned char payload[0];
} __attribute__((__packed__));

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
 * @param stream_id 64 bit unique value for the stream allotted to our channel.
 * @param sz: expexted size of data. Once set, larger datasets cannot be sent via this PDU (allthough smaller is possible)
 *
 * @returns the new PDU or NULL upon failure.
 */
struct timedc_avtp * pdu_create(uint64_t stream_id, uint16_t sz);

/**
 * pdu_destroy : clean up and destroy a pdu
 *
 * @param pdu: indirect pointer to pdu (will update callers ref to NULL as well)
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

struct nethandler;

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
