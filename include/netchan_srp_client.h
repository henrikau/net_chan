#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <netchan.h>
#include <srp/mrp_client.h>

/* Forward declare netchan_avtp container.
 *
 * srp_client is used by netchan, but relies on container to set and
 * update fields
 */
struct netchan_avtp;

/**
 * srp_client_setup() SRP setup helpers
 */
bool nc_srp_client_setup(struct netchan_avtp *pdu);

bool nc_srp_client_talker_setup(struct netchan_avtp *pdu);
bool nc_srp_client_listener_setup(struct netchan_avtp *pdu);

void nc_srp_client_destroy(struct netchan_avtp *pdu);

#ifdef __cplusplus
}
#endif
