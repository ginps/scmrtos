#!/bin/sh
# Run the unit tests in every configuration.
# Usage: run.sh [runner options]          e.g. run.sh -v mutex
#   SRC, PORT, CXX as for build.sh; OUT: directory for the binaries
U=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$(mktemp -d)}; mkdir -p "$OUT"
rc=0
for scheme in 0 1; do
  for order in 0 1; do
    for restart in 0 1; do
      name="scheme$scheme-order$order-restart$restart"
      echo "=================== $name"
      "$U/build.sh" "$OUT/ut-$name" -DscmRTOS_CONTEXT_SWITCH_SCHEME=$scheme -DscmRTOS_PRIORITY_ORDER=$order \
          -DscmRTOS_PROCESS_RESTART_ENABLE=$restart || { rc=1; continue; }
      "$OUT/ut-$name" "$@" || rc=1
    done
    if [ -d "${SRC:-$U/../..}/ext/launch" ]; then
      name="launch-scheme$scheme-order$order"
      echo "=================== $name"
      LAUNCH=1 "$U/build.sh" "$OUT/ut-$name" -DscmRTOS_CONTEXT_SWITCH_SCHEME=$scheme -DscmRTOS_PRIORITY_ORDER=$order || { rc=1; continue; }
      "$OUT/ut-$name" "$@" || rc=1
    fi
  done
done
exit $rc
