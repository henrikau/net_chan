#include <netchan_srp_client.h>
#include <netinet/ether.h>

#include <stdio.h>

static bool nc_srp_client_setup(struct channel *pdu)
{
	pdu->ctx = malloc(sizeof(*pdu->ctx));
	pdu->class_a = malloc(sizeof(*pdu->class_a));
	pdu->class_b = malloc(sizeof(*pdu->class_b));

	if (!pdu->ctx || !pdu->class_a || !pdu->class_b) {
		fprintf(stderr, "%s(): memory allocation for SRP structs failed\n", __func__);
		return false;
	}

	return mrp_ctx_init(pdu->ctx, pdu->nh->verbose) == 0;
}

static bool _nc_set_class_pcp(struct channel *pdu)
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

	if (pdu->nh->verbose) {
		printf("%s(): domain A: %d, B: %d, stream_class: %s\n",
			__func__,
			pdu->class_a->priority,
			pdu->class_b->priority,
			pdu->sc == CLASS_A ? "CLASS_A" : "CLASS_B");
	}

	return true;
}

bool nc_srp_client_listener_setup(struct channel *pdu)
{
	if (!nc_srp_client_setup(pdu)) {
		fprintf(stderr, "%s(): Failed SRP Client Common setup\n", __func__);
		return false;
	}

	if (mrp_create_socket(pdu->ctx) < 0) {
 		fprintf(stderr, "Failed creating MRP CTX socket\n");
 		return false;
 	}

	/* FIXME:
	 *
	 * Is this optimal for a single listener thread, or can
	 * it handle multiple threads for when we have several Rx
	 * channels? */
	if (mrp_listener_monitor(pdu->ctx) != 0) {
		fprintf(stderr, "Failed creating MRP listener monitor\n");
		return false;
	}

	int res = mrp_get_domain(pdu->ctx, pdu->class_a, pdu->class_b);
	if (res == -1) {
		fprintf(stderr, "%s(): mrp_get_domaion() failed (%d; %s)\n",
			__func__, errno, strerror(errno));
		return false;
	}

	if (pdu->nh->verbose) {
		printf("\n%s(): domain PCP_A=%d VID=0x%04x, PCP_B=%d VID=0x%04x, stream_class: %s\n",
			__func__,
			pdu->class_a->priority,
			pdu->class_a->vid,
			pdu->class_b->priority,
			pdu->class_b->vid,
			pdu->sc == CLASS_A ? "CLASS_A" : "CLASS_B");
	}

	if (mrp_report_domain_status(pdu->class_a, pdu->ctx) == -1) {
		fprintf(stderr, "%s(): unable to report domain status! (%d; %s)\n",
			__func__, errno, strerror(errno));
		return false;
	}

	if (mrp_join_vlan(pdu->class_a, pdu->ctx) == -1) {
		fprintf(stderr, "%s(): join VLAN failed (%d; %s)\n",
			__func__, errno, strerror(errno));
		return false;
	}

	return true;
}

bool nc_srp_client_talker_setup(struct channel *pdu)
{
	if (!nc_srp_client_setup(pdu)) {
		fprintf(stderr, "[%lu] Failed SRP Client Common setup\n", pdu->sidw.s64);
		return false;
	}

	/* This forks out a thread to monitor incoming messages */
	if (mrp_connect(pdu->ctx) == -1) {
		fprintf(stderr, "[%lu] mrp_connect() failed (%d; %s)\n",
			pdu->sidw.s64, errno, strerror(errno));
		return false;
	}

	if (mrp_get_domain(pdu->ctx, pdu->class_a, pdu->class_b) == -1) {
		fprintf(stderr, "[%lu] mrp_get_domaion() failed (%d; %s)\n",
			pdu->sidw.s64, errno, strerror(errno));
		return false;
	}
	if (pdu->nh->verbose) {
		printf("\n[%lu] domain PCP_A=%d VID=0x%04x, PCP_B=%d VID=0x%04x, stream_class: %s\n",
			pdu->sidw.s64,
			pdu->class_a->priority,
			pdu->class_a->vid,
			pdu->class_b->priority,
			pdu->class_b->vid,
			pdu->sc == CLASS_A ? "CLASS_A" : "CLASS_B");
	}

	if (mrp_register_domain(pdu->class_a, pdu->ctx) == -1) {
		fprintf(stderr, "[%lu] register-domain failed (%d; %s)\n",
			pdu->sidw.s64, errno, strerror(errno));
		return false;
	}

	if (mrp_join_vlan(pdu->class_a, pdu->ctx) == -1) {
		fprintf(stderr, "[%lu]: join VLAN failed (%d; %s)\n",
			pdu->sidw.s64, errno, strerror(errno));
		return false;
	}

	/* we now have the priorites for class A and B */
	if (!_nc_set_class_pcp(pdu))
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
	int pktsz = pdu->full_size;
	int interval = 125000/125000;
retry:
	if (mrp_advertise_stream(pdu->sidw.s8, pdu->dst,
					pktsz,
					interval,
					3900, pdu->ctx) == -1) {
		fprintf(stderr, "[%lu] advertising stream FAILED (%d : %s)\n",
			pdu->sidw.s64, errno, strerror(errno));
		return false;
	}
	printf("[%lu] Stream advertised\n", pdu->sidw.s64);

	/*
	 * WARNING: mrp-await_listener BLOCKS (with an iter counter).
	 *
	 * If it returns -2, no new listener was found, so resend the
	 * announce as the switch may not reply it
	 *
	 * If netchan is aborted at this stage, the stream will not be torn down!
	 * will poll every 20ms and run for <iter> iterations, 50 corresponds to 1 sec
	 *   50:  1 sec
	 *  500: 10 sec
	 * 1500: 30 sec
	 */
	int res = mrp_await_listener(pdu->sidw.s8, pdu->ctx, 500);
	if (res == -2) {
		printf("[%lu] [TIMEOUT] No listener found, re-advertising! \n", pdu->sidw.s64);
		goto retry;
	} else if (res == -1) {
		fprintf(stderr, "[%lu] mrp_await_listener failed (%d : %s)\n",
			pdu->sidw.s64, errno, strerror(errno));
		return false;
	}
	printf("[%lu] Got listener\n", pdu->sidw.s64);


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
		mrp_send_leave(pdu->ctx);
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
