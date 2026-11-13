#!/bin/sh
# 16-bit target check: evaluate kernel process-map code under a 16-bit int.
# Usage: run.sh [path-to-scmRTOS]      needs clang with the msp430 and avr targets
D=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-$D/../..}
CLANG=${CLANG:-clang++}
OUT=${OUT:-$(mktemp -d)}; mkdir -p "$OUT"
rc=0
PFIX=""; grep -q "static_cast<TProcessMap>(1) << scmRTOS_PROCESS_COUNT" "$SRC/port/avr/gcc/os_target.h" && PFIX="-DTARGET16_PORT_SHIFT_FIXED"
for target in msp430 avr; do
  for order in 0 1; do
    ll="$OUT/check-$target-$order.ll"
    if ! $CLANG --target=$target -std=c++11 -O2 -ffreestanding -fno-exceptions -fno-rtti -S -emit-llvm \
          -DscmRTOS_PRIORITY_ORDER=$order $PFIX -I"$D" -I"$SRC/core" -o "$ll" "$D/check.cpp" 2> "$OUT/err.txt"; then
      echo "FAIL $target order $order: does not compile"; head -5 "$OUT/err.txt"; rc=1; continue
    fi
    grep -E "warning: shift count" "$OUT/err.txt" | sed "s/^.*warning: /WARN $target order $order: /" | sort -u
    grep "// EXPECT" "$D/check.cpp" | sort -u | while read -r _ _ name want want1; do
      [ "$order" = 1 ] && [ -n "$want1" ] && want=$want1
      got=$(awk -v f="@$name(" '$0 ~ /^define/ && index($0, f) {inside=1} inside && /ret i(16|32)/ {print $NF; exit}' "$ll")
      case "$got" in
        ''|*[!0-9-]*) echo "FAIL $target order $order: $name has no value: undefined behaviour, the compiler dropped the code"; echo 1 > "$OUT/failed";;
        *) g=$(printf '0x%X' $(( got & 0xFFFFFFFF ))); w=$(printf '0x%X' $(( want )))
           if [ "$g" = "$w" ]; then echo "PASS $target order $order: $name = $g"
           else echo "FAIL $target order $order: $name = $g, expected $w"; echo 1 > "$OUT/failed"; fi;;
      esac
    done
  done
done
[ -f "$OUT/failed" ] && rc=1
exit $rc
