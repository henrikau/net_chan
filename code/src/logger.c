/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <logger.h>
#include <pthread.h>

/* log 6hrs worth of single stream, 50Hz freq
 * if multiple streams, total time logged will decrease
 */
#define BSZ 50*3600*6

struct log_buffer
{
	int idx;
	uint64_t sid[BSZ];
	uint16_t sz[BSZ];
	uint8_t seqnr[BSZ];
	uint64_t avtp_ns[BSZ];
	uint64_t cap_ptp_ns[BSZ];
	uint64_t send_ptp_ns[BSZ];
	uint64_t rx_ns[BSZ];
	uint64_t recv_ptp_ns[BSZ];
}__attribute__((packed));

struct delay_buffer
{
	int idx;
	uint64_t ptp_target_ns[BSZ];
	uint64_t cpu_target_ns[BSZ];
	uint64_t cpu_actual_ns[BSZ];
}__attribute__((packed));

struct logc
{
	FILE *logfp;
	FILE *delayfp;
	pthread_mutex_t m;

	/* buffer */
	struct log_buffer *lb;
	struct delay_buffer *db;

};

struct logc * log_create(const char *logfile, bool log_delay)
{
	if (!logfile || strlen(logfile) == 0)
		return NULL;

	struct logc *logc = calloc(1, sizeof(*logc));
	if (!logc)
		return NULL;
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&logc->m, &attr);

	pthread_mutex_lock(&logc->m);

	logc->lb = malloc(sizeof(struct log_buffer));
	if (!logc->lb)
		goto err_out;

	/* make sure all memory is paged in
	 * (we use mlockall(), so once it's read, it should not be forced out.
	 */
	logc->lb->idx = 0;
	for (int i = 0; i < BSZ; i++) {
		logc->lb->sid[i] = 0;
		logc->lb->sz[i] = 0;
		logc->lb->seqnr[i] = 0;
		logc->lb->avtp_ns[i] = 0;
		logc->lb->cap_ptp_ns[i] = 0;
		logc->lb->send_ptp_ns[i] = 0;
		logc->lb->rx_ns[i] = 0;
		logc->lb->recv_ptp_ns[i] = 0;
	}

	logc->logfp = fopen(logfile, "w+");
	if (!logc->logfp) {
		fprintf(stderr, "%s(): Failed opening logfile for writing (%d, %s)\n",
			__func__, errno, strerror(errno));
		goto err_out;
	}
	fprintf(logc->logfp, "stream_id,sz,seqnr,avtp_ns,cap_ptp_ns,send_ptp_ns,rx_ns,recv_ptp_ns\n");
	printf("%s(): opened logfile, buffersize: %zd, %d entries\n",
		__func__, sizeof(struct log_buffer), BSZ);

	if (log_delay) {
		logc->db = malloc(sizeof(struct delay_buffer));
		if (!logc->db)
			goto err_out;
		logc->db->idx = 0;
		for (int i = 0; i < BSZ; i++) {
			logc->db->ptp_target_ns[i] = 0;
			logc->db->cpu_target_ns[i] = 0;
			logc->db->cpu_actual_ns[i] = 0;
		}

		char ldf[256] = {0};
		snprintf(ldf, 255, "%s_d", logfile);
		logc->delayfp = fopen(ldf, "w+");
		fprintf(logc->delayfp, "ptp_target,cpu_target,cpu_actual\n");

		printf("%s(): opened delay-log, buffersize: %zd, %d entries\n",
			__func__, sizeof(struct delay_buffer), BSZ);
	}

	pthread_mutex_unlock(&logc->m);

	return logc;
err_out:
	pthread_mutex_unlock(&logc->m);
	log_close_fp(logc);
	return NULL;
}


static void _log(struct logc *logc,
		struct avtpdu_cshdr *du,
		uint64_t cap_ts_ns,
		uint64_t send_ptp_ns,
		uint64_t rx_ns,
		uint64_t recv_ptp_ns)
{
	if (!logc->lb)
		return;

	pthread_mutex_lock(&logc->m);
	if (logc->lb->idx < BSZ) {
		logc->lb->sid[logc->lb->idx] = be64toh(du->stream_id);
		logc->lb->sz[logc->lb->idx] = ntohs(du->sdl);
		logc->lb->seqnr[logc->lb->idx] = du->seqnr;
		logc->lb->avtp_ns[logc->lb->idx] = ntohl(du->avtp_timestamp);
		logc->lb->cap_ptp_ns[logc->lb->idx] = cap_ts_ns;
		logc->lb->send_ptp_ns[logc->lb->idx] = send_ptp_ns;
		logc->lb->rx_ns[logc->lb->idx] = rx_ns;
		logc->lb->recv_ptp_ns[logc->lb->idx] = recv_ptp_ns;
		logc->lb->idx++;
	}
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
	if (logc->logfp && logc->lb) {
		pthread_mutex_lock(&logc->m);
		for (int i = 0; i < logc->lb->idx; i++) {
			fprintf(logc->logfp, "%lu,%u,%u,%lu,%lu,%lu,%lu,%lu\n",
				logc->lb->sid[i],
				logc->lb->sz[i],
				logc->lb->seqnr[i],
				logc->lb->avtp_ns[i],
				logc->lb->cap_ptp_ns[i],
				logc->lb->send_ptp_ns[i],
				logc->lb->rx_ns[i],
				logc->lb->recv_ptp_ns[i]);
		}
		fflush(logc->logfp);
		fclose(logc->logfp);
		logc->logfp = NULL;
		printf("%s(): wrote %d entries to log\n", __func__, logc->lb->idx);

		if (logc->delayfp && logc->db) {
			/* FIXME: dump buffer */
			for (int i = 0; i < logc->db->idx; i++) {
				fprintf(logc->delayfp, "%lu,%lu,%lu\n",
					logc->db->ptp_target_ns[i],
					logc->db->cpu_target_ns[i],
					logc->db->cpu_actual_ns[i]);
				fflush(logc->delayfp);
			}

			fflush(logc->delayfp);
			fclose(logc->delayfp);
			logc->delayfp = NULL;
			printf("%s(): wrote %d entries to delay log\n", __func__, logc->db->idx);
		}

		if (logc->lb)
			free(logc->lb);
		if (logc->db)
			free(logc->db);

		pthread_mutex_unlock(&logc->m);
		pthread_mutex_destroy(&logc->m);
		memset(logc, 0, sizeof(*logc));
	}
}

void log_delay(struct logc *logc,
	uint64_t ptp_target_ns,
	uint64_t cpu_target_ns,
	uint64_t cpu_actual_ns)
{
	if (logc && logc->delayfp && logc->db) {
		pthread_mutex_lock(&logc->m);
		if (logc->db->idx < BSZ) {
			logc->db->ptp_target_ns[logc->db->idx] = ptp_target_ns;
			logc->db->cpu_target_ns[logc->db->idx] = cpu_target_ns;
			logc->db->cpu_actual_ns[logc->db->idx] = cpu_actual_ns;
			logc->db->idx++;
		}
		pthread_mutex_unlock(&logc->m);
	}
}
