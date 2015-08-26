/*
 * server.c
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

#include "log.h"
#include "misc.h"
#include "server.h"

#ifdef LINUX

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/tcp.h>
#include <sys/types.h> 
#include <sys/socket.h>

#include "parse.h"
#include "smchr.h"
#include "term.h"

/*
 * Run SMCHR in server-mode.
 */
extern void server(uint16_t port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
server_socket_error:
        fatal("failed to create socket on port %u: %s", port,
            strerror(errno));
    }

    int on = 1;
    if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&on, sizeof(on)) < 0 ||
        setsockopt(s, IPPROTO_TCP, TCP_QUICKACK, (char *)&on, sizeof(on)) < 0)
        warning("failed to set TCP socket options: %s", strerror(errno));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (const void *)&addr, sizeof(addr)) < 0)
        goto server_socket_error;
    if (listen(s, 1) < 0)
        goto server_socket_error;

    socklen_t len = sizeof(addr);
    s = accept(s, (struct sockaddr *)&addr, &len);
    if (s < 0)
        fatal("failed to establish connection on port %u: %s", port, 
            strerror(errno));

    FILE *stream = fdopen(s, "r+");
    if (stream == NULL)
        fatal("failed to create stream from socket: %s", strerror(errno));

    size_t linelen = 256;
    char *line = (char *)gc_malloc(linelen);
    while (true)
    {
        char c;
        size_t i;
        for (i = 0; (c = getc(stream)) != '\n' && c != EOF; i++)
        {
            if (i >= linelen)
            {
                linelen *= 2;
                line = (char *)gc_realloc(line, linelen);
            }
            line[i] = c;
        }
        if (c == EOF)
        {
            fclose(stream);
            exit(0);
        }
        line[i] = '\0';

        const char *filename = "<client>";
        size_t lineno = 1; 
        term_t t = parse_term(filename, &lineno, opinfo_init(), line, NULL,
            NULL);
        if (t == (term_t)NULL)
        {
            fprintf(stream, "E parse error\n");
            fflush(stream);
            continue;
        }

        t = smchr_execute(filename, lineno, t);

        if (type(t) == BOOL)
        {
            if (t == TERM_FALSE)
                fprintf(stream, "F\n");
            else
                fprintf(stream, "? true\n");
        }
        else if (type(t) == TERM_NIL)
            fprintf(stream, "E execution aborted\n");
        else
            fprintf(stream, "? %s\n", show(t));
        fflush(stream);
    }
}

#else       /* LINUX */

extern void server(uint16_t port)
{
    fatal("failed to set up server on port %u; this feature is only "
        "on Linux", port);
}

#endif      /* LINUX */

