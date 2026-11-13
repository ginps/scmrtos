#!/bin/sh
# Run the unit tests in every configuration.
# Usage: run.sh [runner options]          e.g. run.sh -v mutex
#   SRC, PORT, CXX, OPT as for build.sh; OUT: directory for the binaries
#   FUZZ_SEEDS: seeds per fuzz test and configuration (default 300, 0 = skip)
U=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$(mktemp -d)}; mkdir -p "$OUT"
FUZZ_SEEDS=${FUZZ_SEEDS-300}
SRCDIR=${SRC:-$U/../..}
rc=0
for scheme in 0 1; do
  for order in 0 1; do
    cfg="-DscmRTOS_CONTEXT_SWITCH_SCHEME=$scheme -DscmRTOS_PRIORITY_ORDER=$order"
    for restart in 0 1; do
      name="scheme$scheme-order$order-restart$restart"
      echo "=================== $name"
      "$U/build.sh" "$OUT/ut-$name" $cfg -DscmRTOS_PROCESS_RESTART_ENABLE=$restart || { rc=1; continue; }
      "$OUT/ut-$name" "$@" || rc=1
    done
    if [ "$FUZZ_SEEDS" != 0 ] && [ $# = 0 ]; then
      echo "=================== fuzz-scheme$scheme-order$order (seeds 1-$FUZZ_SEEDS)"
      exe="$OUT/ut-scheme$scheme-order$order-restart1"
      "$exe" --seeds=1-$FUZZ_SEEDS fuzz.injected fuzz.no_injection fuzz.async || rc=1
      # one interrupt of every kind at every point of a fixed scenario
      points=$("$exe" -v --seed=1000000 fuzz.single_interrupt | sed -n 's/^ *points: \([0-9]*\).*/\1/p')
      "$exe" --seeds=1-$((3 * ${points:-1})) fuzz.single_interrupt || rc=1
    fi
    if [ -d "$SRCDIR/ext/launch" ]; then
      name="launch-scheme$scheme-order$order"
      echo "=================== $name"
      LAUNCH=1 "$U/build.sh" "$OUT/ut-$name" $cfg || { rc=1; continue; }
      "$OUT/ut-$name" "$@" || rc=1
    fi
    # process count boundaries: 1, the map type changes at 8 and 16, full maps at 7, 15, 31
    for n in 1 7 8 15 16 31; do
      name="wide$n-scheme$scheme-order$order"
      echo "=================== $name"
      WIDE=$n "$U/build.sh" "$OUT/ut-$name" $cfg -DscmRTOS_SUSPENDED_PROCESS_ENABLE=0 || { rc=1; continue; }
      "$OUT/ut-$name" "$@" || rc=1
    done
  done
done
# get_prio_tag() on a dynamically initialized table, as in the AVR/MSP430/STM8 ports (defect 3)
name="masktable-scheme0-order0"
echo "=================== $name"
"$U/build.sh" "$OUT/ut-$name" -DscmRTOS_LINUX_PRIO_MASK_TABLE=1 && "$OUT/ut-$name" "$@" || rc=1
exit $rc
