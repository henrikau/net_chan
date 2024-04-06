/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <netchan_args.h>
#include <netchan_standalone.h>

error_t parser(int key, char *arg, struct argp_state *state)
{
      switch (key) {
      case 'i':
	      nc_set_nic(arg);
	      break;
      case 'l':
	      nc_set_logfile(arg);
	      break;
      case 's':
	      nc_set_hmap_size(atoi(arg));
	      break;
      case 'S':
	      nc_use_srp();
	      break;
      case 't':
	      nc_use_ftrace();
	      break;
      case 'b':
	      nc_breakval(atoi(arg));
	      break;
      case 'v':
	      nc_verbose();
	      break;
      case 'p':
	      nc_tx_sock_prio(atoi(arg), SC_CLASS_A);
	      break;
      case 'P':
	      nc_tx_sock_prio(atoi(arg), SC_TAS);
	      break;
       }

       return 0;
}

