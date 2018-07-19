#!/bin/sh

set -e

VERSION=`cat doc/VERSION`
INSTALL=smchr-$VERSION

set -x

# (1) Build SMCHR
# Nb: MacOSX version must be built separately.
MAKEFILE=Makefile
make -f $MAKEFILE clean
make -f $MAKEFILE -j4 smchr CPU=VINTAGE_AMD64
make -f $MAKEFILE clean
#make -f $MAKEFILE -j4 smchr.windows
#make -f $MAKEFILE clean
make -f $MAKEFILE answer CPU=VINTAGE_AMD64
make -f $MAKEFILE clean
make -f $MAKEFILE flatzinc CPU=VINTAGE_AMD64
mv answer answer.linux
mv flatzinc flatzinc.linux

# (2) Build the documentation:
(cd doc/; pdflatex smchr.tex; bibtex smchr; pdflatex smchr.tex; \
    pdflatex smchr.tex)

# (3) Copy files:
rm -rf "$INSTALL"
mkdir -p "$INSTALL"
cp smchr "$INSTALL"/smchr.linux
#cp smchr.exe "$INSTALL"
HAVE_MACOSX=yes
if [ -e smchr.macosx ]
then
    cp smchr.macosx "$INSTALL"
else
    HAVE_MACOSX=no
fi
cp -r test/ "$INSTALL"
mkdir -p "$INSTALL"/doc
cp doc/smchr.pdf "$INSTALL"/doc
cp doc/README "$INSTALL"/
cp doc/LICENSE "$INSTALL"/
cp doc/VERSION "$INSTALL"/
cp doc/CHANGELOG "$INSTALL"/
mkdir -p "$INSTALL"/examples/
cp "$INSTALL"/test/chr_array/array.chr \
   "$INSTALL"/test/chr_bool/bool.chr \
   "$INSTALL"/test/chr_bounds/bounds.chr \
   "$INSTALL"/test/chr_domain/domain.chr \
   "$INSTALL"/test/chr_eq/eq.chr \
   "$INSTALL"/test/chr_lt/lt.chr \
   "$INSTALL"/test/chr_leq/leq.chr \
   "$INSTALL"/test/chr_heaps/heaps.chr \
   "$INSTALL"/test/chr_queens/queens.chr \
   "$INSTALL"/test/chr_set/set.chr \
   "$INSTALL"/test/chr_sudoku/sudoku.chr \
   "$INSTALL"/test/chr_gcd/gcd.chr \
        "$INSTALL"/examples/
mkdir -p "$INSTALL"/vim/
cp doc/chr.vim "$INSTALL"/vim/
mkdir -p "$INSTALL"/tools/
cp answer.linux "$INSTALL"/tools/
cp flatzinc.linux "$INSTALL"/tools/

# (4) Create the package:
rm -f "smchr-$VERSION.tar.gz" "smchr-$VERSION.zip"
tar cvz --owner root --group root --exclude=".svn" -f "smchr-$VERSION.tar.gz" \
     "$INSTALL"
zip -r "smchr-$VERSION.zip" "$INSTALL"

# (5) Clean-up
# rm -rf "$INSTALL"

if [ "$HAVE_MACOSX" = "no" ]
then
    echo "warning: missing MacOSX binary" 1>&2
fi

# (6) Create the source package:
INSTALL=smchr-$VERSION-src
rm -rf "$INSTALL"
mkdir -p "$INSTALL"
cp $MAKEFILE "$INSTALL"
cp backend.c backend.h \
   cons.c \
   debug.c debug.h \
   event.c \
   expr.c expr.h \
   gc.c gc.h \
   hash.c hash.h \
   log.c log.h \
   main.c \
   map.h \
   misc.c misc.h \
   names.c names.h \
   op.c op.h \
   options.c options.h \
   parse.c parse.h \
   pass_cnf.c pass_cnf.h \
   pass_flatten.c pass_flatten.h \
   pass_rewrite.c pass_rewrite.h \
   plugin.c plugin.h \
   prompt.c prompt.h \
   prop.c \
   set.h \
   sat.c sat.h \
   server.c server.h \
   show.c show.h \
   smchr.c smchr.h \
   solver.c solver.h \
   solver_bounds.c solver_bounds.h \
   solver_chr.c solver_chr.h \
   solver_dom.c solver_dom.h \
   solver_eq.c solver_eq.h \
   solver_heaps.c solver_heaps.h \
   solver_linear.c solver_linear.h \
   stats.c stats.h \
   store.c \
   term.c term.h \
   trail.c \
   tree.c tree.h \
   typecheck.c typecheck.h \
   var.c \
   word.h "$INSTALL"
mkdir -p "$INSTALL"/tools/
cp tools/flatzinc.c tools/answer.c "$INSTALL"/tools/
mkdir -p "$INSTALL"/doc/
cp doc/LICENSE "$INSTALL"/doc
cp doc/VERSION "$INSTALL"/doc

rm -f "smchr-$VERSION-src.tar.gz"
tar cvz --owner root --group root -f "smchr-$VERSION-src.tar.gz" "$INSTALL"
# rm -rf "$INSTALL"

