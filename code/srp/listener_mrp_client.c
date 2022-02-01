/*
  Copyright (c) 2013 Katja Rohloff <Katja.Rohloff@uni-jena.de>
  Copyright (c) 2014, Parrot SA

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <srp/mrp_client.h>

int msg_process(char *buf, int buflen, struct mrp_ctx *ctx)
{
	uint32_t id;
	int j, l=0;
	unsigned int vid;
	unsigned int priority;

	fprintf(stderr, "Msg: %s\n", buf);

	if (strncmp(buf, "SNE T:", 6) == 0 || strncmp(buf, "SJO T:", 6) == 0)
	{
		l = 6; /* skip "Sxx T:" */
		while ((l < buflen) && ('S' != buf[l++]));
		if (l == buflen)
			return -1;
		l++;
		for(j = 0; j < 8 ; l+=2, j++)
		{
			sscanf(&buf[l],"%02x",&id);
			ctx->stream_id[j] = (unsigned char)id;
		}
		l+=3;
		for(j = 0; j < 6 ; l+=2, j++)
		{
			sscanf(&buf[l],"%02x",&id);
			ctx->dst_mac[j] = (unsigned char)id;
		}
		ctx->talker = 1;
	}

	if (strncmp(buf, "SJO D:", 6) == 0)
	{
		l=8;
		sscanf(&(buf[l]), "%d", &id);
		l=l+4;
		sscanf(&(buf[l]), "%d", &priority);
		l=l+4;
		sscanf(&(buf[l]), "%x", &vid);

		if (id == 6)
		{
			ctx->domain_class_a_id = id;
			ctx->domain_class_a_priority = priority;
			ctx->domain_class_a_vid = vid;
			ctx->domain_a_valid = 1;
		}
		else
		{
			ctx->domain_class_b_id = id;
			ctx->domain_class_b_priority = priority;
			ctx->domain_class_b_vid = vid;
			ctx->domain_b_valid = 1;
		}
		l+=4;

	}
	return 0;
}

/*
 * public
 */

int create_socket(struct mrp_ctx *ctx) // TODO FIX! =:-|
{
	struct sockaddr_in addr;
	ctx->control_socket = socket(AF_INET, SOCK_DGRAM, 0);

	/** in POSIX fd 0,1,2 are reserved */
	if (2 > ctx->control_socket)
	{
		if (-1 > ctx->control_socket)
			close(ctx->control_socket);
	return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(0);

	if(0 > (bind(ctx->control_socket, (struct sockaddr*)&addr, sizeof(addr))))
	{
		fprintf(stderr, "Could not bind socket.\n");
		close(ctx->control_socket);
		return -1;
	}
	return 0;
}

void *mrp_listener_monitor_thread(void *arg)
{
	char *msgbuf;
	struct sockaddr_in client_addr;
	struct msghdr msg;
	struct iovec iov;
	int bytes = 0;
	struct pollfd fds;
	int rc;
	struct mrp_ctx *ctx = (struct mrp_ctx*) arg;

	msgbuf = (char *)malloc(MAX_MRPD_CMDSZ);
	if (NULL == msgbuf)
		return NULL;
	while (!ctx->halt_tx) {
		fds.fd = ctx->control_socket;
		fds.events = POLLIN;
		fds.revents = 0;
		rc = poll(&fds, 1, 100);
		if (rc < 0) {
			free(msgbuf);
			pthread_exit(NULL);
		}
		if (rc == 0)
			continue;
		if ((fds.revents & POLLIN) == 0) {
			free(msgbuf);
			pthread_exit(NULL);
		}
		memset(&msg, 0, sizeof(msg));
		memset(&client_addr, 0, sizeof(client_addr));
		memset(msgbuf, 0, MAX_MRPD_CMDSZ);
		iov.iov_len = MAX_MRPD_CMDSZ;
		iov.iov_base = msgbuf;
		msg.msg_name = &client_addr;
		msg.msg_namelen = sizeof(client_addr);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		bytes = recvmsg(ctx->control_socket, &msg, 0);
		if (bytes < 0)
			continue;
		msg_process(msgbuf, bytes, ctx);
	}
	free(msgbuf);
	pthread_exit(NULL);
}


int mrp_listener_monitor(struct mrp_ctx *ctx)
{
	int rc;
	rc = pthread_attr_init(&ctx->rx_monitor_attr);
	rc |= pthread_create(&ctx->rx_monitor_thread, &ctx->rx_monitor_attr, mrp_listener_monitor_thread, ctx);
	return rc;
}

int report_domain_status(struct mrp_domain_attr *class_a, struct mrp_ctx *ctx)
{
	char* msgbuf;
	int rc;

	msgbuf = malloc(1500);
	if (NULL == msgbuf)
		return -1;
	memset(msgbuf, 0, 1500);
	sprintf(msgbuf, "S+D:C=%d,P=%d,V=%04x", class_a->id, class_a->priority, class_a->vid);
	rc = mrp_send_msg(msgbuf, 1500, ctx->control_socket);
	free(msgbuf);

	if (rc != 1500)
		return -1;
	else
		return 0;
}

int await_talker(struct mrp_ctx *ctx)
{
	while (0 == ctx->talker)
		usleep(5000);
	return 0;
}

int send_ready(struct mrp_ctx *ctx)
{
	char *databuf;
	int rc;

	databuf = malloc(1500);
	if (NULL == databuf)
		return -1;
	memset(databuf, 0, 1500);
	sprintf(databuf, "S+L:L=%02x%02x%02x%02x%02x%02x%02x%02x, D=2",
		     ctx->stream_id[0], ctx->stream_id[1],
		     ctx->stream_id[2], ctx->stream_id[3],
		     ctx->stream_id[4], ctx->stream_id[5],
		     ctx->stream_id[6], ctx->stream_id[7]);
	rc = mrp_send_msg(databuf, 1500, ctx->control_socket);
	free(databuf);

	if (rc != 1500)
		return -1;
	else
		return 0;
}

int send_leave(struct mrp_ctx *ctx)
{
	char *databuf;
	int rc;

	databuf = malloc(1500);
	if (NULL == databuf)
		return -1;
	memset(databuf, 0, 1500);
	sprintf(databuf, "S-L:L=%02x%02x%02x%02x%02x%02x%02x%02x, D=3",
		     ctx->stream_id[0], ctx->stream_id[1],
		     ctx->stream_id[2], ctx->stream_id[3],
		     ctx->stream_id[4], ctx->stream_id[5],
		     ctx->stream_id[6], ctx->stream_id[7]);
	rc = mrp_send_msg(databuf, 1500, ctx->control_socket);
	free(databuf);

	if (rc != 1500)
		return -1;
	else
		return 0;
}
