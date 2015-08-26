/*
 * debug.c
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

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>

#include "debug.h"
#include "log.h"
#include "parse.h"
#include "prompt.h"
#include "solver.h"

#define MAX_SOLVER_NAME         256

/*
 * Debugger commands.
 */
enum command_e
{
    COMMAND_INVALID,
    COMMAND_STEP,
    COMMAND_JUMP,
    COMMAND_GOTO,
    COMMAND_CONTINUE,
    COMMAND_CLEAR,
    COMMAND_BREAK,
    COMMAND_ABORT,
    COMMAND_DUMP,
    COMMAND_HELP,
    COMMAND_QUIT
};
typedef enum command_e command_t;

/*
 * Breakpoints.
 */
typedef struct break_s *break_t;
struct break_s
{
    char *solver;
    break_t next;
};
MAP_DECL(breakinfo, int_t, break_t, int_compare);

/*
 * Debugger state.
 */
size_t debug_state_jump;
size_t debug_state_skip;
size_t debug_state_step;
static breakinfo_t debug_state_breaks;
static history_t debug_history = NULL;

/*
 * Initialize the debugger.
 */
extern void debug_init(void)
{
    if (!option_debug_on)
        return;
    debug_state_jump = 0;
    debug_state_skip = 0;
    debug_state_step = 0;
    debug_state_breaks = breakinfo_init();

    if (!gc_root(&debug_state_breaks, sizeof(debug_state_breaks)))
        panic("failed to add GC root for debugger breakpoints");

    message("***************************************************************"
        "*****************");
    message("[!rDEBUG MODE ENABLED!d]");
    message("Type `?' for help.");
    message("***************************************************************"
        "*****************");
    message("");
}

/*
 * Print the help message.
 */
static void debug_print_help(void)
{
    message("***************************************************************"
        "*****************");
    message("");
    message("PROMPT FORMAT:");
    message("\t!c[S=!ySTEP!c,L=!yLEVEL!c] !yPORT!c: !yCLAUSE!c> ");
    message("\t\twhere");
    message("\t!ySTEP!d   = current step number");
    message("\t!yLEVEL!d  = current decision level");
    message("\t!yPORT!d   = debugger port (see below)");
    message("\t!yCLAUSE!d = relevant clause");
    message("");
    message("PORT:");
    message("\t!yPROPAGATE [T]!d   = theory (T) propagation (lazy clause)");
    message("\t!yPROPAGATE [SAT]!d = SAT propagation (existing clause)");
    message("\t!yFAIL [T]!d        = theory conflict (lazy clause)");
    message("\t!yFAIL [S]!d        = SAT failure (existing clause)");
    message("\t!yLEARN!d           = nogood (learnt clause)");
    message("\t!ySELECT!d          = literal selection (search)");
    message("");
    message("COMMANDS:");
    message("\t!ystep!d, !ys!d, !y<enter>!d = move forward one step");
    message("\t!ystep N!d, !ys N!d      = move forward N steps");
    message("\t!yjump N!d, !yj N!d      = jump forward N steps");
    message("\t!ygoto N!d, !yg N!d      = goto step N");
    message("\t!ycontinue!d, !yc!d      = continue");
    message("\t!ybreak!d, !yb!d         = clear all breakpoints");
    message("\t!ybreak B!y, !yb B!d     = set a breakpoint at B (solver:line)");
    message("\t!yabort!d, !ya!d         = abort");
    message("\t!ydump!d, !yd!d          = dump current state");
    message("\t!yhelp!d, !yh!d, !y?!d       = print this help message");
    message("\t!yquit!d, !yq!d          = quit SMCHR");
    message("");
    message("***************************************************************"
        "*****************");
}

/*
 * Print a literal.
 */
static void debug_show_lit(literal_t lit)
{
    if (lit < 0)
    {
        message_0("not ");
        lit = -lit;
    }
    cons_t c = sat_get_constraint((bvar_t)lit);
    if (c == NULL)
    {
        var_t var = sat_get_var((bvar_t)lit);
        message_0("!y%s!d", var->name);
    }
    else
        message_0("!y%s!d", show_cons(c));
}

/*
 * Print an inference.
 */
static void debug_show_impl(literal_t *lits, size_t len)
{
    ssize_t i = len-1;
    bool prev = false;
    for (; i >= 0; i--)
    {
        literal_t lit = -lits[i];
        if (literal_isfree(lit))
            break;
        if (prev)
            message_0(" /\\ ");
        prev = true;
        debug_show_lit(lit);
    }
    if (i == len-1)
        message_0("true");
    message_0(" ==> ");
    if (i < 0)
        message_0("false");
    prev = false;
    for (; i >= 0; i--)
    {
        literal_t lit = lits[i];
        if (prev)
            message_0(" \\/ ");
        prev = true;
        debug_show_lit(lit);
    }
}

/*
 * Get a number.
 */
static bool debug_get_num(const char *str, size_t *n)
{
    for (size_t i = 0; str[i]; i++)
    {
        if (!isdigit(str[i]))
            return false;
    }

    *n = strtoull(str, NULL, 10);
    return true;
}

/*
 * Parse a command.
 */
static command_t debug_parse_command(const char *line, size_t *n, char *solver)
{
    while (isspace(line[0]))
        line++;
    switch (line[0])
    {
        case '\0':
            *n = 1;
            return COMMAND_STEP;
        case 's':
            if (line[1] == 't' && line[2] == 'e' && line[3] == 'p')
                line += 4;
            else if (line[1] == '\0' || isspace(line[1]))
                line += 1;
            else
                return COMMAND_INVALID;
            while (isspace(line[0]))
                line++;
            if (line[0] == '\0')
            {
                *n = 1;
                return COMMAND_STEP;
            }
            if (!debug_get_num(line, n))
                return COMMAND_INVALID;
            return COMMAND_STEP;
        case 'j':
            if (line[1] == 'u' && line[2] == 'm' && line[3] == 'p')
                line += 4;
            else if (line[1] == '\0' || isspace(line[1]))
                line += 1;
            else
                return COMMAND_INVALID;
            while (isspace(line[0]))
                line++;
            if (line[0] == '\0')
            {
                *n = 1;
                return COMMAND_JUMP;
            }
            if (!debug_get_num(line, n))
                return COMMAND_INVALID;
            return COMMAND_JUMP;
        case 'g':
            if (line[1] == 'o' && line[2] == 't' && line[3] == 'o')
                line += 4;
            else if (line[1] == '\0' || isspace(line[1]))
                line += 1;
            else
                return COMMAND_INVALID;
            while (isspace(line[0]))
                line++;
            if (!debug_get_num(line, n))
                return COMMAND_INVALID;
            return COMMAND_GOTO;
        case 'a':
            if (line[1] == 'b' && line[2] == 'o' && line[3] == 'r' &&
                    line[4] == 't')
                line += 5;
            else if (line[1] == '\0')
                line += 1;
            else
                return COMMAND_INVALID;
            if (line[0] != '\0')
                return COMMAND_INVALID;
            return COMMAND_ABORT;
        case 'd':
            if (line[1] == 'u' && line[2] == 'm' && line[3] == 'p')
                line += 4;
            else if (line[1] == '\0')
                line += 1;
            else
                return COMMAND_INVALID;
            if (line[0] != '\0')
                return COMMAND_INVALID;
            return COMMAND_DUMP;
        case '?':
            if (line[1] == '\0')
                return COMMAND_HELP;
            else
                return COMMAND_INVALID;
        case 'h':
            if (line[1] == 'e' && line[2] == 'l' && line[3] == 'p')
                line += 4;
            else if (line[1] == '\0')
                line += 1;
            else
                return COMMAND_INVALID;
            if (line[1] != '\0')
                return COMMAND_INVALID;
            return COMMAND_HELP;
        case 'c':
            if (line[1] == 'o' && line[2] == 'n' && line[3] == 't' &&
                    line[4] == 'i' && line[5] == 'n' && line[6] == 'u' &&
                    line[7] == 'e')
                line += 8;
            else if (line[1] == '\0')
                line += 1;
            else
                return COMMAND_INVALID;
            if (line[0] != '\0')
                return COMMAND_INVALID;
            return COMMAND_CONTINUE;
        case 'q':
            if (line[1] == 'u' && line[2] == 'i' && line[3] == 't')
                line += 4;
            else if (line[1] == '\0')
                line += 1;
            else
                return COMMAND_INVALID;
            if (line[0] != '\0')
                return COMMAND_INVALID;
            return COMMAND_QUIT;
        case 'b':
        {
            if (line[1] == 'r' && line[2] == 'e' && line[3] == 'a' &&
                    line[4] == 'k')
                line += 5;
            else if (line[1] == '\0' || isspace(line[1]))
                line += 1;
            else
                return COMMAND_INVALID;
            if (line[0] == '\0')
                return COMMAND_CLEAR;
            while (isspace(line[0]))
                line++;
            for (size_t i = 0; !isspace(line[0]) && line[0] != ':'; i++)
            {
                if (i >= MAX_SOLVER_NAME)
                    return COMMAND_INVALID;
                solver[i] = line[0];
                line++;
            }
            if (line[0] != ':')
                return COMMAND_INVALID;
            line++;
            if (!isdigit(line[0]))
                return COMMAND_INVALID;
            size_t i;
            for (i = 1; isdigit(line[i]); i++)
                ;
            if (line[i] != '\0')
                return COMMAND_INVALID;
            size_t lineno = strtoul(line, NULL, 10);
            if (lineno == 0)
                return COMMAND_INVALID;
            *n = lineno;
            return COMMAND_BREAK;
        }
        default:
            return COMMAND_INVALID;
    }
}

/*
 * Get the (short) solver name.
 */
static void debug_get_solver_name(char *buf, size_t len, const char *solver)
{
    if (solver == NULL)
    {
        buf[0] = '\0';
        return;
    }
    bool is_chr = true;
    if (solver[0] == 's' && solver[1] == 'o' && solver[2] == 'l' &&
        solver[3] == 'v' && solver[4] == 'e' && solver[5] == 'r' &&
        solver[6] == '_')
    {
        is_chr = false;
        solver += 7;
    }
    size_t i;
    for (i = 0; i < len-1 && (is_chr || solver[i] != '.') && solver[i] != '\0';
            i++)
        buf[i] = solver[i];
    buf[i] = '\0';
}

/*
 * Should be break?
 */
static bool debug_break(const char *solver, size_t lineno)
{
    break_t breaks;
    if (!breakinfo_search(debug_state_breaks, lineno, &breaks))
        return false;
    char buf[MAX_SOLVER_NAME+1];
    debug_get_solver_name(buf, sizeof(buf), solver);
    while (breaks != NULL)
    {
        if (strcmp(buf, breaks->solver) == 0)
            return true;
        breaks = breaks->next;
    }
    return false;
}

/*
 * Handle a debug step.
 */
extern void debug_step_0(port_t port, bool lazy, literal_t *lits, size_t len,
    const char *solver, size_t lineno)
{
    // Skip the select of "TRUE"
    if (debug_state_step == 0)
        return;

debug_step_start: {}

    bool should_break = false;
    if (debug_state_jump > debug_state_step)
    {
        if (!debug_break(solver, lineno))
            return;
        should_break = true;
    }

    message_0("[!cS=%u,L=%u!d] ", debug_state_step, sat_level());

    switch (port)
    {
        case DEBUG_FAIL:
            if (lazy)
            {
                char buf[MAX_SOLVER_NAME+1];
                debug_get_solver_name(buf, sizeof(buf), solver);
                message_0("!lrFAIL [%s:%zu]!d: ", buf, lineno);
            }
            else
                message_0("!rFAIL [SAT]!d: ");
            debug_show_impl(lits, len);
            break;
        case DEBUG_PROPAGATE:
            if (lazy)
            {
                char buf[MAX_SOLVER_NAME+1];
                debug_get_solver_name(buf, sizeof(buf), solver);
                message_0("!lgPROPAGATE [%s:%zu]!d: ", buf, lineno);
            }
            else
                message_0("!gPROPAGATE [SAT]!d: ");
            debug_show_impl(lits, len);
            break;
        case DEBUG_LEARN:
            message_0("!mLEARN!d: ");
            debug_show_impl(lits, len);
            break;
        case DEBUG_SELECT:
            message_0("!cSELECT!d: ");
            debug_show_lit(lits[0]);
            break;
        default:
            panic("unknown debug port (%d)", port);
    }

    if (!should_break && debug_state_skip > debug_state_step)
    {
        if (!debug_break(solver, lineno))
        {
            message("");
            return;
        }
        should_break = true;
    }

    char *line = prompt(false, stdin, &debug_history);
    if (line == NULL)
        solver_abort();
    size_t n;
    char break_solver[MAX_SOLVER_NAME+1];
    switch (debug_parse_command(line, &n, break_solver))
    {
        case COMMAND_STEP:
            debug_state_skip = debug_state_step + n;
            return;
        case COMMAND_JUMP:
            debug_state_jump = debug_state_step + n;
            return;
        case COMMAND_GOTO:
            if (debug_state_step >= n)
            {
                error("DEBUG: cannot jump backwards to step %zu", n);
                goto debug_step_start;
            }
            debug_state_jump = n;
            return;
        case COMMAND_CONTINUE:
            debug_state_jump = SIZE_MAX;
            return;
        case COMMAND_CLEAR:
            debug_state_breaks = breakinfo_init();
            goto debug_step_start;
        case COMMAND_BREAK:
        {
            break_t br = (break_t)gc_malloc(sizeof(struct break_s));
            br->solver = gc_strdup(break_solver);
            br->next   = NULL;
            break_t breaks;
            if (breakinfo_search(debug_state_breaks, n, &breaks))
            {
                bool found = false;
                for (break_t bi = breaks; !found && bi != NULL; bi = bi->next)
                {
                    if (strcmp(bi->solver, br->solver) == 0)
                        found = true;
                }
                if (!found)
                    br->next = breaks;
                else
                    br = breaks;
            }
            debug_state_breaks = breakinfo_destructive_insert(
                debug_state_breaks, n, br);
            message("DEBUG: set break point at %s:%zu", break_solver, n);
            goto debug_step_start;
        }
        case COMMAND_ABORT:
            solver_abort();
        case COMMAND_DUMP:
        {
            term_t t = sat_result();
            message("%s", show(t));
            goto debug_step_start;
        }
        case COMMAND_HELP:
            debug_print_help();
            goto debug_step_start;
        case COMMAND_QUIT:
            exit(EXIT_SUCCESS);
        default:
            error("failed to parse debugger command `!y%s!d'", line);
            goto debug_step_start;
    }
}

