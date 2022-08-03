/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <timedc_args.h>
#include <timedc_avtp.h>

error_t parser(int key, char *arg, struct argp_state *state)
{
      switch (key) {
      case 'D':
	      nf_keep_cstate();
	      break;
      case 'i':
	      nf_set_nic(arg);
	      break;
      case 'l':
	      nf_set_logfile(arg);
	      break;
      case 'L':
	      nf_log_delay();
	      break;
      case 's':
	      nf_set_hmap_size(atoi(arg));
	      break;
      case 'S':
	      nf_use_srp();
	      break;
      case 't':
	      nf_use_ftrace();
	      break;
      case 'b':
	      nf_breakval(atoi(arg));
	      break;
      case 'T':
	      nf_use_termtag(arg);
	      break;
      case 'v':
	      nf_verbose();
	      break;
       }

       return 0;
}

