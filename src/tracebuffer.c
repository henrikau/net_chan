/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <tracebuffer.h>
#include <errno.h>
#include <string.h>

void _tb_tracefs_write_single(const char *attr, const char *val)
{
	char fname[256] = {0};
	snprintf(fname, sizeof(fname)-1, "/sys/kernel/tracing/%s", attr);
	FILE *f = fopen(fname, "w");
	if (f) {
		fprintf(f, "%s\n", val);
		fflush(f);
		fclose(f);
	}

}

FILE * tb_open(void)
{
	_tb_tracefs_write_single("tracing_on", "0");
	_tb_tracefs_write_single("buffer_size_kb", "8192");

	_tb_tracefs_write_single("events/sched/enable", "1");
	_tb_tracefs_write_single("events/net/enable", "1");
	_tb_tracefs_write_single("events/irq/enable", "1");

	/* Only enable this when debugging, this is *very* invasive */
	// _tb_tracefs_write_single("current_tracer", "function");

	FILE *tracefd = fopen("/sys/kernel/tracing/trace_marker", "w");
	if (!tracefd)
		return NULL;
	_tb_tracefs_write_single("tracing_on", "1");

	printf("%s() tracefile opened\n", __func__);
	return tracefd;
}

void tb_close(FILE *tracefd)
{
	_tb_tracefs_write_single("tracing_on", "0");
	if (tracefd) {
		fflush(tracefd);
		fclose(tracefd);
		printf("%s(): tracebuffer closed\n", __func__);
	}
}

void tb_tag(FILE *tracefd, const char *fmt, ...)
{
	if (!tracefd)
		return;

	char buffer[256] = {0};
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	int r = fprintf(tracefd, "%s", buffer);
	if (r < 0) {
		fprintf(stderr, "%s(): failed writing line to trace! (%d, %s)\n",
			__func__, errno, strerror(errno));
	}
	fflush(tracefd);
}
