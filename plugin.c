/*
 * plugin.c
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
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "misc.h"
#include "plugin.h"

#ifndef WINDOWS
#include <dlfcn.h>
#endif      /* WINDOWS */

#ifdef LINUX
#define PLUGIN_EXT  ".so"
#endif      /* LINUX */

#ifdef MACOSX
#define PLUGIN_EXT  ".dylib"
#endif      /* MACOSX */

#ifdef WINDOWS
#define PLUGIN_EXT  ".dll"
#endif      /* WINDOWS */

/*
 * Load a solver plugin.
 */
extern solver_t plugin_load(const char *name)
{
#ifndef WINDOWS
    char sym[100];
    int r = snprintf(sym, sizeof(sym)-1, "solver_%s", name);
    if (r < 0 || r >= sizeof(sym)-1)
    {
        warning("failed to construct symbol name for solver \"%s\"", name);
        return NULL;
    }

    char filename[100];
    r = snprintf(filename, sizeof(sym)-1, "./lib%s%s", name, PLUGIN_EXT);
    if (r < 0 || r >= sizeof(filename)-1)
    {
        warning("failed to construct filename for solver \"%s\"", name);
        return NULL;
    }

    void *handle = dlopen(filename, RTLD_LOCAL | RTLD_NOW);
    if (handle == NULL)
    {
        if (errno == ENOENT)
            return NULL;
        error("failed to open solver plugin \"%s\": %s", filename,
            dlerror());
        return NULL;
    }

    solver_t solver = *(solver_t *)dlsym(handle, sym);
    if (solver == NULL)
    {
        error("failed to load symbol \"%s\" from solver plugin \"%s\": %s",
            sym, filename, dlerror());
        dlclose(handle);
        return NULL;
    }

    if (strcmp(name, solver->name) != 0)
        warning("file \"%s\" contains solver `%s' instead of `%s'",
            filename, solver->name, name);
    
    return solver;

#else
    return NULL;
#endif
}

