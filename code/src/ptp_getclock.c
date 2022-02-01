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
#define MS_IN_SEC 1000
#define US_IN_SEC (1000 * MS_IN_SEC)
#define NS_IN_SEC (1000 * US_IN_SEC)

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

uint32_t tai_to_avtp_ns(uint64_t tai_ns)
{
	return (uint32_t)(tai_ns & 0xffffffff);
}

uint64_t get_ptp_ts_ns(int ptp_fd)
{
	if (ptp_fd < 0)
		return 0;

	struct timespec ts;
	if (clock_gettime(get_clockid(ptp_fd), &ts) == -1) {
		fprintf(stderr, "%s(): Failed reading PTP clock (%d, %s)\n",
			__func__, errno, strerror(errno));
		return 0;
	}

	return ts.tv_sec * NS_IN_SEC + ts.tv_nsec;
}

int get_ptp_fd(const char *ifname)
{
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
	printf("%s(): ptp_path: %s\n", __func__, ptp_path);

	int ptp_fd = open(ptp_path, O_RDONLY);
	if (ptp_fd < 0) {
		perror("Failed opening PTP fd, perhaps try with sudo?");
		return -1;
	}

	return ptp_fd;
}
