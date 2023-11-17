#pragma once
#include <stdint.h>
#include <time.h>


#define US_IN_MS   (1000L)
#define NS_IN_US   (1000L)
#define NS_IN_MS   (1000L * NS_IN_US)
#define NS_IN_SEC  (1000L * NS_IN_MS)
#define NS_IN_HOUR (3600L * NS_IN_SEC)

static inline void ts_normalize(struct timespec *ts)
{
	if (!ts)
		return;
	while (ts->tv_nsec >= NS_IN_SEC) {
		ts->tv_nsec -= NS_IN_SEC;
		ts->tv_sec++;
	}
}

static inline void ts_subtract_ns(struct timespec *ts, uint64_t sub)
{
	if (!ts)
		return;
	if (sub > ts->tv_nsec) {
		ts->tv_sec--;
		ts->tv_nsec = ts->tv_nsec + NS_IN_SEC;
	}
	ts->tv_nsec -= sub;
}

static inline void ts_add_ns(struct timespec *ts, uint64_t add)
{
	if (!ts)
		return;
	ts->tv_nsec += add;
	ts_normalize(ts);
}
