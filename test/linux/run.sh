#!/bin/sh
# Linux port (port/linux/gcc) self-tests over all port configurations.
# Usage: run.sh [path-to-scmRTOS]   (default: the tree this port is in)
T=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-$T/../..}
OUT=${OUT:-$(mktemp -d)}; mkdir -p "$OUT"
CXX=${CXX:-g++}
USRLIB=""; [ -f "$SRC/core/usrlib.cpp" ] && USRLIB="$SRC/core/usrlib.cpp"
rc=0
for scheme in 0 1; do
  for order in 0 1; do
    cfg="-DscmRTOS_CONTEXT_SWITCH_SCHEME=$scheme -DscmRTOS_PRIORITY_ORDER=$order"
    FLAGS="-std=c++11 -O2 -g -Wall -Wextra -Wno-unused-parameter -I$T -I$SRC/core -I$SRC/port/linux/gcc $cfg"
    KERNEL="$SRC/core/os_kernel.cpp $SRC/core/os_services.cpp $USRLIB $SRC/port/linux/gcc/os_target.cpp"
    echo "== scheme $scheme, priority order $order =="
    $CXX $FLAGS -DscmRTOS_LINUX_TICK_MODE=0 -o "$OUT/manual" "$T/test_manual.cpp" $KERNEL || { rc=1; continue; }
    timeout 10 "$OUT/manual" || rc=1
    $CXX $FLAGS -DscmRTOS_LINUX_TICK_MODE=1 -DscmRTOS_PROCESS_COUNT=5 -o "$OUT/realtime" "$T/test_realtime.cpp" $KERNEL || { rc=1; continue; }
    timeout 20 "$OUT/realtime" || { echo "FAIL realtime: exit $?"; rc=1; }
  done
done
exit $rc
