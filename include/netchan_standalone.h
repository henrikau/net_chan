#pragma once
#include <netchan.h>
/* --------------------------
 * Main NetChan Macros
 *
 * These macros rely heavily upon a sort-of singleton approach. If you
 * are concerned with interference from other threads, then avoid the
 * macros and use the non-standalone functions.
 */
#define NETCHAN_RX(x) struct channel * x ## _du = \
		chan_create_standalone(#x, 0, nc_channels, \
		ARRAY_SIZE(nc_channels))
#define NETCHAN_TX(x) struct channel * x ## _du = \
		chan_create_standalone(#x, 1, nc_channels, \
		ARRAY_SIZE(nc_channels))

#define WRITE(x,v) chan_send_now(x ## _du, v)
#define WRITE_WAIT(x,v) chan_send_now_wait(x ## _du, v)
#define READ(x,v) chan_read(x ## _du, v)
#define READ_WAIT(x,v) chan_read_wait(x ## _du, v)

#define CLEANUP() nh_destroy_standalone()

int nc_set_nic(char *nic);
void nc_set_hmap_size(int sz);
void nc_use_srp(void);
void nc_use_ftrace(void);
void nc_breakval(int break_us);
void nc_verbose(void);
void nc_set_logfile(const char *logfile);
void nc_tx_sock_prio(int prio, enum stream_class sc);


/**
 * chan_create_standalone - create a new channel using internal refs as much as possible
 *
 * This is a multiplexing function primarily inteded to be used with the
 * macros. In time, this function will be deprecated, use
 * nh_create_init() and chan_create_tx() and chan_create_rx() instead.
 *
 * @param name : name of channel in attribute list
 * @param tx : flag indicating if channel is tx or rx
 * @param arr : channel_attrs array of values
 * @param arr_size : size of channel_attrs array
 *
 * @returns new channel, NULL on error
 */
struct channel *chan_create_standalone(char *name,
				bool tx,
				struct channel_attrs *attrs,
				int arr_size);

/**
 * nh_create_init_standalone - create and initialize a standalone instance of nethandler
 *
 * This creates a 'hidden' nethandler instance kept by the library. It
 * is intended to be used alongside the various macros (in particular
 * NETCHAN_(T|R)X_CREATE()) to hide away resource management etc.
 *
 * It will use the values stored in nc_nic (see nc_set_nic) and
 * nc_hmap_size.
 *
 * @params : void
 * @returns: 0 on success, -1 on error
 */
int nh_create_init_standalone(void);

/**
 * nh_destroy_standalone: destroy singular nethandler created by nh_create_init_standalone()
 *
 * @param void
 * @return: void
 */
void nh_destroy_standalone();
