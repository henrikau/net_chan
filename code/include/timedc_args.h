/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#pragma once
#include <argp.h>

static struct argp_option options[] = {
       {"nic"    , 'i', "NIC"    , 0, "Network Interface" },
       {"hmap_sz", 's', "HMAP_SZ", 0, "Size of hash-map for Rx callbacks"},
       {"srp"    , 'S', NULL     , 0, "Enable stream reservation (SRP), including MMRP and MVRP"},
       {"keep_cstate", 'D', NULL, 0, "Do not disable cstate adjustment for powersave (this option is *not* good for RT!)"},
       {"verbose", 'v', NULL     , 0, "Run in verbose mode" },
       {"log_ts" , 'l', "LOGFILE", 0, "Log timestamps to logfile (csv format)"},
       {"log_delay" , 'L', NULL  , 0, "Log wakeup (delay) to file (csv format)"},
       {"ftrace"    , 't', NULL  , 0, "Enable tagging of ftrace tracebuffer from various points in the system"},
       {"break"     , 'b', "USEC", 0, "Stop program and ftrace if calculated E2E delay is larger than [USEC]"},
       {"terminal"  , 'T', "device"  , 0, "Write a character to the tty-device on return from READ/WRITE(_WAIT)"},
       { 0 }
};

error_t parser(int key, char *arg, struct argp_state *state);

static struct argp argp __attribute__((unused)) = {
	.options = options,
	.parser = parser};

#define GET_ARGS() argp_parse(&argp, argc, argv, 0, NULL, NULL)
