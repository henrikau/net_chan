#pragma once
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
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

struct timedc_avtp
{
	struct avtpdu_cshdr pdu;

	/* channel to use internal. Must be agreed upon beforehand and
	 * configured before starting app
	 */
	uint16_t chan_id;

	/* payload */
	uint16_t payload_size;
	unsigned char payload[0];
} __attribute__((__packed__));

static inline struct timedc_avtp * pdu_create(uint64_t stream_id, uint16_t chan_id, size_t sz)
{
	struct timedc_avtp *pdu = calloc(sizeof(*pdu),1);
	if (!pdu)
		return NULL;
	pdu->pdu.subtype = AVTP_SUBTYPE_TIMEDC;
	pdu->pdu.stream_id = stream_id;
	pdu->chan_id = chan_id;
	pdu->payload_size = sz;
	return pdu;
}

static inline int pdu_update(struct timedc_avtp *pdu, uint32_t ts, void *data, size_t sz)
{
	if (!pdu)
		return -1;
	if (sz > pdu->payload_size)
		return -1;

	pdu->pdu.avtp_timestamp = ts;
	pdu->pdu.tv = 1;
	memcpy(pdu->payload, data, sz);
	return 0;
}

struct nethandler;

struct nethandler * nh_init(const unsigned char *ifname, size_t hmap_size, const unsigned char *dstmac);

/* Register a callback for a given stream_id
 *
 * Whenever a new frame arrives with the registred streamID, the
 * provided callback will be invoked with the captured pdu
 *
 * Note: this is done from the receiver thread, so cb should should be
 * non-blocking and fast.
 *
 */
int nh_reg_callback(struct nethandler *nh,
		uint64_t stream_id,
		void *priv_data,
		int (*cb)(void *priv_data, struct timedc_avtp *pdu));


int nh_feed_pdu(struct nethandler *nh, struct timedc_avtp *pdu);
int nh_start(struct nethandler *nh);

void nh_destroy(struct nethandler **nh);
