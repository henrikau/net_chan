#pragma once
/*				!!! NOTE !!!
 *
 * This file is a combination of listener_mrp_client and
 * talker_mrp_client from the OpenAnu project hosted by Avnu
 *
 */

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <poll.h>

#include <mrpd.h>
#include <mrp.h>
#include <msrp.h>

struct mrp_talker_ctx
{
	int control_socket;
	volatile int halt_tx;
	volatile int domain_a_valid;
	int domain_class_a_id;
	int domain_class_a_priority;
	u_int16_t domain_class_a_vid;
	volatile int domain_b_valid;
	int domain_class_b_id;
	int domain_class_b_priority;
	u_int16_t domain_class_b_vid;
	unsigned char monitor_stream_id[8];
	volatile int listeners;
};

struct mrp_listener_ctx
{
	int control_socket;
	volatile int talker;
	unsigned char stream_id[8];
	unsigned char dst_mac[6];
	volatile int halt_tx;
	volatile int domain_a_valid;
	int domain_class_a_id;
	int domain_class_a_priority;
	u_int16_t domain_class_a_vid;
	volatile int domain_b_valid;
	int domain_class_b_id;
	int domain_class_b_priority;
	u_int16_t domain_class_b_vid;
};

struct mrp_domain_attr
{
	int id;
	int priority;
	u_int16_t vid;
};

/* common */
int send_msg(char *data, int len, int control_socket);


/* listener */
int create_socket(struct mrp_listener_ctx *ctx);
int mrp_listener_monitor(struct mrp_listener_ctx *ctx);
int report_domain_status(struct mrp_domain_attr *class_a, struct mrp_listener_ctx *ctx);
int join_vlan(struct mrp_domain_attr *class_a, struct mrp_listener_ctx *ctx);
int await_talker(struct mrp_listener_ctx *ctx);
int send_ready(struct mrp_listener_ctx *ctx);
int send_leave(struct mrp_listener_ctx *ctx);
int mrp_listener_disconnect(struct mrp_listener_ctx *ctx);
int mrp_listener_get_domain(struct mrp_listener_ctx *ctx, struct mrp_domain_attr *class_a, struct mrp_domain_attr *class_b);
int mrp_listener_client_init(struct mrp_listener_ctx *ctx);

/* talker */
extern volatile int mrp_error;
int mrp_connect(struct mrp_talker_ctx *ctx);
int mrp_disconnect(struct mrp_talker_ctx *ctx);
int mrp_register_domain(struct mrp_domain_attr *reg_class, struct mrp_talker_ctx *ctx);
int mrp_join_vlan(struct mrp_domain_attr *reg_class, struct mrp_talker_ctx *ctx);
int mrp_advertise_stream(uint8_t * streamid, uint8_t * destaddr, int pktsz, int interval, int latency, struct mrp_talker_ctx *ctx);
int mrp_unadvertise_stream(uint8_t * streamid, uint8_t * destaddr, int pktsz, int interval, int latency, struct mrp_talker_ctx *ctx);
int mrp_await_listener(unsigned char *streamid, struct mrp_talker_ctx *ctx);
int mrp_talker_get_domain(struct mrp_talker_ctx *ctx, struct mrp_domain_attr *class_a, struct mrp_domain_attr *class_b);
int mrp_talker_client_init(struct mrp_talker_ctx *ctx);
