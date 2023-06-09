#include <netchan_srp_client.h>

#include <netinet/ether.h>

#include <stdio.h>

bool nc_srp_client_setup(struct channel *pdu)
{
	pdu->ctx = malloc(sizeof(*pdu->ctx));
	pdu->class_a = malloc(sizeof(*pdu->class_a));
	pdu->class_b = malloc(sizeof(*pdu->class_b));

	if (!pdu->ctx || !pdu->class_a || !pdu->class_b) {
		fprintf(stderr, "%s(): memory allocation for SRP structs failed\n", __func__);
		return false;
	}


	return true;
}

static bool _set_socket_prio(struct channel *pdu)
{
	switch (pdu->sc) {
	case CLASS_A:
		pdu->socket_prio = pdu->class_a->priority;
		break;
	case CLASS_B:
		pdu->socket_prio = pdu->class_b->priority;
		break;
	default:
		fprintf(stderr, "%s(): unhandled TSN stream priority\n", __func__);
		return false;
	}

	printf("%s(): domain A: %d, B: %d, stream_class: %s\n",
		__func__,
		pdu->class_a->priority,
		pdu->class_b->priority,
		pdu->sc == CLASS_A ? "CLASS_A" : "CLASS_B");

	if (setsockopt(pdu->tx_sock, SOL_SOCKET, SO_PRIORITY,
			&pdu->socket_prio, sizeof(pdu->socket_prio)) < 0) {
		fprintf(stderr, "%s(): failed setting socket priority (%d, %s)\n",
			__func__, errno, strerror(errno));
		return false;
	}

	return true;
}

bool nc_srp_client_listener_setup(struct channel *pdu)
{
	if (create_socket(pdu->ctx) < 0) {
		fprintf(stderr, "Failed creating MRP CTX socket\n");
		return false;
	}

	if (mrp_listener_monitor(pdu->ctx) != 0) {
		fprintf(stderr, "Failed creating MRP listener monitor\n");
		return false;
	}

	printf("%s(): domain A: %d, B: %d, stream_class: %s\n",
		__func__,
		pdu->class_a->priority,
		pdu->class_b->priority,
		pdu->sc == CLASS_A ? "CLASS_A" : "CLASS_B");
	return false;

	report_domain_status(pdu->class_a, pdu->ctx);
	mrp_join_vlan(pdu->class_a, pdu->ctx);

	return true;
}

bool nc_srp_client_talker_setup(struct channel *pdu)
{
	int res = mrp_ctx_init(pdu->ctx);
	if (res == -1) {
		fprintf(stderr, "%s(): CTX init failed (%d; %s)\n",
			__func__, errno, strerror(errno));
		return false;
	}

	res = mrp_connect(pdu->ctx);
	if (res == -1) {
		fprintf(stderr, "%s(): mrp_connect() failed (%d; %s)\n",
			__func__, errno, strerror(errno));
		return false;
	}

	res = mrp_get_domain(pdu->ctx, pdu->class_a, pdu->class_b);
	if (res == -1) {
		fprintf(stderr, "%s(): mrp_get_domaion() failed (%d; %s)\n",
			__func__, errno, strerror(errno));
		return false;
	}

	res = mrp_register_domain(pdu->class_a, pdu->ctx);
	if (res == -1) {
		fprintf(stderr, "%s(): register-domain failed (%d; %s)\n",
			__func__, errno, strerror(errno));
		return false;
	}

	res = mrp_join_vlan(pdu->class_a, pdu->ctx);
	if (res == -1) {
		fprintf(stderr, "%s(): join VLAN failed (%d; %s)\n",
			__func__, errno, strerror(errno));
		return false;
	}

	/* we now have the priorites for class A and B */
	if (!_set_socket_prio(pdu))
		return false;

	return false;
	/*
	 * Currently we allow for 8kHz inter frame gap,
	 * even if the actual freq is much lower than
	 * than 8kHz because mrp_advertise_stream() does
	 * not handle fractional values for interval
	 *
	 * Size calculation
	 * Ether, vlan, crc: : 14 + 4 + 4: 22
	 * Preample, start, IPG: 7 + 1 + 12 : 20
	 *
	 * interval: IPG / 125uS (or 250 for class B)
	 * IPG: (1/freq * 1e9)ns
	 * Note: interval is an *integer*, so no fractional values
	 *
	 * 125000/125000 := 1
	 */
	int pktsz = pdu->payload_size + 22 + 20 + sizeof(struct ether_addr);
	int interval = 125000/125000;
	res = mrp_advertise_stream(pdu->sidw.s8, pdu->dst,
				pktsz,
				interval,
				3900, pdu->ctx);
	printf("%s(): advertised stream: %d\n", __func__, res);

	/*
	 * WARNING: mrp-awai_listener BLOCKS
	 *
	 * If netchan is aborted at this stage, the stream will not be torn down!
	 */
	res = mrp_await_listener(pdu->sidw.s8, pdu->ctx);
	if (res) {
		printf("%s(): mrp_await_listener failed\n", __func__);
		return false;
	}
	return true;
}

void nc_srp_client_destroy(struct channel *pdu)
{
	if (!pdu)
		return;

	if (pdu->tx_sock != -1) {
		int pktsz = pdu->payload_size + 22 + 20 + sizeof(struct ether_addr);
		int interval = 125000/125000;
		if (mrp_unadvertise_stream(pdu->sidw.s8, pdu->dst,
						pktsz,
						interval,
						3900, pdu->ctx) == -1) {
			fprintf(stderr, "%s(): failed to unadvertise stream\n", __func__);

		}
	} else {
		send_leave(pdu->ctx);
	}

	if (mrp_disconnect(pdu->ctx) < 0) {
		fprintf(stderr, "%s(): failed to send mrp disconnect() := %s\n",
			__func__, strerror(errno));
	}
	if (pdu->ctx)
		free(pdu->ctx);
	if (pdu->class_a)
		free(pdu->class_a);
	if (pdu->class_b)
		free(pdu->class_b);
}