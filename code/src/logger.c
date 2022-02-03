#include <logger.h>
#include <pthread.h>

struct logc
{
	FILE *logfp;
	pthread_mutex_t m;
};

struct logc * log_create(const char *logfile)
{
	if (!logfile || strlen(logfile) == 0)
		return NULL;

	struct logc *logc = malloc(sizeof(*logc));
	if (!logc)
		return NULL;

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&logc->m, &attr);

	pthread_mutex_lock(&logc->m);
	logc->logfp = fopen(logfile, "w+");
	if (!logc->logfp) {
		fprintf(stderr, "%s(): Failed opening logfile for writing (%d, %s)\n",
			__func__, errno, strerror(errno));
		pthread_mutex_unlock(&logc->m);
		free(logc);
		return NULL;
	}

	fprintf(logc->logfp, "stream_id,sz,seqnr,avtp_ns,cap_ptp_ns,send_ptp_ns,rx_ns,recv_ptp_ns\n");
	pthread_mutex_unlock(&logc->m);

	return logc;
}

static void _log(struct logc *logc,
		struct avtpdu_cshdr *du,
		uint64_t cap_ts_ns,
		uint64_t send_ptp_ns,
		uint64_t rx_ns,
		uint64_t recv_ptp_ns)
{
	pthread_mutex_lock(&logc->m);
	fprintf(logc->logfp, "%ld,%u,%u,%u,%lu,%lu,%lu,%lu\n",
		be64toh(du->stream_id),
		ntohs(du->sdl),
		du->seqnr,
		ntohl(du->avtp_timestamp),
		cap_ts_ns,
		send_ptp_ns,
		rx_ns,
		recv_ptp_ns);
	fflush(logc->logfp);
	pthread_mutex_unlock(&logc->m);
}

void log_tx(struct logc *logc,
	struct avtpdu_cshdr *du,
	uint64_t cap_ts_ns,
	uint64_t send_ptp_ns)
{
	if (logc && du)
		_log(logc, du, cap_ts_ns, send_ptp_ns, 0, 0);
}

void log_rx(struct logc *logc,
	struct avtpdu_cshdr *du,
	uint64_t rx_ns,
	uint64_t recv_ptp_ns)
{
	if (logc && du)
		_log(logc, du, 0, 0, rx_ns, recv_ptp_ns);
}

void log_close_fp(struct logc *logc)
{
	if (!logc)
		return;
	printf("%s(): closing logger\n", __func__);
	if (logc->logfp) {
		pthread_mutex_lock(&logc->m);
		fflush(logc->logfp);
		fclose(logc->logfp);
		logc->logfp = NULL;
		pthread_mutex_unlock(&logc->m);
		pthread_mutex_destroy(&logc->m);
		memset(logc, 0, sizeof(*logc));
	}
}
