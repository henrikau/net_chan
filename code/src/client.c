#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <argp.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/ether.h>
#include <common.h>

static char nic[IFNAMSIZ] = DEFAULT_NIC;
static char dstmac[ETH_ALEN] = DEFAULT_MAC;
static char m_ip[INET6_ADDRSTRLEN];

static struct argp_option options[] = {
	{"dst" , 'd', "MACADDR", 0, "Stream Destination MAC address" },
	{"nic" , 'i', "NIC" , 0, "Network Interface" },
	{"multicast", 'm', "MULTICAST IP", 0, "Multicast IP address for stream" },
	{ 0 }
};

static error_t parser(int key, char *arg, struct argp_state *state)
{
	int res;

	switch (key) {
	case 'd':
		res = sscanf(arg, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
					&dstmac[0], &dstmac[1], &dstmac[2],
					&dstmac[3], &dstmac[4], &dstmac[5]);
		if (res != 6) {
			fprintf(stderr, "Invalid MAC destination address!\n");
			exit(EXIT_FAILURE);
		}

		break;
	case 'i':
		strncpy(nic, arg, sizeof(nic) - 1);
		break;
	case 'm':
		inet_pton(AF_INET, arg, m_ip);
		break;
	}

	return 0;
}

static struct argp argp = { options, parser };


int main(int argc, char *argv[])
{
	argp_parse(&argp, argc, argv, 0, NULL, NULL);

	printf("Mac Address: %s\n", ether_ntoa((struct ether_addr *)&dstmac));
	printf("Iface: %s\n", nic);

	char ip[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET, m_ip, ip, INET6_ADDRSTRLEN);
	printf("IP Multicast: %s\n", ip);

	struct timedc_avtp *pdu = pdu_create(42, 1, sizeof(uint64_t));
	uint64_t data = 0xdeadbeef;

	pdu_update(pdu, 0, &data, sizeof(data));
	printf("Data in payload: 0x%lx\n", *(uint64_t *)pdu->payload);
	if (pdu)
		free(pdu);
	return 0;
}
