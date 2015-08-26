/*
 * options.c
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

#include "options.h"

/*
 * Various options.
 */
bool option_debug = OPTION_DEBUG_DEFAULT;
bool option_debug_on = false;
bool option_eq = false;
bool option_script = OPTION_SCRIPT_DEFAULT;
bool option_silent = OPTION_SILENT_DEFAULT;
int option_verbosity = OPTION_VERBOSITY_DEFAULT;

