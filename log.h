/*
 * log.h
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
#ifndef __LOG_H
#define __LOG_H

#include <stdarg.h>
#include <stdbool.h>

extern void log_message(bool nl, const char *format, ...);

#ifndef NODEBUG
#define debug(format, ...)                                                  \
    log_message(true, format, ## __VA_ARGS__)
#else
#define debug(format, ...)
#endif

#define message(format, ...)                                                \
    log_message(true, format, ## __VA_ARGS__)
#define message_0(format, ...)                                              \
    log_message(false, format, ## __VA_ARGS__)

#define panic(format, ...)                                                  \
    do {                                                                    \
        log_message(true, "!rPANIC !m(%s:%d)!d: " format, __FILE__,         \
            __LINE__, ## __VA_ARGS__);                                      \
        abort();                                                            \
    } while (false)

#define warning(format, ...)                                                \
    log_message(true, "!rwarning!d: " format, ## __VA_ARGS__)
#define error(format, ...)                                                  \
    log_message(true, "!rerror!d: " format, ## __VA_ARGS__)

#define fatal(format, ...)                                                  \
    do {                                                                    \
        log_message(true, "!rfatal error!d: " format, ## __VA_ARGS__);      \
        abort();                                                            \
    } while (false)

#ifndef NODEBUG
#define check(cond)                                                         \
    do {                                                                    \
        if (!(cond))                                                        \
            panic("check (%s) failed", #cond);                              \
    } while (false)
#else
#define check(cond)
#endif

#endif      /* __LOG_H */
