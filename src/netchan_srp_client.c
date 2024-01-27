#include <netchan_srp_client.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <poll.h>
#include <unistd.h>

void * nc_srp_monitor(void *data)
{
	struct nethandler *nh = (struct nethandler *)data;
	if (!nh) {
		ERROR(NULL, "%s(): invalid nethandler.", __func__);
		pthread_exit(NULL);
	}
	struct srp *srp = nh->srp;
	if (!srp) {
		ERROR(NULL, "%s(): invalid srp container\n", __func__);
		pthread_exit(NULL);
	}

	char buffer[1522] = {0};
	struct pollfd fds;

	/* Start receiving messages and process, call into
	 * talker_mrp_client and listener_mrp_client for now.
	 */
	while (nh->running) {
		fds.fd = srp->sock;
		fds.events = POLLIN;
		fds.revents = 0;

		int ret = poll(&fds, 1, 100);
		if (ret < 0)
			pthread_exit(NULL);
		if (ret == 0 || !nh->running)
			continue;
		if ((fds.revents & POLLIN) == 0)
			pthread_exit(NULL);

		struct iovec iov = {
			.iov_len = sizeof(buffer),
			.iov_base = &buffer,
		};
		struct sockaddr_in client = {0};
		struct msghdr msg = {
			.msg_name = &client,
			.msg_namelen = sizeof(client),
			.msg_iov = &iov,
			.msg_iovlen = 1,
		};
		int rxsz = recvmsg(srp->sock, &msg, 0);
		if (rxsz < 0)
			continue;

		if (nc_mrp_talker_process_msg(buffer, rxsz, nh) != 0)
			ERROR(NULL, "%s() FAILED processing MRP message", __func__);

		if (nc_mrp_listener_process_msg(buffer, rxsz, nh) != 0)
			ERROR(NULL, "%s() FAILED processing MRP Listener messages", __func__);


		/* Clear buffer for next round */
		memset(buffer, 0, rxsz);
	}
	return NULL;
}

/* Periodic task that (re)-announces:
 * - talkers without any listeners: every 10 sec
 */
void * nc_srp_talker_announce(void *data)
{
	struct nethandler *nh = (struct nethandler *)data;
	if (!nh) {
		ERROR(NULL, "%s(): invalid nethandler.", __func__);
		pthread_exit(NULL);
	}
	struct srp *srp = nh->srp;
	if (!srp) {
		ERROR(NULL, "%s(): invalid srp container\n", __func__);
		pthread_exit(NULL);
	}
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	while (nh->running) {
		ts.tv_sec += 10;
		if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == -1) {
			WARN(NULL, "%s(): clock_nanosleep failed (%d, %s)", __func__, errno, strerror(errno));
			usleep(500000);
			continue;
		}

		struct channel *talker = nh->du_tx_head;
		while (talker) {
			if (!talker->ready)
				nc_srp_new_talker(talker);
			talker = talker->next;
		}
	}
	return NULL;
}

bool nc_srp_setup(struct nethandler *nh)
{
	struct srp *srp = calloc(1, sizeof(*srp));
	if (!srp) {
		ERROR(NULL, "%s(): SRP memory allocation failed.", __func__);
		goto err_out;
	}

	srp->nh = nh;
	srp->prio_a = DEFAULT_CLASS_A_PRIO;
	srp->prio_b = DEFAULT_CLASS_B_PRIO;
	nh->srp = srp;

	/* Create control sockets */
	srp->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (srp->sock <= 2) {
		ERROR(NULL, "%s() Failed creating mprd-ctrl socket.", __func__);
		goto err_out;
	}

	/* MRPD runs on localhost, setup addr for reaching it */
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(0);
	inet_aton("127.0.0.1", &addr.sin_addr);

	/* FIXME: segfaults */
	if (bind(srp->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ERROR(NULL, "%s() Failed binding sock_rx, cannot receive SRP messages\n", __func__);
		goto err_out;
	}

	/* Start monitor thread to start processing messages.
	 */
	if (pthread_create(&srp->tid, NULL, nc_srp_monitor, (void *)nh)) {
		ERROR(NULL, "%s(): FAILED creating monitor thread", __func__);
		goto err_out;
	}

	/* This functino will block until nc_srp_monitor() have received
	 * an update to the domain!
	 */
	int res = nc_mrp_get_domain(srp);
	if (res == -1) {
		ERROR(NULL, "%s(): mrp_get_domaion() failed (%d; %s)",
			__func__, errno, strerror(errno));
		goto err_out;
	}

	if (!nc_mrp_register_domain_class_a(srp)) {
		ERROR(NULL, "%s(): Failed registrering nethandler with domain", __func__);
		goto err_out;
	}

	/* Join VLAN */
	if (!nc_mrp_join_vlan(srp)) {
		ERROR(NULL, "join VLAN failed (%d; %s)", errno, strerror(errno));
		goto err_out;
	}

	if (pthread_create(&srp->announcer, NULL, nc_srp_talker_announce, (void *)nh)) {
		ERROR(NULL, "Failed starting SRP Talker announcer");
		goto err_out;
	}

	return true;

err_out:
	nh->running = false;
	if (srp->sock> 0)
		close(srp->sock);
	if (srp->tid > 0) {
		pthread_join(srp->tid, NULL);
		srp->tid = 0;
	}
	if (srp->announcer) {
		pthread_join(srp->announcer, NULL);
		srp->announcer = 0;
	}

	if (srp)
		free(srp);
	nh->srp = NULL;

	WARN(NULL, "%s(): done tearing down, SRP failed.", __func__);
	return false;
}

void nc_srp_teardown(struct nethandler *nh)
{
	if (!nh || !nh->use_srp || !nh->srp) {
		ERROR(NULL, "%s(): Cannot teardown SRP for nethandler", __func__);
		return;
	}
	/* stop monitor thread
	 *
	 * We are called from nh_destroy(), which will set nh->running =
	 * false, which *should* stop the thread
	 */
	if (nh->srp->tid > 0) {
		pthread_join(nh->srp->tid, NULL);
		nh->srp->tid = 0;
	}
	if (nh->srp->announcer > 0) {
		pthread_join(nh->srp->announcer, NULL);
		nh->srp->announcer = 0;
	}

	/* leave VLAN */
	nc_mrp_leave_vlan(nh->srp);

	/* unregister domain, currently only A */
	nc_mrp_unregister_domain_class_a(nh->srp);

	/* close socket */
	close(nh->srp->sock);

	/* free srp */
	free(nh->srp);
	nh->srp = NULL;
}

bool nc_srp_new_talker(struct channel *ch)
{
	if (!ch || !ch->nh || !ch->nh->srp)
		return false;

	struct srp *srp = ch->nh->srp;

	/*
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
	int interval = 125000/125000;
	int latency = 3900;

	/* FIXME: verify class */
	if (!nc_mrp_advertise_stream_class_a(srp, ch->sidw, ch->dst, ch->full_size, interval, latency)) {
		ERROR(ch, "FAILED advertising stream");
		return false;
	}

	/* FIXME: make sure monitor starts tracking listeners for this stream */
	INFO(ch, "Stream advertised.");
	return true;
}

bool nc_srp_remove_talker(struct channel *ch)
{
	if (!ch)
		return false;
	if (!ch->nh)
		return false;
	struct srp *srp = ch->nh->srp;
	if (!srp)
		return false;

	/* unannounce talker */
	int interval = 125000/125000;
	int latency = 3900;

	/* FIXME: verify class */
	if (!nc_mrp_unadvertise_stream_class_a(ch->nh->srp, ch->sidw, ch->dst, ch->full_size, interval, latency)) {
		ERROR(ch, "Failed unadvertising stream");
		return false;
	}
	/* FIXME: make sure monitor stops tracking listeners for this stream */
	INFO(ch, "Stream unadvertised");
	return true;
}

bool nc_srp_remove_listener(struct channel *ch)
{
	if (!ch)
		return false;
	if (!nc_mrp_send_leave(ch->nh->srp, ch->sidw)) {
		ERROR(ch, "Failed removing listener from SRP list");
		return false;
	}
	return true;
}
