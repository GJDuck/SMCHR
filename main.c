/*
 * main.c
 * Copyright (C) 2018 National University of Singapore
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
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
int isatty(int fd);     // Surpress warning.

#include "debug.h"
#include "options.h"
#include "prompt.h"
#include "server.h"
#include "set.h"
#include "show.h"
#include "smchr.h"
#include "stats.h"
#include "term.h"

/*
 * Options.
 */
enum option_e
{
    OPTION_DEBUG = 1000,
    OPTION_HELP,
    OPTION_INPUT,
    OPTION_SCRIPT,
    OPTION_SERVER,
    OPTION_SILENT,
    OPTION_SOLVER,
    OPTION_VERBOSITY,
    OPTION_VERSION,
};
typedef enum option_e option_t;
static const struct option long_options[] =
{
    {"debug", 0, NULL, OPTION_DEBUG},
    {"help", 0, NULL, OPTION_HELP},
    {"input", 1, NULL, OPTION_INPUT},
    {"script", 0, NULL, OPTION_SCRIPT},
    {"server", 1, NULL, OPTION_SERVER},
    {"silent", 0, NULL, OPTION_SILENT},
    {"solver", 1, NULL, OPTION_SOLVER},
    {"version", 0, NULL, OPTION_VERSION},
    {"verbosity", 1, NULL, OPTION_VERBOSITY},
    {NULL, 0, NULL, 0}
};
SET_DECL(solvers, const char *, strcmp_compare);

/*
 * Prototypes.
 */
static void print_usage(FILE *out, const char *progname, bool help);
static void print_version(FILE *out, const char *progname);
static void print_help(FILE *out, const char *progname);
static void print_banner(const char *progname);
static void gc_error_handler(void);
static void interrupt(int sig);
static void show_answer(const char *msg, term_t answer);
static void pretty_print(term_t t, bool more);
static void command(const char *cmd);

/*
 * GC error handler.
 */
static void gc_error_handler(void)
{
    panic("garbage collector failed: %s", strerror(errno));
}

/*
 * Program entry point.
 */
int main(int argc, char **argv)
{
    // OS-dep init.
    os_init();

    // Initialise the GC.
    if (!gc_init())
        panic("failed to initialize the garbage collector: %s",
            strerror(errno));
    gc_error(gc_error_handler);

    // Process options:
    solvers_t ss = solvers_init();
    option_silent = !isatty(fileno(stdout));
    uint16_t port = 0;
    const char *input_filename = NULL;
    while (true)
    {
        int idx;
        int option = getopt_long(argc, argv, "ds:v:", long_options, &idx);
        if (option < 0)
            break;
        switch (option)
        {
            case OPTION_DEBUG: case 'd':
                option_debug = true;
                break;
            case OPTION_HELP:
                print_help(stdout, argv[0]);
                return EXIT_SUCCESS;
            case OPTION_INPUT:
                input_filename = gc_strdup(optarg);
                break;
            case OPTION_SERVER:
            {
                char *end;
                size_t port0 = strtoul(optarg, &end, 10);
                if (port0 == 0 || port0 > UINT16_MAX ||
                        (end != NULL && end[0] != '\0'))
                    fatal("failed to parse port number for `--server' option; "
                        "expected a valid port number 1-65535, found \"%s\"",
                        optarg);
                port = (uint16_t)port0;
            }
            case OPTION_SCRIPT:
                option_script = true;
                break;
            case OPTION_SILENT:
                option_silent = true;
                break;
            case OPTION_SOLVER: case 's':
            {
                for (char *name = strtok(optarg, ","); name != NULL;
                        name = strtok(NULL, ","))
                {
                    if (strcmp(name, "sat") == 0)
                        continue;
                    ss = solvers_insert(ss, name);
                    if (strcmp(name, "eq") == 0)
                        option_eq = true;
                }
                break;
            }
            case OPTION_VERBOSITY: case 'v':
            {
                if (!isdigit(optarg[0]) || optarg[1] != '\0')
                    fatal("expected verbosity level 0-9, found \"%s\"",
                        optarg);
                option_verbosity = (int)optarg[0] - (int)'0';
                break;
            }
            case OPTION_VERSION:
                print_version(stdout, argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(stderr, argv[0], true);
                return EXIT_FAILURE;
        }
    }

    if (optind < argc)
    {
        print_usage(stderr, argv[0], true);
        return EXIT_FAILURE;
    }

    if (!option_silent)
        print_banner(argv[0]);

#ifndef NODEBUG
    warning("executable %s has been compiled in !yDEBUG!d mode\n\n", argv[0]);
#endif

    // Initialise stuff:
    smchr_init();
    if (port == 0)
        prompt_init();
    if (!option_silent)
        message("!yLOAD!d solver \"!gsat!d\"");
    if (port == 0)
        signal(SIGINT, interrupt);

    solversitr_t itr = (solversitr_t)gc_malloc(solvers_itrsize(ss));
    const char *name;
    for (solversitr_t i = solvers_itrinit(itr, ss); solvers_get(i, &name);
        solvers_next(i))
    {
        if (!option_silent)
            message("!yLOAD!d solver \"!g%s!d\"", name);
        if (!smchr_load(name))
            exit(EXIT_FAILURE);
    }
    if (!option_silent)
        message("");

    // Enter server-mode if need be:
    if (port != 0)
    {
        server(port);
        return 0;
    }

    // Open input file if need be:
    FILE *input = stdin;
    if (input_filename != NULL)
    {
        input = fopen(input_filename, "r");
        if (input == NULL)
            fatal("unable to open file \"%s\": %s", input_filename,
                strerror(errno));
    }

    // Main loop:
    const char *filename = (input == stdin? "<stdin>": input_filename);
    history_t history = NULL;
    int exit_code = EXIT_SUCCESS;
    while (true)
    {
        // (1) Read the goal:
        char *line = prompt(option_silent, input, &history);
        if (line == NULL)
            break;
        if (line[0] == '\0')
            continue;
        if (line[0] == ':')
        {
            command(line + 1);
            continue;
        }
        size_t lineno = 1;
        const char *err_str;
        term_t t = parse_term(filename, &lineno, opinfo_init(), line, &err_str,
            NULL);
        if (t == (term_t)NULL)
        {
            size_t cxt = 64;
            char pre_err[cxt+1];
            size_t offset = err_str - line, j = 0;
            for (ssize_t i = (offset > cxt? offset - cxt: 0); i < offset; i++)
                pre_err[j++] = line[i];
            pre_err[j++] = '\0';
            error("(%s: %zu) failed to parse goal; error is \"%s!y%s!d\" "
                "<--- here ---> \"!y%.64s!d%s\"", filename, lineno,
                (offset > cxt? "...": ""), pre_err, err_str,
                (strlen(err_str) > cxt? "...": ""));
            if (option_script)
                exit_code = EXIT_FAILURE;
            continue;
        }

        // (2) Execute the goal:
        t = smchr_execute(filename, lineno, t);

        // (3) Interpret the result:
        if (option_script)
        {
            if (type(t) != BOOL || t == TERM_TRUE) 
                exit_code = EXIT_FAILURE;
        }

        if (type(t) == BOOL)
        {
            if (t == TERM_FALSE)
                message("!rUNSAT!d");
            else
                show_answer("!gUNKNOWN!d", t);
        }
        else if (type(t) == NIL)
            message("!yABORT!d");
        else
            show_answer("!gUNKNOWN!d", t);

        stats_print();
    }

    return exit_code;
}

/*
 * Pretty print an answer.
 */
static void show_answer(const char *msg, term_t answer)
{
    if (option_verbosity < 1)
    {
        message(msg);
        return;
    }

    if (option_silent)
    {
        message_0(msg);
        message(" %s", show(answer));
        return;
    }

    message_0(msg);
    message(":");

    pretty_print(answer, false);
}

/*
 * Pretty print an answer.
 */
static void pretty_print(term_t t, bool more)
{
    while (true)
    {
        if (type(t) != FUNC)
        {
            if (more)
                message("\t%s /\\", show(t));
            else
                message("\t%s", show(t));
            break;
        }
        func_t f = func(t);
        if (f->atom == ATOM_AND)
        {
            term_t arg1 = f->args[0];
            term_t arg2 = f->args[1];
            if (type(arg1) == FUNC)
            {
                f = func(arg1);
                if (f->atom == ATOM_AND)
                {
                    pretty_print(arg1, true);
                    t = arg2;
                    continue;
                }
            }
            message("\t%s /\\", show(arg1));
            t = arg2;
        }
        else
        {
            if (more)
                message("\t%s /\\", show(t));
            else
                message("\t%s", show(t));
            break;
        }
    }
}

/*
 * Print the banner.
 */
static void print_banner(const char *progname)
{
    message("!m               !r ____ _   _ ____");
    message("!m ___ _ __ ___  !r/ ___| | | |  _ \\");
    message("!m/ __| '_ ` _ \\!r| |   | |_| | |_) |");
    message("!m\\__ \\ | | | |!r | |___|  _  |  _ <");
    message("!m|___/_| |_| |_|!r\\____|_| |_|_| \\_\\!d [Version "
        STRING(VERSION) "]");
    message("(C) 2018, all rights reserved.");
    message("Try `%s --help' or `:help' for more information.\n\n", progname);
}

/*
 * Print the usage message.
 */
static void print_usage(FILE *out, const char *progname, bool help)
{
    fprintf(out, "usage: %s [OPTIONS]\n", progname);
    if (help)
        fprintf(stderr, "Try `%s --help' for more information.\n", progname);
}

/*
 * Print version information.
 */
static void print_version(FILE *out, const char *progname)
{
    fprintf(out, "%s version " STRING(VERSION) "\n", progname);
    fputs("Copyright (C) 2014 National University of Singapore\n", out);
    fputs("This is free software; see the source for copying conditions.  "
        "There is NO\n", out);
    fputs("warranty; not even for MERCHANTABILITY or FITNESS FOR A "
        "PARTICULAR PURPOSE.\n", out);
}

/*
 * Print the help message.
 */
static void print_help(FILE *out, const char *progname)
{
    print_usage(out, progname, false);
    putc('\n', out);
    fputs("OPTIONS:\n", out);
    fputs("\t--debug, -d\n", out);
    fputs("\t\tEnable solver debugging mode.\n", out);
    fputs("\t--help\n", out);
    fputs("\t\tPrints this helpful message and exits.\n", out);
    fputs("\t--input FILE\n", out);
    fputs("\t\tUse FILE instead of stdin as input.\n", out);
    fputs("\t--script\n", out);
    fputs("\t\tEnter script-mode.  The exit code will be 0 only if all "
        "goals\n", out);
    fputs("\t\tare UNSAT.\n", out);
    fputs("\t--server PORT\n", out);
    fputs("\t\t[LINUX ONLY] Enter server-mode (listening on PORT).\n", out);
    fputs("\t--silent\n", out);
    fputs("\t\tSurpress printing banners and command prompts.\n", out);
    fputs("\t--solver SOLVER, -s SOLVER\n", out);
    fputs("\t\tEnable the solver named SOLVER.  See below for a list.\n", out);
    fputs("\t--verbosity N, -v N\n", out);
    fprintf(out, "\t\tSets the verbosity level (0-9, default %d).\n",
        OPTION_VERBOSITY_DEFAULT);
    fputs("\t--version\n", out);
    fputs("\t\tPrints version information and exits.\n", out);
    putc('\n', out);
    fputs("SOLVERS:\n", out);
    fputs("\t<file>.chr - Any CHR solver (read from file).\n", out);
    fputs("\tbounds - Integer bounds propagation solver.\n", out);
    fputs("\tdom - Lazy Clause Generation integer domain support.\n", out);
    fputs("\teq - unification based (dis)equality solver.\n", out);
    fputs("\theap - Separation Logic style heap constraint solver.\n", out);
    fputs("\tlinear - Simplex-based integer linear arithmetic solver.\n", out);
    fputs("\tsat - The underlying SAT solver (always loaded).\n", out);
    putc('\n', out);
}

/*
 * Interrupt handler.
 */
static void interrupt(int sig)
{
    debug_enable();
}

/*
 * Command interpreter.
 */
static void command(const char *cmd)
{
    switch (cmd[0])
    {
        case 'd':
            if (cmd[1] == '\0' || strcmp(cmd, "debug") == 0)
            {
                message("[DEBUG=on]");
                option_debug = true;
                return;
            }
            break;
        case 'q':
            if (cmd[1] == '\0' || strcmp(cmd, "quit") == 0)
            {
                message("[QUIT]");
                exit(EXIT_SUCCESS);
            }
            break;
        case 'h':
            if (cmd[1] == '\0' || strcmp(cmd, "help") == 0)
            {
                message("\nCOMMANDS:");
                message("\t:d, :debug");
                message("\t\tEnter DEBUG mode.");
                message("\t:h, :help");
                message("\t\tPrint this helpful message.");
                message("\t:n, :nodebug");
                message("\t\tDisable DEBUG mode.");
                message("\t:q, :quit");
                message("\t\tQuit SMCHR.");
                message("");
                return;
            }
        case 'n':
            if (cmd[1] == '\0' || strcmp(cmd, "nodebug") == 0)
            {
                message("[DEBUG=off]");
                option_debug = false;
                return;
            }
            break;
        default:
            break;
    }

    error("invalid command \"!y%s!d\"", cmd);
}

