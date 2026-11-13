#!/bin/sh
# Build the unit test binary for one configuration.
# Usage: build.sh <out-binary> [extra compiler flags...]
#   LAUNCH=1  build the OS::launch() test program instead of the main one
#   WIDE=N    build the process-count test program (test_wide.cpp) for N processes
#   OBJDIR    compile each source to an object in OBJDIR first (needed for coverage)
#   SRC   scmRTOS tree to test        (default: the tree this directory is in)
#   PORT  Linux port directory        (default: $SRC/port/linux/gcc, else ../../port/linux/gcc)
#   CXX   compiler                    (default: g++)
set -e
U=$(cd "$(dirname "$0")" && pwd)
OUTBIN=${1:?output binary}; shift
SRC=${SRC:-$U/../..}
if [ -z "$PORT" ]; then
  if [ -f "$SRC/port/linux/gcc/os_target.h" ]; then PORT=$SRC/port/linux/gcc; else PORT=$U/../../port/linux/gcc; fi
fi
CXX=${CXX:-g++}

# Sources of the tree under test; develop-only parts are detected by file
CORE="$SRC/core/os_kernel.cpp $SRC/core/os_services.cpp"
[ -f "$SRC/core/usrlib.cpp" ] && CORE="$CORE $SRC/core/usrlib.cpp"
EXT_INC=""; EXT_SRC=""; TESTS="$U/test_kernel.cpp $U/test_ring_buffer.cpp $U/test_services.cpp $U/test_fuzz.cpp $U/test_atomic.cpp"
add_ext() { # dir source test
  if [ -d "$SRC/ext/$1" ]; then
    EXT_INC="$EXT_INC -I$SRC/ext/$1"
    [ -n "$2" ] && EXT_SRC="$EXT_SRC $SRC/ext/$1/$2"
    [ -n "$3" ] && TESTS="$TESTS $U/$3"
  fi
  return 0
}
add_ext recursive-mutex recursive_mutex.cpp test_recursive_mutex.cpp
add_ext semaphore       semaphore.cpp       test_semaphore.cpp
add_ext event-flags     event_flags.cpp     test_event_flags.cpp
add_ext safe-run        ""                  test_safe_run.cpp
add_ext round-robin     ""                  test_round_robin.cpp
add_ext profiler        ""                  test_profiler.cpp
add_ext watchdog        ""                  test_watchdog.cpp

EXISTING=""; for t in $TESTS; do [ -f "$t" ] && EXISTING="$EXISTING $t"; done
[ -d "$SRC/ext/semaphore" ] || set -- -DUT_V52 "$@"
# defect 2 (a timed wait can turn into an endless one) is present unless the timer skips
# ready processes (develop) or the services re-check the timeout (patch 0002 for v5.2.0)
grep -q "is_process_ready(p->Priority)" "$SRC/core/os_kernel.h" || \
  grep -q "timeout expired after the process had been waked up" "$SRC/core/os_services.h" || set -- -DUT_DEFECT2_PRESENT "$@"
# fix of defect 4 present: ring_buffer rejects a Size its index type cannot hold at compile time
grep -q "static_assert(Size" "$SRC/core/usrlib.h" && set -- -DUT_RB_SIZE_CHECKED "$@"

if [ "$LAUNCH" = 1 ]; then
  [ -d "$SRC/ext/launch" ] || { echo "no ext/launch in $SRC" >&2; exit 3; }
  EXISTING="$U/test_launch.cpp"; EXT_SRC=""; EXT_INC="-I$SRC/ext/launch"
  set -- -DUT_EXTERNAL_PROCESSES -DscmRTOS_PROCESS_COUNT=3 "$@"
fi

if [ -n "$WIDE" ]; then
  EXISTING="$U/test_wide.cpp"; EXT_SRC=""; EXT_INC=""
  set -- -DUT_EXTERNAL_PROCESSES -DscmRTOS_PROCESS_COUNT=$WIDE "$@"
fi

OPT=${OPT:--O1}
FLAGS="-std=c++17 $OPT -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I$U -I$SRC/core -I$PORT $EXT_INC"
ALL="$U/utest.cpp $EXISTING $CORE $EXT_SRC $PORT/os_target.cpp"

if [ -z "$OBJDIR" ]; then
  $CXX $FLAGS "$@" -o "$OUTBIN" $ALL
else
  mkdir -p "$OBJDIR"; OBJS=""
  for f in $ALL; do
    o="$OBJDIR/$(echo "$f" | sed 's|.*/\(ext/[^/]*/\)|\1|; s|.*/||; s|/|_|g').o"
    $CXX $FLAGS "$@" -c -o "$o" "$f"
    OBJS="$OBJS $o"
  done
  $CXX $FLAGS "$@" -o "$OUTBIN" $OBJS
fi
