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

/* periodic timer
 *
 * Helper for creating a periodic timer (useful for testing and example
 * codes that wants to run at a precise interval)
 */

struct periodic_timer;
struct channel_attrs;

/**
 * pt_init() Initialize a periodic timer.
 *
 * @params base_ns time to use as starting point for timer. 0 indicates use a current timestamp.
 * @params phase_ns time to wait each cycle
 * @params clockid which clock to use (CLOCK_MONOTONIC, CLOCK_REALTIME, CLOCK_TAI)
 *
 * @returns container for pt.
 */
struct periodic_timer * pt_init(uint64_t base_ns, uint64_t phase_ns, int clockid);

/**
 * pt_init_from_attr() Initialize a periodic timer directly from channel attributes.
 *
 * @params attr channel attributes
 *
 * @returns container for pt.
 */
struct periodic_timer * pt_init_from_attr(struct channel_attrs *attr);

/**
 * pt_next_cycle() wait for next timer cycle
 *
 * This function blocks until the timer expires. It will use the base timer and wi
 *
 */
int pt_next_cycle(struct periodic_timer *);
