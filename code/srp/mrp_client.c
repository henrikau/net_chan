#include <errno.h>
#include <srp/mrp_client.h>
int mrp_ctx_init(struct mrp_ctx *ctx)
{
	if (!ctx)
		return -1;
	memset(ctx, 0, sizeof(*ctx));
	ctx->control_socket = -1;
	return 0;
}

int mrp_send_msg(char *data, int len, int control_socket)
{
	struct sockaddr_in addr;
	if (control_socket == -1 || data == NULL)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(MRPD_PORT_DEFAULT);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	inet_aton("127.0.0.1", &addr.sin_addr);
	return sendto(control_socket, data, len, 0,
		(struct sockaddr*)&addr, (socklen_t)sizeof(addr));
}

int mrp_disconnect(struct mrp_ctx *ctx)
{
	char msgbuf[] = "BYE";
	return mrp_send_msg(msgbuf, sizeof(msgbuf), ctx->control_socket);
}

