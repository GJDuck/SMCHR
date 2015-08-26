#!/bin/bash
#
# regtest.sh
# Copyright (C) 2012 National University of Singapore
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

MAXTIME=5

UNAME=`uname`
EXEC=`pwd`/../smchr

if [ ! -x $EXEC ]
then
    case "$UNAME" in
        Linux)
            EXEC=`pwd`/../smchr.linux
            ;;
        Darwin)
            EXEC=`pwd`/../smchr.macosx
            ;;
        *)
            EXEC=`pwd`/../smchr.exe
            ;;
    esac
    if [ ! -x $EXEC ]
    then
        echo "error: unable to find the smchr executable"
        exit 1;
    fi
fi

exec 2> /dev/null
trap "true" SIGSEGV

PASSED=0
FAILED=0
UNKNOWN=0

if [ "$UNAME" != "Darwin" ]
then
    TIMEOUT="timeout $MAXTIME"
else
    TIMEOUT=
fi

if [ $# = 0 ]
then
    DIRS=*
else
    DIRS=$@
fi

echo

for DIR in $DIRS
do
    if [ ! -d $DIR ]
    then
        continue
    fi
    cd $DIR
    if [ -e options ]
    then
        OPTIONS=`head -n 1 options`
    else
        OPTIONS=
    fi

    for TEST in *.in
    do
        if [ "$TEST" = '*.in' ]
        then
            break
        fi
        TESTNAME=`basename $TEST .in`
        if [ "$DIR" = "." ]
        then
            TESTPATH=$TESTNAME
        else
            TESTPATH=$DIR/$TESTNAME
        fi
        EXTRA_OPTS=
        if [ -e $TESTNAME.opt ]
        then
            EXTRA_OPTS=`cat $TESTNAME.opt`
        fi
        echo -e -n "\033[33mTEST\033[0m $TESTPATH"
        LENGTH=`expr 40 - length $TESTPATH`
        DOTS0=$(printf "%${LENGTH}s" '')
        DOTS=...${DOTS0//?/.}
        echo -n "$DOTS"
        $TIMEOUT $EXEC $OPTIONS $EXTRA_OPTS --silent --verbosity=9 \
            --input $TEST > $TESTNAME.out 2>&1
        STATUS=$?
        TIMEDOUT=no
        if [ $STATUS = 124 ]
        then
            TIMEDOUT=yes
            echo "TIMEOUT" > $TESTNAME.out
        fi
        if [ ! -e $TESTNAME.exp ]
        then
            touch $TESTNAME.exp
        fi

        grep "warning" $TESTNAME.out

        N=1
        M=`wc -l < $TESTNAME.exp`
        OK=yes
        while [ $N -le $M ]
        do
            STRING=`head -n $N $TESTNAME.exp | tail -n 1`
            if grep "$STRING" $TESTNAME.out > /dev/null 2>&1
            then
                N=`expr $N + 1` 
                continue   
            else
                if grep DEBUG $TESTNAME.out > /dev/null
                then
                    echo -e "\033[35m???\033[0m"
                    UNKNOWN=`expr $UNKNOWN + 1`
                else
                    if [ $TIMEDOUT = "yes" ]
                    then
                        echo -e "\033[31mTIMEOUT\033[0m"
                    else
                        echo -e "\033[31mFAILED\033[0m (\"$STRING\" missing)"
                    fi
                    FAILED=`expr $FAILED + 1`
                fi
                OK=no
                break
            fi
        done
        if [ "$M" = 0 ]
        then
            echo -e "\033[31mMISSING\033[0m"
            FAILED=`expr $FAILED + 1`
            OK=no
        fi
        if [ "$OK" = yes ]
        then
            echo -e "\033[32mpassed\033[0m"
            rm -rf $TESTNAME.out
            PASSED=`expr $PASSED + 1`
        fi
    done
    cd ..
done

echo
if [ $UNKNOWN = 0 ]
then
    echo "$PASSED passed, $FAILED failed."
else
    echo "$PASSED passed, $FAILED failed, $UNKNOWN unknown."
    echo
    echo "Try compiling with NODEBUG enabled."
fi
echo

