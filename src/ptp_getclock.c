/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <ptp_getclock.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define PTP_MAX_DEV_PATH 16

/*
 * Get clockid, take get_clockid() from
 * https://elixir.bootlin.com/linux/v5.13-rc2/source/tools/testing/selftests/ptp/testptp.c
 */
static clockid_t get_clockid(int fd)
{
#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)	((~(clockid_t) (fd) << 3) | CLOCKFD)

	return FD_TO_CLOCKID(fd);
}

uint64_t get_ptp_ts_ns(int ptp_fd)
{
	if (ptp_fd < 0)
		return 0;

	struct timespec ts;
	if (clock_gettime(get_clockid(ptp_fd), &ts) == -1) {
		/* fprintf(stderr, "%s(): Failed reading PTP clock (%d, %s)\n", */
		/* 	__func__, errno, strerror(errno)); */
		return 0;
	}

	return ts.tv_sec * NS_IN_SEC + ts.tv_nsec;
}

int get_ptp_fd(const char *ifname)
{
	/*
	 * It does not make sense to use lo as a PTP device, it can by
	 * definition not get any external sync, nor sync others. In
	 * this case, it is better to fail early and either force the
	 * caller to use CLOCK_REALTIME or just abort.
	 */
	if (strncmp(ifname, "lo", 2) == 0)
		return -1;

	struct ifreq req;
	struct ethtool_ts_info interface_info = {0};

	interface_info.cmd = ETHTOOL_GET_TS_INFO;
	snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", ifname);
	req.ifr_data = (char *) &interface_info;

	int ioctl_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (ioctl_fd < 0) {
		fprintf(stderr, "%s(): Failed opening fd for ioctl (%d, %s)\n",
			__func__, errno, strerror(errno));
		return -1;
	}

	if (ioctl(ioctl_fd, SIOCETHTOOL, &req) < 0) {
		fprintf(stderr, "%s(): ioctl failed (%d, %s)\n",
			__func__, errno, strerror(errno));
		return -1;
	}

	if (interface_info.phc_index < 0) {
		fprintf(stderr, "%s(): No suitable PTP device found for nic %s (%d, %s)\n",
			__func__, ifname, errno, strerror(errno));
		return -1;
	}
	close(ioctl_fd);

	char ptp_path[PTP_MAX_DEV_PATH] = {0};
	snprintf(ptp_path, sizeof(ptp_path), "%s%d", "/dev/ptp",
		interface_info.phc_index);

	int ptp_fd = open(ptp_path, O_RDONLY);
	if (ptp_fd < 0) {
		perror("Failed opening PTP fd, perhaps try with sudo?");
		return -1;
	}

	return ptp_fd;
}

uint32_t tai_to_avtp_ns(uint64_t tai_ns)
{
	return (uint32_t)(tai_ns & 0xffffffff);
}

uint64_t tai_get_ns(void)
{
	struct timespec ts_tai;
	clock_gettime(CLOCK_TAI, &ts_tai);
	return ts_tai.tv_sec * NS_IN_SEC + ts_tai.tv_nsec;
}

uint64_t real_get_ns(void)
{
	struct timespec ts_tai;
	clock_gettime(CLOCK_REALTIME, &ts_tai);
	return ts_tai.tv_sec * NS_IN_SEC + ts_tai.tv_nsec;
}

