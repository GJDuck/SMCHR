# Makefile
# (C) 2013, all rights reserved,
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

UNAME=$(shell uname)
ifeq ($(UNAME), Darwin)
    LDFLAGS=-Wl,-stack_size,8000000
else
    LDFLAGS=-Wl,--gc-sections -rdynamic
endif

# CPU=VINTAGE_AMD64
CPU=MODERN_AMD64

CC=gcc
PROG=smchr
VERSION=$(shell cat doc/VERSION)
OBJS_BASE= \
    backend.o \
    cons.o \
    debug.o \
    event.o \
    expr.o \
    gc.o \
    hash.o \
    log.o \
    misc.o \
    names.o \
    op.o \
    options.o \
    parse.o \
    pass_cnf.o \
    pass_flatten.o \
    pass_rewrite.o \
    plugin.o \
    prompt.o \
	prop.o \
    sat.o \
    server.o \
    show.o \
    smchr.o \
    solver.o \
    solver_bounds.o \
    solver_chr.o \
    solver_dom.o \
    solver_eq.o \
    solver_heaps.o \
    solver_linear.o \
    stats.o \
	store.o \
    term.o \
	trail.o \
    tree.o \
    typecheck.o \
    var.o
OBJS= \
    main.o \
    $(OBJS_BASE)

smchr: CFLAGS = -O3 -msse4.1 -maes -D SMCHR -Wall --std=gnu99 -ffast-math \
    -fno-math-errno -DNODEBUG -Wno-unused-function $(LDFLAGS) \
    -fomit-frame-pointer -D VERSION=$(VERSION) -Wno-typedef-redefinition \
    -D $(CPU)
smchr: $(OBJS_BASE) main.o
	$(CC) $(CFLAGS) -o smchr $(OBJS_BASE) main.o -ldl -lm
	strip smchr

libsmchr.a: CFLAGS = -O3 -msse4.1 -maes -D SMCHR -Wall --std=gnu99 \
    -ffast-math -fno-math-errno --save-temps -DNODEBUG -Wno-unused-function \
    -D $(CPU)
libsmchr.a: $(OBJS_BASE)
	ar -cvq libsmchr.a $(OBJS_BASE)

libsmchr.so: CFLAGS = -O3 -msse4.1 -maes -D SMCHR -Wall --std=gnu99 \
    -ffast-math -fno-math-errno --save-temps -DNODEBUG -Wno-unused-function \
    -D $(CPU)
libsmchr.so: $(OBJS_BASE)
	gcc -shared -Wl,-soname,libsmchr.so -o libsmchr.so $(OBJS_BASE)

libsmchr.dylib: CFLAGS = -O3 -msse4.1 -maes -D SMCHR -Wall --std=gnu99 \
    -ffast-math -fno-math-errno --save-temps -DNODEBUG -Wno-unused-function \
    -fPIC -D $(CPU)
libsmchr.dylib: $(OBJS_BASE)
	gcc -dynamiclib -o libsmchr.dylib $(OBJS_BASE)

smchr.debug: CFLAGS = -O0 -msse4.1 -maes -g -D SMCHR -Wall --std=gnu99 \
    --save-temps $(LDFLAGS) -D VERSION=$(VERSION) -D $(CPU)
smchr.debug: $(OBJS_BASE) main.o
	$(CC) $(CFLAGS) -o smchr $(OBJS_BASE) main.o -ldl -lm

smchr.realdebug: CFLAGS = -O0 -msse4.1 -maes -g -D SMCHR -Wall --std=gnu99 \
    --save-temps -DNODEBUG $(LDFLAGS) -D VERSION=$(VERSION) -D $(CPU)
smchr.realdebug: $(OBJS_BASE) main.o
	$(CC) $(CFLAGS) -o smchr $(OBJS_BASE) main.o -ldl -lm

smchr.profile: CFLAGS = -O2 -msse4.1 -maes -pg -D SMCHR -Wall --std=gnu99 \
    --save-temps -DNODEBUG $(LDFLAGS) -D VERSION=$(VERSION) -D $(CPU)
smchr.profile: $(OBJS_BASE) main.o
	$(CC) $(CFLAGS) -o smchr $(OBJS_BASE) main.o -ldl -lm

smchr.windows: CC = x86_64-w64-mingw32-gcc
smchr.windows: CFLAGS = -O3 -msse4.1 -maes -D SMCHR -Wall --std=gnu99 \
    -ffast-math -fno-math-errno --save-temps -D NODEBUG -D VERSION=$(VERSION) \
    -D $(CPU)
smchr.windows: $(OBJS_BASE) main.o
	$(CC) $(CFLAGS) -s -o smchr.exe $(OBJS_BASE) main.o

flatzinc: CFLAGS = -O3 -msse4.1 -maes --std=gnu99 -ffast-math -lm \
    -DNODEBUG -fdata-sections -ffunction-sections $(LDFLAGS) -I $(shell pwd) \
    -D $(CPU)
flatzinc: $(OBJS_BASE) tools/flatzinc.o
	$(CC) $(CFLAGS) -o flatzinc $(OBJS_BASE) tools/flatzinc.o -ldl -lm

answer: CFLAGS = -O3 -msse4.1 -maes --std=gnu99 -ffast-math -lm \
    -DNODEBUG -fdata-sections -ffunction-sections $(LDFLAGS) -I $(shell pwd) \
    -D $(CPU)
answer: $(OBJS_BASE) tools/answer.o
	$(CC) $(CFLAGS) -o answer $(OBJS_BASE) tools/answer.o -ldl -lm

clean:
	rm -rf tools/*.o *.o *.s *.i

