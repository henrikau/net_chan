/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#pragma once
#include <netchan.h>
#include <inttypes.h>

/**
 * timedC logger
 *
 * Format:
 * <common>
 * stream_id: 64 bit value, unique stream ID
 * sz: payload size (uint16_t, in bytes/octets)
 * seqnr: uint8_t avtp seqnr
 * avtp_ns: 32 bit truncated PTP time for data capture
 *
 * <talker>
 * cap_ptp_ns : 64 bit PTP time of data capture (what avtp_ns is constructed from)
 * send_ptp_ns: 64 bit PTP time for when packet has been handed off to sendto()
 *
 * <listener>
 * rx_ns: 64bit timestamp when packet received (preferrably with HW ts)
 * recv_ptp_ns: 64 bit PTP timestamp when frame has entered application via recvmsg()
 */
struct logc;

struct logc * log_create(const char *logfile, bool log_delay);
void log_close_fp(struct logc *logc);

/**
 * log_rx: log Rx entries to a CSV log (if enabled)
 *
 * This will add Rx-entries to a csv-log. It will log
 * - stream_id
 * - size of payload
 * - seqnr
 * - presentation_time (avtp_time)
 * - Timestamp (PTP) for data capture (probably base for avtp time)
 * - Timestamp when package has been passed on to network layer (should be captured close to send())
 *
 * @param logc: log container
 * @param du: data frame to send
 * @param cap_ts_ns: timestamp from network layer upon recv of msg
 * @param send_ptp_ns: timestamp from NIC upon return from recvmsg()
 */
void log_tx(struct logc *logc,
	struct avtpdu_cshdr *du,
	uint64_t cap_ts_ns,
	uint64_t send_ptp_ns);

/**
 * log_rx: log Rx entries to a CSV log (if enabled)
 *
 * This will add Rx-entries to a csv-log. It will log
 * - stream_id
 * - size of payload
 * - seqnr
 * - presentation_time (avtp_time)
 * - Rx timestamp from network subsystem (SO_TIMESTAMPNS)
 * - Timestamp retrieved from the PTP clock on the active NIC upon return from recvmsg()
 *
 * @param logc: log container
 * @param du: received data packet
 * @param rx_ns: timestamp from network layer upon recv of msg
 * @param recv_ptp_ns: timestamp from NIC upon return from recvmsg()
 */
void log_rx(struct logc *logc,
	struct avtpdu_cshdr *du,
	uint64_t rx_ns,
	uint64_t recv_ptp_ns);

/**
 * log_delay: log timestamps for delay (clock_nanosleep)
 *
 * clock_nanosleep() uses CLOCK_MONOTONIC, so we log both ptp target and
 * CPU target. We assume that the CPU clock runs at close enough rate so
 * that we don't have to convert back to PTP timestamp. We want to avoid
 * this as reading the PTP clock over a PCIe link have latency
 * variations from 2-20 us depending on the PCIe link load.
 *
 * @param: logc: log container
 * @param: ptp_target_ns: target wakeup time
 * @param: cpu_target_ns: target wakeup time converted to CLOCK_MONOTONIC
 * @param: cpu_actual_ns: cpu timestamp (MONOTONIC) for when thread actually woke up.
 */
void log_delay(struct logc *logc,
	uint64_t ptp_target_ns,
	uint64_t cpu_target_ns,
	uint64_t cpu_actual_ns);
