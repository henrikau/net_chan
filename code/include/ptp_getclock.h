/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#pragma once
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

#define US_IN_MS (1000)
#define NS_IN_MS (1000 * US_IN_MS)
#define NS_IN_SEC (1000 * NS_IN_MS)

static inline void ts_normalize(struct timespec *ts)
{
	while (ts->tv_nsec >= NS_IN_SEC) {
		ts->tv_nsec -= NS_IN_SEC;
		ts->tv_sec++;
	}
}
