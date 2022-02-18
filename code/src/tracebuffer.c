#include <tracebuffer.h>
#include <errno.h>
#include <string.h>

FILE * tb_open(void)
{
	FILE *tracing_on = fopen("/sys/kernel/debug/tracing/tracing_on", "w");
	if (!tracing_on) {
		fprintf(stderr, "%s(): could not locate tracing_on, cannot enable tracing\n", __func__);
		return NULL;
	}

	fprintf(tracing_on, "1\n");
	fflush(tracing_on);
	fclose(tracing_on);

	FILE *tracefd = fopen("/sys/kernel/debug/tracing/trace_marker", "w");
	if (!tracefd)
		return NULL;
	printf("%s() tracefile opened\n", __func__);
	return tracefd;
}

void tb_close(FILE *tracefd)
{
	if (!tracefd)
		return;

	FILE *tracing_on = fopen("/sys/kernel/debug/tracing/tracing_on", "w");
	if (!tracing_on) {
		fprintf(stderr, "%s(): could not open tracing_on to stop trace\n", __func__);
		goto out;
	}
	fprintf(tracing_on, "0\n");
	fflush(tracing_on);
	fclose(tracing_on);

out:
	fflush(tracefd);
	fclose(tracefd);
	printf("%s(): tracebuffer closed\n", __func__);
}

void tb_tag(FILE *tracefd, const char *line)
{
	if (!tracefd) {
		printf("%s() no tracefd available\n", __func__);
		return;
	}
	int r = fprintf(tracefd, "%s\n", line);
	if (r < 0) {
		fprintf(stderr, "%s(): failed writing line to trace! (%d, %s)\n",
			__func__, errno, strerror(errno));
	}
	fflush(tracefd);
}
