/*
 * misc.c
 * Copyright (C) 2013 National University of Singapore
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
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "misc.h"

#ifdef WINDOWS
#include <windows.h>
#else       /* WINDOWS */
#include <sys/mman.h>
#endif      /* WINDOWS */

#ifdef LINUX
#include <sys/resource.h>
#endif      /* LINUX */

/*
 * Word comparison.
 */
extern int_t word_compare(word_t a, word_t b)
{
    return (int_t)a - (int_t)b;
}

/*
 * Int comparison.
 */
extern int_t int_compare(int_t a, int_t b)
{
    return a - b;
}

/*
 * strcmp() wrapper.
 */
extern int_t strcmp_compare(const char *a, const char *b)
{
    return (int_t)strcmp(a, b);
}

/*
 * Greatest Common Divisor.
 */
extern int64_t gcd(int64_t x0, int64_t y0)
{
    if (x0 == 1 || y0 == 1)
        return 1;
    if (x0 == 0)
        return y0;
    if (y0 == 0)
        return x0;
    uint64_t x = llabs(x0);
    uint64_t y = llabs(y0);
    if (y < x)
    {
        uint64_t t = x;
        x = y;
        y = t;
    }
    while (x != 0)
    {
        uint64_t t = x;
        if (y < 4*x)
        {
            // Division (inc. %) is very slow -- try to avoid it:
            uint64_t r = y;
            while (r >= x)
                r -= x;
            x = r;
        }
        else
            x = y % x;
        y = t;
    }
    return y;
}

/*
 * Allocate a large buffer.
 */
extern void *buffer_alloc(size_t size)
{
    void *buf;
#if defined WINDOWS
    buf = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_READWRITE);
    if (buf == NULL)
        panic("failed to allocate %zu bytes for buffer", size);
#elif defined MACOSX
    buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (buf == MAP_FAILED)
        panic("failed to allocate %zu bytes for buffer: %s", size,
            strerror(errno));
#else       /* MACOSX */
    buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (buf == MAP_FAILED)
        panic("failed to allocate %zu bytes for buffer: %s", size,
            strerror(errno));
#endif      /* MACOSX */
    return buf;
}

/*
 * Free parts of a buffer.
 */
extern void buffer_free(void *buf, size_t size)
{
#if defined LINUX
    madvise(buf, size, MADV_DONTNEED);
#endif

#if !defined LINUX
    memset(buf, 0, size);
#endif

#if defined MACOSX
    madvise(buf, size, MADV_FREE);
#endif
}

#ifdef WINDOWS
/*
 * Windows unhandled exception handler.
 */
static LONG WINAPI unhandled_exception_filter(
    struct _EXCEPTION_POINTERS *info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
    {
        fprintf(stderr, "unhandled exception (code=%u)\n",
            info->ExceptionRecord->ExceptionCode);
        exit(EXIT_FAILURE);
    }

    void *ptr = (void *)info->ExceptionRecord->ExceptionInformation[1];
    if (ptr == NULL ||
        VirtualAlloc(ptr, sizeof(uint64_t), MEM_COMMIT, PAGE_READWRITE) == NULL)
    {
        fprintf(stderr, "memory access violation error (segmentation fault) "
            "at address (%p)\n", ptr);
        exit(EXIT_FAILURE);
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}
#endif

/*
 * OS-dependent init.
 */
extern void os_init(void)
{
#ifdef WINDOWS
    // Note: Windows does not have demand-paging.  We can emulate it by
    //       registering a handler for illegal access exceptions, and manually
    //       committing memory.
    SetUnhandledExceptionFilter(unhandled_exception_filter);
#endif

#ifdef LINUX
    // Max-out the size of the stack.  It is not an error if this fails.
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0)
    {
        limit.rlim_cur = RLIM_INFINITY;
        setrlimit(RLIMIT_STACK, &limit);
    }
#endif
}

