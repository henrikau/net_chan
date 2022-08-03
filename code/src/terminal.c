/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <terminal.h>

int term_open(const char *termpath)
{
	(void)termpath;
	return -1;
}

void term_close(int ttys)
{
	(void)ttys;
}

int term_write(int ttys, const char *msg, size_t msglen)
{
	(void)ttys;
	(void)msg;
	(void)msglen;
	return -1;
}
