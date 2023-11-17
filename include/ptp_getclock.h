/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <time.h>

/**
 * get fd for the PTP device on ifname
 *
 * If ifname does not have a PTP device, it will return -1
 */
int get_ptp_fd(const char *ifname);

/**
 * get_ptp_ts_ns(): read current timestamp from PTP device.
 *
 * Will return the current timestamp, in ns on uint64_t format. If
 * ptp_fd is invalid, 0 is returned.
 */
uint64_t get_ptp_ts_ns(int ptp_fd);

/**
 * tai_to_avtp_ns(): convert at 64bit TAI timestamp (nanosec granularity) to 32 bit
 *
 * This is the AVB presentation_time format which is the lower 32 bits,
 * or roughly 4 seconds of timestamp.
 */
uint32_t tai_to_avtp_ns(uint64_t tai_ns);

/**
 * tai_get_ns(): read local clock and get TAI format in return
 *
 * WARNING: This expects system to be PTP synchronized such that the PTP
 * clock on the NIC and the systemclock (TAI) are reasonably in sync!
 *
 * @param: void:
 * @returns: TAI clock in ns
 */
uint64_t tai_get_ns(void);

#define US_IN_MS   (1000L)
#define NS_IN_US   (1000L)
#define NS_IN_MS   (1000L * NS_IN_US)
#define NS_IN_SEC  (1000L * NS_IN_MS)
#define NS_IN_HOUR (3600L * NS_IN_SEC)

static inline void ts_subtract_ns(struct timespec *ts, uint64_t sub)
{
	if (!ts)
		return;
	if (sub > ts->tv_nsec) {
		ts->tv_sec--;
		ts->tv_nsec = ts->tv_nsec + NS_IN_SEC;
	}
	ts->tv_nsec -= sub;
}

static inline void ts_normalize(struct timespec *ts)
{
	while (ts->tv_nsec >= NS_IN_SEC) {
		ts->tv_nsec -= NS_IN_SEC;
		ts->tv_sec++;
	}
}

static inline void ts_add_ns(struct timespec *ts, uint64_t add)
{
	if (!ts)
		return;
	ts->tv_nsec += add;
	ts_normalize(ts);
}

#ifdef __cplusplus
}
#endif
