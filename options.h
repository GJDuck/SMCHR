/*
 * options.h
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
#ifndef __OPTIONS_H
#define __OPTIONS_H

#include <stdbool.h>

/*
 * Default values.
 */
#define OPTION_DEBUG_DEFAULT        false
#define OPTION_SCRIPT_DEFAULT       false
#define OPTION_SILENT_DEFAULT       false
#define OPTION_VERBOSITY_DEFAULT    9

/*
 * Various options.
 */
extern bool option_debug;
extern bool option_debug_on;
extern bool option_script;
extern bool option_eq;
extern bool option_silent;
extern int  option_verbosity;

#endif      /* __OPTIONS_H */
