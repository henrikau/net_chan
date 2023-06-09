#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <netchan.h>
#include <srp/mrp_client.h>

/**
 * srp_client_setup() SRP setup helpers
 */
bool nc_srp_client_setup(struct channel *pdu);

bool nc_srp_client_talker_setup(struct channel *pdu);
bool nc_srp_client_listener_setup(struct channel *pdu);

void nc_srp_client_destroy(struct channel *pdu);

#ifdef __cplusplus
}
#endif
