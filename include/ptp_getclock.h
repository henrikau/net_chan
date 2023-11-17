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
#include <netchan_utils.h>

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

#ifdef __cplusplus
}
#endif
