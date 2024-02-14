/*
 * Copyright 2022 SINTEF AS
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not distributed
 * with this file, You can obtain one at https://mozilla.org/MPL/2.0/
 */
#include <stdio.h>
#include <logger.h>
#include <pthread.h>
#include <arpa/inet.h>

/* log 6hrs worth of single stream, 50Hz freq
 * if multiple streams, total time logged will decrease
 */
#define BSZ 50*3600*6

struct log_buffer
{
	FILE *logfp;
	int idx;
	uint64_t sid[BSZ];
	uint16_t sz[BSZ];
	uint8_t seqnr[BSZ];
	uint64_t avtp_ns[BSZ];
	uint64_t cap_ptp_ns[BSZ];
	uint64_t send_ptp_ns[BSZ];
	uint64_t tx_ns[BSZ];
	uint64_t rx_ns[BSZ];
	uint64_t recv_ptp_ns[BSZ];
}__attribute__((packed));

struct wakeup_delay_buffer
{
	FILE *delayfp;
	int idx;
	uint64_t ptp_target_ns[BSZ];
	uint64_t cpu_target_ns[BSZ];
	uint64_t cpu_actual_ns[BSZ];
}__attribute__((packed));

struct logc
{
	pthread_mutex_t m;
	char *logfile;
	int flush_ctr;

	/* buffer */
	struct log_buffer *lb;
	struct wakeup_delay_buffer *wdb;

};

static int _log_create_wakeup_delay_buffer(struct logc *logc)
{
	logc->wdb = malloc(sizeof(struct wakeup_delay_buffer));
	if (!logc->wdb)
		return -ENOMEM;

	/* make sure all memory is paged in
	 * (we use mlockall(), so once it's read, it should not be forced out.
	 */
	logc->wdb->idx = 0;
	for (int i = 0; i < BSZ; i++) {
		logc->wdb->ptp_target_ns[i] = 0;
		logc->wdb->cpu_target_ns[i] = 0;
		logc->wdb->cpu_actual_ns[i] = 0;
	}
	return 0;
}

static int _log_create_ts(struct logc *logc)
{
	logc->lb = malloc(sizeof(struct log_buffer));
	if (!logc->lb)
		return -ENOMEM;

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
	return 0;
}

struct logc * log_create(const char *logfile)
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

	/* Keep logfile */
	logc->logfile = calloc(1, strlen(logfile) + 1);
	if (!logc->logfile)
		goto err_out;
	strncpy(logc->logfile, logfile, strlen(logfile));

	/* Create buffers */
	if (_log_create_ts(logc) || _log_create_wakeup_delay_buffer(logc))
		goto err_out;

	pthread_mutex_unlock(&logc->m);
	return logc;
err_out:
	pthread_mutex_unlock(&logc->m);
	log_destroy(logc);
	return NULL;
}

void log_destroy(struct logc *logc)
{
  if (!logc)
    return;

  if (logc->logfile)
    free(logc->logfile);
  if (logc->lb) {
    free(logc->lb);
    logc->lb = NULL;
  }
  if (logc->wdb) {
    free(logc->wdb);
    logc->wdb = NULL;
  }

  free(logc);
}

static void _log_reset(struct logc *logc)
{
	if (logc->lb)
		logc->lb->idx = 0;
	if (logc->wdb)
		logc->wdb->idx = 0;
}

void log_reset(struct logc *logc)
{
	if (!logc)
		return;
	pthread_mutex_lock(&logc->m);
	_log_reset(logc);
	pthread_mutex_unlock(&logc->m);
}

static void _flush_ts(const char *logfile, struct log_buffer *lb)
{
	if (!logfile || !lb)
		return;

	/* no entries written, avoid creating new logfile */
	if (lb->idx == 0)
		return;

	FILE *fp = fopen(logfile, "w+");
	if (fp) {
		fprintf(fp, "stream_id,sz,seqnr,avtp_ns,cap_ptp_ns,send_ptp_ns,tx_ns,rx_ns,recv_ptp_ns\n");
		for (int i = 0; i < lb->idx; i++) {
			fprintf(fp, "%lu,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu\n",
				lb->sid[i],
				lb->sz[i],
				lb->seqnr[i],
				lb->avtp_ns[i],
				lb->cap_ptp_ns[i],
				lb->send_ptp_ns[i],
				lb->tx_ns[i],
				lb->rx_ns[i],
				lb->recv_ptp_ns[i]);
		}
		fflush(fp);
		fclose(fp);
		printf("%s(): wrote %d entries to log (%s)\n", __func__, lb->idx, logfile);
	}
}

static void _flush_wakeup_delay(const char *logfile, struct wakeup_delay_buffer *wdb)
{
	if (!logfile || !wdb)
		return;

	/* no entries written, no read_now_wait() or write_now_wait() used */
	if (wdb->idx == 0)
		return;

	FILE *fp = fopen(logfile, "w+");
	if (fp) {
		fprintf(fp, "ptp_target,cpu_target,cpu_actual\n");
		for (int i = 0; i < wdb->idx; i++) {
			fprintf(fp, "%lu,%lu,%lu\n",
				wdb->ptp_target_ns[i],
				wdb->cpu_target_ns[i],
				wdb->cpu_actual_ns[i]);
		}
		fflush(fp);
		fclose(fp);
		printf("%s(): wrote %d entries to wakeup-delay-log (%s)\n", __func__, wdb->idx, logfile);
	}
}

void log_flush_and_rotate(struct logc *logc)
{
	if (!logc)
		return;
	pthread_mutex_lock(&logc->m);

	/* Writing content to logbuffer */
	if (logc->lb) {
		char tsf[256] = {0};
		snprintf(tsf, 255, "%s-%d", logc->logfile, logc->flush_ctr);
		_flush_ts(tsf, logc->lb);
	}

	if (logc->wdb) {
		char ldf[256] = {0};
		snprintf(ldf, 255, "%s_d-%d", logc->logfile, logc->flush_ctr);
		_flush_wakeup_delay(ldf, logc->wdb);
	}
	logc->flush_ctr++;
	_log_reset(logc);
	pthread_mutex_unlock(&logc->m);
}

static void _log(struct logc *logc,
		struct avtpdu_cshdr *du,
		uint64_t cap_ts_ns,
		uint64_t send_ptp_ns,
		uint64_t tx_ns,
		uint64_t rx_ns,
		uint64_t recv_ptp_ns)
{
	pthread_mutex_lock(&logc->m);
	if (logc->lb->idx < BSZ) {
		logc->lb->sid[logc->lb->idx] = be64toh(du->stream_id);
		logc->lb->sz[logc->lb->idx] = ntohs(du->sdl);
		logc->lb->seqnr[logc->lb->idx] = du->seqnr;
		logc->lb->avtp_ns[logc->lb->idx] = ntohl(du->avtp_timestamp);
		logc->lb->cap_ptp_ns[logc->lb->idx] = cap_ts_ns;
		logc->lb->send_ptp_ns[logc->lb->idx] = send_ptp_ns;
		logc->lb->tx_ns[logc->lb->idx] = tx_ns;
		logc->lb->rx_ns[logc->lb->idx] = rx_ns;
		logc->lb->recv_ptp_ns[logc->lb->idx] = recv_ptp_ns;
		logc->lb->idx++;
	} else {
	  log_flush_and_rotate(logc);
	}
	pthread_mutex_unlock(&logc->m);
}

void log_tx(struct logc *logc,
	struct avtpdu_cshdr *du,
	uint64_t cap_ts_ns,
	uint64_t send_ptp_ns,
	uint64_t tx_ns)
{
	if (logc && du)
		_log(logc, du, cap_ts_ns, send_ptp_ns, tx_ns, 0, 0);
}

void log_rx(struct logc *logc,
	struct avtpdu_cshdr *du,
	uint64_t rx_ns,
	uint64_t recv_ptp_ns)
{
	if (logc && du)
		_log(logc, du, 0, 0, 0, rx_ns, recv_ptp_ns);
}

void log_wakeup_delay(struct logc *logc,
	uint64_t ptp_target_ns,
	uint64_t cpu_target_ns,
	uint64_t cpu_actual_ns)
{
	if (!logc)
		return;

	pthread_mutex_lock(&logc->m);
	if (logc->wdb->idx < BSZ) {
		logc->wdb->ptp_target_ns[logc->wdb->idx] = ptp_target_ns;
		logc->wdb->cpu_target_ns[logc->wdb->idx] = cpu_target_ns;
		logc->wdb->cpu_actual_ns[logc->wdb->idx] = cpu_actual_ns;
		logc->wdb->idx++;
	} else {
	  log_flush_and_rotate(logc);
	}
	pthread_mutex_unlock(&logc->m);
}
