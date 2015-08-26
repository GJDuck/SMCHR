/*
 * log.c
 * Copyright (C) 2012 National University of Singapore
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

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "misc.h"

#define COLOR_ESCAPE            '\033'

#ifdef WINDOWS

#include <windows.h>

static HANDLE console = NULL;

/*
 * Set color (Windows).
 */
static void set_color(bool light, char color)
{
    if (console == NULL)
        console = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD attrs = 0;
    switch (color)
    {
        case 'd':
            SetConsoleTextAttribute(console,
                FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            return;
        case 'r':
            attrs |= FOREGROUND_RED;
            break;
        case 'g':
            attrs |= FOREGROUND_GREEN;
            break;
        case 'b': 
            attrs |= FOREGROUND_BLUE;
            break;
        case 'y':
            attrs |= FOREGROUND_RED | FOREGROUND_GREEN;
            break;
        case 'm':
            attrs |= FOREGROUND_RED | FOREGROUND_BLUE;
            break;
        case 'c':
            attrs |= FOREGROUND_GREEN | FOREGROUND_BLUE;
            break;
    }
    if (light)
        attrs |= FOREGROUND_INTENSITY;
    SetConsoleTextAttribute(console, attrs);
}

#else       /* WINDOWS */

/*
 * Set color (non-Windows).
 */
static void set_color(bool light, char color)
{
    switch (color)
    {
        case 'd':
            fputs("\33[0m", stdout);
            break;
        case 'r':
            fputs((light? "\33[1;31m": "\33[31m"), stdout);
            break;
        case 'g':
            fputs((light? "\33[1;32m": "\33[32m"), stdout);
            break;
        case 'b':
            fputs((light? "\33[1;34m": "\33[34m"), stdout);
            break;
        case 'y':
            fputs((light? "\33[1;33m": "\33[33m"), stdout);
            break;
        case 'm':
            fputs((light? "\33[1;35m": "\33[35m"), stdout);
            break;
        case 'c':
            fputs((light? "\33[1;36m": "\33[36m"), stdout);
            break;
    }
}

#endif      /* WINDOWS */

/*
 * Check if character corresponds to a color.
 */
static bool is_color(char c)
{
    switch (c)
    {
        case 'd': case 'r': case 'g': case 'b': case 'y': case 'm': case 'c':
            return true;
        default:
            return false;
    }
}

/*
 * Print a log message.
 */
extern void log_message(bool nl, const char *format, ...)
{
    size_t len = strlen(format);
    char format_1[2*len];

    size_t j = 0;
    for (size_t i = 0; i < len; i++)
    {
        format_1[j] = format[i]; 
        if (format[i] == '!' && is_color(format[i+1]))
        {
            format_1[j++] = COLOR_ESCAPE;
            format_1[j++] = format[i+1];
            i += 1;
            continue;
        }
        if (format[i] == '!' && format[i+1] == 'l' && is_color(format[i+2]))
        {
            format_1[j++] = COLOR_ESCAPE;
            format_1[j++] = format[i+1];
            format_1[j++] = format[i+2];
            i += 2;
            continue;
        }
#ifdef WINDOWS
        if (format[i] == '%' && format[i+1] == 'z' && format[i+2] == 'u')
        {
            format_1[j++] = '%';
            format_1[j++] = 'l';
            format_1[j++] = 'l';
            format_1[j++] = 'u';
            i += 2;
            continue;
        }
#endif      /* WINDOWS */
        j++;
    }
    format_1[j] = '\0';

    va_list args, args_cpy;
    va_start(args, format);
    va_copy(args_cpy, args);
    int r = vsnprintf(NULL, 0, format_1, args);
    if (r < 0)
    {
        fprintf(stderr, "error: failed to print message\n");
        return;
    }
    char message[r+1];
    vsnprintf(message, sizeof(message), format_1, args_cpy);
    va_end(args);
    va_end(args_cpy);

    bool terminal = isatty(fileno(stdout));
    char c = '\0';
    for (size_t i = 0; message[i] != '\0'; i++)
    {
        c = message[i];
        if (c == COLOR_ESCAPE)
        {
            i++;
            bool light = false;
            switch (message[i])
            {
                case 'l':
                    i++;
                    light = true;
                    // Fallthru:
                default:
                    if (terminal)
                        set_color(light, message[i]);
                    break;
            }
            if (message[i] == '\0')
                break;
        }
        else
            putchar(message[i]);
    }
    if (nl && c != '\n')
        putchar('\n');

    if (terminal)
        set_color(false, 'd');
}

