#include <netchan_srp_client.h>
#include <unistd.h>		/* usleep() */
#ifndef MSRP_LISTENER_ASKFAILED
#define MSRP_LISTENER_ASKFAILED 1
#endif
#include <arpa/inet.h>		/* inet_aton */

static int _send_msg(struct srp *srp, char *data, int len)
{
	if (!srp || srp->sock < 0 || data == NULL) {
		ERROR(NULL, "%s(): Cannot send (srp=%p, data=%p, len=%d)", __func__, srp, data, len);
		return -1;
	}
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(7500);
	inet_aton("127.0.0.1", &addr.sin_addr);
	return sendto(srp->sock, data, len, 0, (struct sockaddr*)&addr, (socklen_t)sizeof(addr));
}

bool nc_mrp_join_vlan(struct srp *srp)
{
	char msgbuf[33] = {0};

	/* We *should* have the same VLAN for both A and B, so just use A. */
	snprintf(msgbuf, sizeof(msgbuf)-1, "V++:I=%04x", srp->vid_a);
	int res = _send_msg(srp, msgbuf, strlen(msgbuf)+1);

	/* DEBUG(NULL, "%s() %s, %d", __func__, msgbuf, res); */
	return res > 0;
}
bool nc_mrp_leave_vlan(struct srp *srp)
{
	char msgbuf[33] = {0};

	/* We *should* have the same VLAN for both A and B, so just use A. */
	snprintf(msgbuf, sizeof(msgbuf)-1, "V--:I=%04x\n", srp->vid_a);

	return _send_msg(srp, msgbuf, strlen(msgbuf)+1) > 0;
}


static bool _advertise_stream(struct srp *srp,
			bool advertise,
			bool class_a,
			union stream_id_wrapper sidw,
			uint8_t * dst,
			int pktsz,
			int interval,
			int latency)
{
	char msg[129] = {0};

	snprintf(msg, sizeof(msg), "S%s:S=%02X%02X%02X%02X%02X%02X%02X%02X"
		",A=%02X%02X%02X%02X%02X%02X"
		",V=%04X"
		",Z=%d"
		",I=%d"
		",P=%d"
		",L=%d",
		advertise ? "++" : "--",
		sidw.s8[0], sidw.s8[1], sidw.s8[2], sidw.s8[3],
		sidw.s8[4], sidw.s8[5], sidw.s8[6], sidw.s8[7],
		dst[0], dst[1], dst[2], dst[3], dst[4], dst[5],
		class_a ? srp->vid_a : srp->vid_b,
		pktsz, interval,
		(class_a ? srp->prio_a : srp->prio_b) << 5,
		latency);
	/* DEBUG(NULL, "%s: %s", __func__, msg); */
	return _send_msg(srp, msg, strlen(msg)+1) > 0;
}

bool nc_mrp_advertise_stream_class_a(struct srp *srp,
			union stream_id_wrapper sidw,
			uint8_t * dst,
			int pktsz,
			int interval,
			int latency)
{
	return _advertise_stream(srp, true, true, sidw, dst, pktsz, interval, latency);
}

bool nc_mrp_unadvertise_stream_class_a(struct srp *srp,
			union stream_id_wrapper sidw,
			uint8_t * dst,
			int pktsz,
			int interval,
			int latency)
{
	return _advertise_stream(srp, false, true, sidw, dst, pktsz, interval, latency);
}


static bool _listener_join_stream(struct srp *srp,
				bool advertise,
				union stream_id_wrapper sidw)
{
	char msg[129] = {0};
	snprintf(msg, sizeof(msg) - 1, "S%sL:L=%02x%02x%02x%02x%02x%02x%02x%02x, D=%d",
		advertise ? "+" : "-",
		sidw.s8[0], sidw.s8[1], sidw.s8[2], sidw.s8[3],
		sidw.s8[4], sidw.s8[5], sidw.s8[6], sidw.s8[7],
		advertise ? 2 : 3);
	return _send_msg(srp, msg, strlen(msg)+1) > 0;
}

bool nc_mrp_send_ready(struct srp *srp,	union stream_id_wrapper sidw)
{
	return _listener_join_stream(srp, true, sidw);
}

bool nc_mrp_send_leave(struct srp *srp,	union stream_id_wrapper sidw)
{
	return _listener_join_stream(srp, false, sidw);
}


static bool _mrp_update_domain(struct srp *srp, bool class_a, bool reg)
{
	char msgbuf[64] = {0};
	snprintf(msgbuf, sizeof(msgbuf), "S%sD:C=%d,P=%d,V=%04x",
		reg ? "+" : "-",
		class_a ? srp->id_a : srp->id_b,
		class_a ? srp->prio_a : srp->id_b,
		srp->vid_a);
	int res = _send_msg(srp, msgbuf, strlen(msgbuf)+1);

	if (res < 0)
		perror("unable to update domain");
	return res > 0;
}

bool nc_mrp_register_domain_class_a(struct srp *srp)
{
	if (!srp)
		return false;
	return _mrp_update_domain(srp, true, true);
}

bool nc_mrp_register_domain_class_b(struct srp *srp)
{
	if (!srp)
		return false;
	return _mrp_update_domain(srp, false, true);
}

bool nc_mrp_unregister_domain_class_a(struct srp *srp)
{
	if (!srp)
		return false;
	return _mrp_update_domain(srp, true, false);
}

bool nc_mrp_unregister_domain_class_b(struct srp *srp)
{
	if (!srp)
		return false;
	return _mrp_update_domain(srp, false, false);
}


bool nc_mrp_get_domain(struct srp *srp)
{
	if (!srp)
		return false;

	/* Send a message to mrpd and wait for monitor to catch the
	 * reply and update the status
	 */
	char msgbuf[32] = {0};
	sprintf(msgbuf, "S??");
	if (_send_msg(srp, msgbuf, sizeof(msgbuf)) < 0) {
		ERROR(NULL, "%s(): Failed sending msg to MRPD, %d: %s",
			__func__, errno, strerror(errno));
		return false;
	}

	/*
	 * Wait for reply to come and populate with valid domain descriptors
	 *
	 * Do periodic timeouts and poll nh->running so that we can
	 * abort if we're being torn down.
	 */
	while (srp->nh->running && !srp->valid_a  && !srp->valid_b) {
		usleep(20000);
		if (srp->nh->verbose) {
			printf(".");
			fflush(stdout);
		}
	}

	if (!srp->nh->running)
		return false;

	return true;
}


static void _update_srp_fields(struct nethandler *nh, int prio, int id, int vid)
{
	if (id == 6) {
		if (nh->srp->prio_a != prio ||
			nh->srp->id_a != id ||
			nh->srp->vid_a != vid) {

			nh->srp->prio_a  = prio;
			nh->srp->id_a    = id;
			nh->srp->vid_a   = vid;
			nh->srp->valid_a = true;
			// DEBUG(NULL, "%s(): prio_a=%d, id_a=%d, vid_a=%d", __func__, nh->srp->prio_a, nh->srp->id_a, nh->srp->vid_a);
		}
	} else {
		if (nh->srp->prio_b != prio ||
			nh->srp->id_b != id ||
			nh->srp->vid_b != vid) {

			nh->srp->prio_b  = prio;
			nh->srp->id_b    = id;
			nh->srp->vid_b   = vid;
			nh->srp->valid_b = true;
			// DEBUG(NULL, "%s(): prio_b=%d, id_b=%d, vid_b=%d", __func__, nh->srp->prio_b, nh->srp->id_b, nh->srp->vid_b);
		}
	}

}
static int find_stream_id(char *buf, int len, union stream_id_wrapper *sidw)
{
	if (len < 2)
		return 0;
	/* scan through until we find 'S=' and stop at ',' */
	int start = 0;
	int end = 0;
	int idx = 0;

	while (!(buf[idx] == 'S' && buf[idx+1] == '=') && (idx+1) < len)
		idx++;
	if (idx == len)
		return 0;
	start = idx+2;

	while (buf[idx] != ',' && idx < len)
		idx++;
	if (idx == len)
		return 0;
	end = idx;

	for (int i = 0; i < 8; i++) {
		unsigned int id;
		sscanf(&buf[start + i*2],"%02x", &id);
		sidw->s8[i] = id;
	}

	return end;
}

static int find_dst(char *buf, int len, uint8_t dst[6])
{
	int idx = 0, start=0, end = 0;

	while (!(buf[idx] == 'A' && buf[idx+1] == '=') && (idx+1) < len)
		idx++;

	if (idx == len)
		return 0;
	start = idx+2;

	while (buf[idx] != ',' && idx < len)
		idx++;
	if (idx == len)
		return 0;
	end = idx;

	for (int i = 0; i < ETH_ALEN; i++) {
		uint32_t t;
		sscanf(&buf[start + i*2], "%02x", &t);
		dst[i] = t;
	}
	return end;
}

int nc_mrp_listener_process_msg(char *buf, int buflen, struct nethandler *nh)
{
	if (!nh)
		return -1;

	uint32_t id;
	unsigned int vid;
	unsigned int priority;

	union stream_id_wrapper sidw = {0};
	uint8_t dst[ETH_ALEN] = {0};

	/* FIXME: Talker announce, see if we can find a Rx channel
	 * waiting for this talker
	 *
	 * SNE: MRP_NOTIFY_NEW
	 * SJO: MRP_NOTIFY_JOIN
	 */
	int idx = 0;
	if (strncmp(&buf[idx], "SNE T:", 6) == 0 || strncmp(&buf[idx], "SJO T:", 6) == 0) {
		idx += find_stream_id(&buf[idx], buflen-idx, &sidw);
		idx += find_dst(&buf[idx], buflen-idx, dst);
		nh_notify_listener_Tnew(nh, sidw, dst);
		// printf("%s(): %s", __func__, buf);
	} else if (strncmp(buf, "SLE T:S", 7) == 0) {
		/* SLE: MRP_NOTIFY_LEAVE */
		idx += find_stream_id(&buf[idx], buflen-idx, &sidw);
		idx += find_dst(&buf[idx], buflen-idx, dst);
		nh_notify_listener_Tleave(nh, sidw, dst);
		// printf("%s(): %s", __func__, buf);
	}

	/* Update id and prio for both stream classes
	 * SJO: NOTIFY_JOIN (Domain?)
	 */
	if (strncmp(&buf[idx], "SJO D:", 6) == 0)
	{
		idx=8;
		sscanf(&(buf[idx]), "%d", &id);
		idx += 4;
		sscanf(&(buf[idx]), "%d", &priority);
		idx += 4;
		sscanf(&(buf[idx]), "%x", &vid);

		_update_srp_fields(nh, priority, id, vid);
	}
	return 0;
}

/* FIXME: this *really* ought to be cleaned up! */
int nc_mrp_talker_process_msg(char *buf, int buflen, struct nethandler *nh)
{
	/* FIXME: update code to dance around ctx
	 * removed from parameters: struct mrp_ctx *ctx)
	 */

	/*
	 * 1st character indicates application
	 * [MVS] - MAC, VLAN or STREAM
	 */
	unsigned int id;
	unsigned int priority;
	unsigned int vid;
	int i, j, k;
	unsigned int substate;
	union stream_id_wrapper recovered_sidw;
	k = 0;
 next_line:if (k >= buflen)
		return 0;

	/* if (nh->verbose) */
	/* 	printf("<- %s\n", buf); */

	switch (buf[k]) {
	case 'E':
		ERROR(NULL, "[ERROR] %s from mrpd\n", buf);
		break;
	case 'O':
		/* mrp_okay is no longer actively used, but it indicates
		 * a state we should(?) keep track of
		 */
		// mrp_okay = 1;
		// break;
	case 'M':
	case 'V':
		/* printf("%s unhandled from mrpd\n", buf); */
		/* fflush(stdout); */

		/* unhandled for now */
		break;
	case 'L':

		/* parse a listener attribute - see if it matches our monitor_stream_id */
		i = k;
		while (buf[i] != 'D')
			i++;
		i += 2;		/* skip the ':' */
		sscanf(&(buf[i]), "%d", &substate);
		while (buf[i] != 'S')
			i++;
		i += 2;		/* skip the ':' */
		/* FIXME use the new parser to extract streamid */
		for (j = 0; j < 8; j++) {
			sscanf(&(buf[i + 2 * j]), "%02x", &id);
			recovered_sidw.s8[j] =  (unsigned char)id;
		}
		if (substate > MSRP_LISTENER_ASKFAILED) {
			/* A listener has just arrived, notify netchan
			 * to go through the list of Tx channels and see
			 * if we have match */
			nh_notify_talker_Lnew(nh, recovered_sidw, substate);
		}
		fflush(stdout);

		/* try to find a newline ... */
		while ((i < buflen) && (buf[i] != '\n') && (buf[i] != '\0'))
			i++;
		if (i == buflen)
			return 0;
		if (buf[i] == '\0')
			return 0;
		i++;
		k = i;
		goto next_line;
		break;
	case 'D':
		i = k + 4;

		/* save the domain attribute */
		sscanf(&(buf[i]), "%d", &id);
		while (buf[i] != 'P')
			i++;
		i += 2;		/* skip the ':' */
		sscanf(&(buf[i]), "%d", &priority);
		while (buf[i] != 'V')
			i++;
		i += 2;		/* skip the ':' */
		sscanf(&(buf[i]), "%x", &vid);

		_update_srp_fields(nh, priority, id, vid);

		while ((i < buflen) && (buf[i] != '\n') && (buf[i] != '\0'))
			i++;
		if ((i == buflen) || (buf[i] == '\0'))
			return 0;
		i++;
		k = i;
		goto next_line;
		break;
	case 'T':

		/* as simple_talker we don't care about other talkers */
		i = k;
		while ((i < buflen) && (buf[i] != '\n') && (buf[i] != '\0'))
			i++;
		if (i == buflen)
			return 0;
		if (buf[i] == '\0')
			return 0;
		i++;
		k = i;
		goto next_line;
		break;
	case 'S':

		/* handle the leave/join events */
		switch (buf[k + 4]) {
		case 'L':
			i = k + 5;
			while (buf[i] != 'D')
				i++;
			i += 2;	/* skip the ':' */
			sscanf(&(buf[i]), "%d", &substate);
			while (buf[i] != 'S')
				i++;
			i += 2;	/* skip the ':' */
			for (j = 0; j < 8; j++) {
				sscanf(&(buf[i + 2 * j]), "%02x", &id);
				recovered_sidw.s8[j]  = (unsigned char)id;
			}
			switch (buf[k + 1]) {
			case 'L':
				nh_notify_talker_Lleaving(nh, recovered_sidw, substate);
				break;
			case 'J':
			case 'N':
				if (substate > MSRP_LISTENER_ASKFAILED) {
					nh_notify_talker_Lnew(nh, recovered_sidw, substate);
				}

				break;
			}

			/* only care about listeners ... */
		default:
			return 0;
			break;
		}
		break;
	case '\0':
		break;
	}
	return 0;
}
