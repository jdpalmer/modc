/*
 * POSIX <time.h> for %C — curated; host libc at link.
 */
#pragma once

#include <stdint.h>

struct timespec {
	int64_t tv_sec;
	long tv_nsec;
};

#ifdef __APPLE__
#define CLOCK_MONOTONIC 6
#else
#define CLOCK_MONOTONIC 1
#endif

int nanosleep(const struct timespec *req, struct timespec *rem);
int clock_gettime(int clk, struct timespec *tp);
