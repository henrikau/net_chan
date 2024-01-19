#include <netchan_standalone.h>
static struct nethandler *_nh;
static bool verbose = false;
static bool do_srp = false;
static bool use_tracebuffer = false;
static int break_us = -1;
static char nc_nic[IFNAMSIZ] = {0};
static char nc_logfile[129] = {0};
static int nc_hmap_size = 42;
static int tx_sock_prio = 3;


int nc_set_nic(char *nic)
{
	strncpy(nc_nic, nic, IFNAMSIZ-1);
	if (verbose)
		printf("%s(): set nic to %s\n", __func__, nc_nic);
	return 0;
}

void nc_set_hmap_size(int sz)
{
	nc_hmap_size = sz;
}
void nc_use_srp(void)
{
	do_srp = true;
}
void nc_use_ftrace(void)
{
	use_tracebuffer = true;
}
void nc_breakval(int b_us)
{
	if (b_us > 0 && b_us < 1000000)
		break_us = b_us;
}

void nc_verbose(void)
{
	verbose = true;
}

void nc_set_logfile(const char *logfile)
{
	strncpy(nc_logfile, logfile, 128);
	if (verbose)
		printf("%s(): set logfile to %s\n", __func__, nc_logfile);
}

void nc_tx_sock_prio(int prio)
{
	if (prio < 0 || prio > 15)
		return;
	tx_sock_prio = prio;
}


struct channel *chan_create_standalone(char *name,
				bool tx_update,
				struct channel_attrs *attrs,
				int sz)
{
	if (!name || !attrs || sz <= 0)
		return NULL;

	int idx = nc_get_chan_idx(name, attrs, sz);
	if (idx < 0)
		return NULL;

	nh_create_init_standalone();
	return tx_update ? chan_create_tx(_nh, &attrs[idx], false) : chan_create_rx(_nh, &attrs[idx], false);
}

/* DEPRECATED */
int nc_rx_create(char *name, struct channel_attrs *attrs, int sz)
{
	struct channel *chan = chan_create_standalone(name, 0, attrs, sz);
	if (!chan)
		return -1;

	return chan->fd_r;
}


int nh_create_init_standalone(void)
{
	/* Make sure we have _nh, but only initialize it *once*
	 */
	if (!_nh) {
		_nh = nh_create_init(nc_nic, nc_hmap_size, nc_logfile);
		if (!_nh)
			return -1;
		nh_set_verbose(_nh, verbose);
		nh_set_srp(_nh, do_srp);
		nh_set_trace_breakval(_nh, break_us);
		if (!nh_set_tx_prio(_nh, tx_sock_prio))
			return -1;
	}
	return 0;
}

void nh_destroy_standalone()
{
	if (_nh)
		nh_destroy(&_nh);
}

