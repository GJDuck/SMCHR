/*
 * prompt.c
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

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "log.h"
#include "misc.h"
#include "prompt.h"

#ifndef WINDOWS
#include <dlfcn.h>
#endif      /* WINDOWS */

static bool use_readline = false;
static bool use_state = false;

typedef struct _hist_state HISTORY_STATE;
typedef void (*using_history_t)(void);
typedef void (*add_history_t)(char *);
typedef char *(*readline_t)(const char *);
typedef HISTORY_STATE *(*history_get_history_state_t)(void);
typedef void (*history_set_history_state_t)(HISTORY_STATE *);

static add_history_t add_history;
static readline_t readline;
static history_get_history_state_t history_get_history_state;
static history_set_history_state_t history_set_history_state;
static HISTORY_STATE *empty_state = NULL;

#ifdef LINUX
#define SO_EXT      ".so"
#endif      /* LINUX */

#ifdef MACOSX
#define SO_EXT      ".dylib"
#endif      /* MACOSX */

/*
 * Initialise the prompt.
 */
extern void prompt_init(void)
{
#ifndef WINDOWS
    if (!isatty(fileno(stdin)))
        return;

    void *handle = dlopen("libreadline" SO_EXT, RTLD_LAZY | RTLD_LOCAL);
    if (handle == NULL)
        handle = dlopen("libedit" SO_EXT, RTLD_LAZY | RTLD_LOCAL);
    if (handle == NULL)
        handle = dlopen("libreadline" SO_EXT ".6", RTLD_LAZY | RTLD_LOCAL);
    if (handle == NULL)
        handle = dlopen("libreadline" SO_EXT ".5", RTLD_LAZY | RTLD_LOCAL);
    if (handle == NULL)
    {
        warning("failed to open libreadline (%s); readline functionality "
            "is disabled", strerror(errno));
        return;
    }

    using_history_t using_history =
        (using_history_t)dlsym(handle, "using_history");
    add_history = (add_history_t)dlsym(handle, "add_history");
    readline = (readline_t)dlsym(handle, "readline");
    history_get_history_state = (history_get_history_state_t)dlsym(handle,
        "history_get_history_state");
    history_set_history_state = (history_set_history_state_t)dlsym(handle,
        "history_set_history_state");
    
    if (using_history == NULL || add_history == NULL || readline == NULL)
    {
        dlclose(handle);
        warning("failed to load readline API; readline functionality "
            "disabled");
        return;
    }

    use_readline = true;
    using_history();
    if (history_get_history_state != NULL &&
            history_set_history_state != NULL)
        use_state = true;
    if (use_state)
        empty_state = history_get_history_state();
#endif      /* WINDOWS */
}

/*
 * Read a line from the prompt.
 */
extern char *prompt(bool silent, FILE *input, history_t *state)
{
    const char *prompt = (silent? "": "> ");

    while (true)
    {
#ifndef WINDOWS
        if (use_readline && input == stdin)
        {
            // Set the state:
            if (use_state)
            {
                if (*state != NULL)
                {
                    history_set_history_state((HISTORY_STATE *)*state);
                    free(*state);
                    *state = NULL;
                }
                else
                    history_set_history_state(empty_state);
            }

            // Readline:
            char *line = readline(prompt);

            if (line != NULL)
            {
                bool all_space = true;
                for (size_t i = 0; line[i]; i++)
                {
                    if (!isspace(line[i]))
                    {
                        all_space = false;
                        break;
                    }
                }
                if (!all_space)
                    add_history(line);
            }

            if (use_state)
                *state = (history_t)history_get_history_state();

            return line;
        }
#endif      /* WINDOWS */

        // Non-readline:
        size_t size = 128, len = 0;
        char *line = gc_malloc(size*sizeof(char));
        char c;
        fputs(prompt, stdout);
        while (!feof(input) && !ferror(input) && (c = getc(input)) != '\n')
        {
            if (len >= size-1)
            {
                size = (3 * size) / 2;
                line = (char *)gc_realloc(line, size*sizeof(char));
            }
            line[len++] = c;
        }
        if (feof(input) || ferror(input))
            return NULL;
        line[len] = '\0';
        return line;
    }
}

