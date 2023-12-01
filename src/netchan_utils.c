#include <netchan_utils.h>
#include <netchan.h>

struct periodic_timer {
	struct timespec pt_ts;
	uint64_t phase_ns;
	int clock_id;
};


struct periodic_timer * pt_init(uint64_t base_ns, uint64_t phase_ns, int clockid)
{
	/* phase needs to be at lest 100us */
	if (phase_ns < 100 * NS_IN_US)
		return NULL;

	/* Supported IDs, anything else fails. */
	if (!(clockid == CLOCK_REALTIME ||
			clockid == CLOCK_MONOTONIC ||
			clockid == CLOCK_TAI))
		return NULL;

	struct periodic_timer *pt = malloc(sizeof(*pt));
	if (!pt)
		return NULL;
	pt->clock_id = clockid;
	pt->phase_ns = phase_ns;

	switch (clockid) {
	case CLOCK_REALTIME:
		clock_gettime(CLOCK_REALTIME, &pt->pt_ts);
		break;
	case CLOCK_MONOTONIC:
		clock_gettime(CLOCK_MONOTONIC, &pt->pt_ts);;
		break;
	case CLOCK_TAI:
		clock_gettime(CLOCK_TAI, &pt->pt_ts);
		break;
	default:
		/* invalid ID (should not get here, but play it safe) */
		return NULL;
	}
	uint64_t time_now_ns = pt->pt_ts.tv_sec * NS_IN_SEC + pt->pt_ts.tv_nsec;
	/* Use different base than *time now* */
	if (base_ns != 0) {
		/* Set the limit for phase into the past for  the timer */
		if (base_ns < (time_now_ns - phase_ns)) {
			fprintf(stderr, "%s(): TimerBase too old (%lu) , more than 1 phase (%lu) in the past, use a newer base. Time now: %lu\n",
				__func__, base_ns, phase_ns, time_now_ns);
			return NULL;
		}

		pt->pt_ts.tv_sec = base_ns / NS_IN_SEC;
		pt->pt_ts.tv_nsec = base_ns % NS_IN_SEC;
	}
	return pt;
}

int pt_next_cycle(struct periodic_timer *pt)
{
	if (!pt)
		return -1;

	ts_add_ns(&(pt->pt_ts), pt->phase_ns);
	return clock_nanosleep(pt->clock_id, TIMER_ABSTIME, &pt->pt_ts, NULL);
}
