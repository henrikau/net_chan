#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <argp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/ether.h>
#include <timedc_avtp.h>

static unsigned char nic[IFNAMSIZ] = DEFAULT_NIC;
static unsigned char dstmac[ETH_ALEN] = DEFAULT_MAC;
static unsigned char m_ip[INET6_ADDRSTRLEN];

static struct argp_option options[] = {
	{"dst" , 'd', "MACADDR", 0, "Stream Destination MAC address" },
	{"nic" , 'i', "NIC" , 0, "Network Interface" },
	{"multicast", 'm', "MULTICAST IP", 0, "Multicast IP address for stream (can be used instead of MAC)" },
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
		strncpy((char *)nic, arg, sizeof(nic) - 1);
		break;
	case 'm':
		inet_pton(AF_INET, arg, m_ip);
		break;
	}

	return 0;
}

static struct argp argp = { options, parser };

static int cb(void *priv, struct timedc_avtp *pdu)
{
	if (!priv)
		printf("Calling cb, priv: %p, pdu: %p\n", priv, pdu);
	else
		printf("%s pdu: %p\n", (char *)priv, pdu);

	return 0;
}

int main(int argc, char *argv[])
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	argp_parse(&argp, argc, argv, 0, NULL, NULL);

	printf("Iface: %s\n", nic);
	printf("Mac Address: %s\n", ether_ntoa((struct ether_addr *)&dstmac));

	char ip[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET, m_ip, ip, INET6_ADDRSTRLEN);
	printf("IP address: %s\n", ip);

	struct timedc_avtp *pdu = pdu_create(nic, 16, 42, sizeof(uint64_t));
	struct timedc_avtp *pdu2 = pdu_create(nic, 16, 10, sizeof(uint64_t));

	uint64_t data = 0xdeadbeef;


	pdu_update(pdu, 0, &data, sizeof(data));
	pdu_update(pdu2, 0, &data, sizeof(data));

	printf("Data in payload: 0x%lx\n", *(uint64_t *)pdu_get_payload(pdu));

	struct nethandler *nh = nh_init(nic, 16, dstmac);
	char *msg = "cb for 10";
	nh_reg_callback(nh, 42, "msg for 42", cb);
	nh_reg_callback(nh, 10, (void*)msg, cb);

	printf("Feeding data: %d\n", nh_feed_pdu(nh, pdu));
	printf("Feeding data: %d\n", nh_feed_pdu(nh, pdu2));
	if (pdu)
		free(pdu);
	if (pdu2)
		free(pdu2);

	nh_start_rx(nh);

	/* send a frame, tickle cb */

	printf("nh: %p\n", nh);
	nh_destroy(&nh);
	printf("nh: %p\n", nh);

	printf("------------------------------\n\n");
	return 0;
}
