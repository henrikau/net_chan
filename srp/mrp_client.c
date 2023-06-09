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
	if (control_socket == -1 || data == NULL) {
		fprintf(stderr, "%s(): control_socket=%d, data=%p\n", __func__, control_socket, data);
		return -1;
	}

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

int mrp_join_vlan(struct mrp_domain_attr *reg_class, struct mrp_ctx *ctx)
{
	char msgbuf[32] = {0};
	sprintf(msgbuf, "V++:I=%04x\n",reg_class->vid);
	return mrp_send_msg(msgbuf, strlen(msgbuf)+1, ctx->control_socket);
}

int mrp_get_domain(struct mrp_ctx *ctx,
		struct mrp_domain_attr *class_a,
		struct mrp_domain_attr *class_b)
{
	if (!ctx || !class_a || !class_b)
		return -ENOMEM;

	char msgbuf[32] = {0};
	sprintf(msgbuf, "S??");
	if (mrp_send_msg(msgbuf, sizeof(msgbuf), ctx->control_socket) == -1) {
		fprintf(stderr, "%s(): Failed sending msg to MRPD, %d: %s\n",
			__func__, errno, strerror(errno));
		return -1;
	}

	memset(class_a, 0, sizeof(*class_a));
	memset(class_b, 0, sizeof(*class_b));

	/*
	 * Wait for reply to come and populate with valid domain descriptors
	 */
	while (!ctx->halt_tx && (ctx->domain_a_valid == 0) && (ctx->domain_b_valid == 0)) {
		usleep(20000);
		printf("."); fflush(stdout);
	}

	if (ctx->halt_tx)
		return 0;

	class_a->id = ctx->domain_class_a_id;
	class_a->priority = ctx->domain_class_a_priority;
	class_a->vid = ctx->domain_class_a_vid;

	class_b->id = ctx->domain_class_b_id;
	class_b->priority = ctx->domain_class_b_priority;
	class_b->vid = ctx->domain_class_b_vid;

	return 0;
}
