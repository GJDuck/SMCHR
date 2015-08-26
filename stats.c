/*
 * stats.c
 * Copyright (C) 2014 National University of Singapore
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "misc.h"
#include "stats.h"

#ifdef WINDOWS
#include <windows.h>
#else       /* WINDOWS */
#include <sys/time.h>
#endif      /* WINDOWS */

/*
 * Various counters.
 */
size_t stat_constraints;
size_t stat_backtracks;
size_t stat_clauses;
size_t stat_decisions;
size_t stat_pivots;
static size_t stat_time;

/*
 * Get time.
 */
extern size_t timer(void)
{
    size_t r;
#if defined WINDOWS
    LARGE_INTEGER c, f;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    r = (size_t)(((double)c.QuadPart * 1000000000.0)/ (double)f.QuadPart);
#elif defined MACOSX
    struct timeval tv;
    gettimeofday(&tv, NULL);
    r = (size_t)tv.tv_sec * 1000000000 + (size_t)tv.tv_usec * 1000;
#else
    struct timespec t;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t) != 0)
        panic("failed to get CPU time: %s", strerror(errno));
    r = (size_t)t.tv_sec * 1000000000 + (size_t)t.tv_nsec;
#endif
    return r;
}

/*
 * Reset.
 */
extern void stats_reset(void)
{
    stat_constraints = 0;
    stat_backtracks = 0;
    stat_clauses = 0;
    stat_decisions = 0;
    stat_pivots = 0;
    stat_time = 0;
}

/*
 * Start.
 */
extern void stats_start(void)
{
    stats_reset();
    stat_time = timer();
}

/*
 * Stop.
 */
extern void stats_stop(void)
{
    size_t end_time = timer();
    stat_time = end_time - stat_time;
}

/*
 * Print.
 */
extern void stats_print(void)
{
    size_t time_in_ms = (size_t)round((double)stat_time / (double)1000000);
    message("TIME %zu", time_in_ms);
    message("CONSTRAINTS %zu", stat_constraints);
    message("BACKTRACKS %zu", stat_backtracks);
    message("CLAUSES %zu", stat_clauses);
    message("DECISIONS %zu", stat_decisions);
    message("PIVOTS %zu", stat_pivots);
}

