#!/bin/sh
# Build every configuration with coverage, run the tests, print merged coverage.
# Usage: coverage.sh [--uncovered]
#   SRC, PORT, CXX as for build.sh (gcov must match the compiler); OUT: work directory
U=$(cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$U/../..}; export SRC
OUT=${OUT:-$(mktemp -d)}; mkdir -p "$OUT"
GCOV=${GCOV:-gcov}
DIRS=""
run_cfg() { # name [LAUNCH] flags...
  name=$1; shift
  rm -rf "$OUT/$name"; mkdir -p "$OUT/$name/obj" "$OUT/$name/gcov"
  OBJDIR="$OUT/$name/obj" OPT=-O0 "$U/build.sh" "$OUT/$name/ut" --coverage -fno-exceptions -DINLINE=inline "$@" || return 1
  "$OUT/$name/ut" > "$OUT/$name/result.txt" 2>&1
  grep SUMMARY "$OUT/$name/result.txt" | sed "s/^/$name: /"
  ( cd "$OUT/$name/gcov" && $GCOV -b -l -p -o "$OUT/$name/obj" "$OUT/$name"/obj/*.o > /dev/null 2>&1 )
  DIRS="$DIRS $OUT/$name/gcov"
}
for scheme in 0 1; do
  for order in 0 1; do
    run_cfg "s$scheme-o$order-r0" -DscmRTOS_CONTEXT_SWITCH_SCHEME=$scheme -DscmRTOS_PRIORITY_ORDER=$order
    run_cfg "s$scheme-o$order-r1" -DscmRTOS_CONTEXT_SWITCH_SCHEME=$scheme -DscmRTOS_PRIORITY_ORDER=$order -DscmRTOS_PROCESS_RESTART_ENABLE=1
    [ -d "$SRC/ext/launch" ] && LAUNCH=1 run_cfg "launch-s$scheme-o$order" -DscmRTOS_CONTEXT_SWITCH_SCHEME=$scheme -DscmRTOS_PRIORITY_ORDER=$order
  done
done
echo
python3 "$U/gcov_summary.py" "$SRC" $DIRS "$@"
