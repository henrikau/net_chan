#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <netchan.h>
#include <netchan_utils.h>
#include <netinet/in.h>
#include <stdbool.h>

/* Empty mac multicast (ip multicast should be appended (low order 23
 * bit to low order 23)
 */
#define DEFAULT_MCAST {0x01, 0x00, 0x5E, 0x00, 0x00, 0x00}

enum {
	DEFAULT_CLASS_A_PRIO = 3,
	DEFAULT_CLASS_B_PRIO = 2
};

struct srp {
	struct nethandler *nh;

	/* SRP message monitor thread
	 *
	 * Tracks incoming messages from mrpd and calls back into
	 * nethandler to mange talkers and listeners coming and going.
	 */
	pthread_t tid;

	/*
	 * task (re-)announcing all talkers currently without a listener.
	 *
	 * A TxChannel will announce once when it is started, but if a
	 * listener joins later, the talker-announce is not re-sent
	 * causing the listener to potentially wait forever.
	 *
	 * This task periodically iterate through the list of TxChannels
	 * and announce all those without a registred listener.
	 */
	pthread_t announcer;

	int prio_a;
	int id_a;
	bool valid_a;
	int vid_a;

	int prio_b;
	int id_b;
	bool valid_b;

	/* should be identical to vid_a, but conform to
	 * (talker|listener)_process_msg() for now. */
	int vid_b;

	/* Control socket */
	int sock;
};

/**
 *
 */
bool nc_srp_setup(struct nethandler *nh);
void nc_srp_teardown(struct nethandler *nh);

/**
 * nc_srp_client_remove() : tear down active SRP client, remvoing *all*
 * active Tx and Rx channels and leaving the network in an orderly
 * fashion.
 */
bool nc_srp_remove(struct srp *srp);

/**
 * nc_srp_monitor() monitor that keeps track of SRP activity
 *
 * @params nethandler container
 * @returns NULL
 */
void * nc_srp_monitor(void *);

/**
 * nc_srp_start_monitor() for out a thread to run the monitor and update
 * registred channels as remote talker and listeners come and go
 *
 * @params nethandler container
 * @returns true on success
 */
bool nc_srp_start_monitor(struct nethandler *nh);

/**
 * nc_srp_new_talker(): announce a talker and register for update once the monitor detects new listeners
 *
 * @params channel container
 * @return true on success
 */
bool nc_srp_new_talker(struct channel *ch);

/**
 * nc_srp_new_listener(): setup listener and make sure the SRP monitor
 * keeps track of the StreamID and notifies the talker and marks it
 * ready.
 *
 * @params channel container
 * @return true on success
 */
bool nc_srp_new_listener(struct channel *ch);

bool nc_srp_remove_talker(struct channel *ch);
bool nc_srp_remove_listener(struct channel *ch);


/*
 * ===============================================================
 *
 *			HELPERS
 * (netchan_srp_helper.c)
 * ===============================================================
 */

bool nc_mrp_join_vlan(struct srp *srp);

bool nc_mrp_leave_vlan(struct srp *srp);
bool nc_mrp_advertise_stream_class_a(struct srp *srp,
			union stream_id_wrapper sidw,
			uint8_t * dst,
			int pktsz,
			int interval,
			int latency);
bool nc_mrp_unadvertise_stream_class_a(struct srp *srp,
			union stream_id_wrapper sidw,
			uint8_t * dst,
			int pktsz,
			int interval,
			int latency);



bool nc_mrp_send_ready(struct srp *srp,	union stream_id_wrapper sidw);
bool nc_mrp_send_leave(struct srp *srp,	union stream_id_wrapper sidw);

bool nc_mrp_register_domain_class_a(struct srp *srp);
bool nc_mrp_register_domain_class_b(struct srp *srp);
bool nc_mrp_unregister_domain_class_a(struct srp *srp);
bool nc_mrp_unregister_domain_class_b(struct srp *srp);
bool nc_mrp_get_domain(struct srp *srp);

int nc_mrp_talker_process_msg(char *buf, int buflen, struct nethandler *nh);
int nc_mrp_listener_process_msg(char *buf, int buflen, struct nethandler *nh);

#ifdef __cplusplus
}
#endif
