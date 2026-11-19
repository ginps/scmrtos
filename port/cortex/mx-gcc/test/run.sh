#!/bin/sh
# Test matrix of the Cortex-M port in the cm4sim model.
# Usage: run.sh [quick]
set -u
here=$(cd "$(dirname "$0")" && pwd)
out=${OUT:-/tmp/cm4test}; mkdir -p "$out"
cc -O2 -o "$out/cm4sim" "$here/cm4sim.c" || exit 1
sim=$out/cm4sim
pass=0; failn=0; n=0

# run NAME EXPECT_EXIT SIMFLAGS -- BUILDFLAGS
run() {
    name=$1; expect=$2; simf=$3; shift 3
    n=$((n+1))
    if ! "$here/build.sh" "$out/$name.elf" "$@" 2> "$out/$name.build"; then
        echo "BUILD FAIL $name: $*"; failn=$((failn+1)); return
    fi
    rates=$RATES
    case "$*" in *-O0*) rates=$RATES_O0;; esac
    for seed in $SEEDS; do for rate in $rates; do
        "$sim" -s $seed -r $rate $simf "$out/$name.elf" > "$out/$name.$seed.$rate.log" 2>&1
        rc=$?
        if [ $rc -eq $expect ]; then pass=$((pass+1)); else
            failn=$((failn+1)); echo "FAIL $name seed=$seed rate=$rate rc=$rc ($*)"; tail -3 "$out/$name.$seed.$rate.log"; fi
    done; done
}

# random interrupt rate per million instructions; -O0 code is slower, the
# rates are chosen so that the lowest priority process still makes progress
if [ "${1:-}" = quick ]; then SEEDS="1"; RATES="3000"; RATES_O0="1500"; OPTS="-O2"
else SEEDS="1 2 3"; RATES="500 3000 6000"; RATES_O0="300 1000 2000"; OPTS="-O0 -Os -O2"; fi

FPU="-mfloat-abi=hard -mfpu=fpv4-sp-d16"
for o in $OPTS; do
    for di in 0 2 5 6; do
        for pg in 3 5; do
            run "soft$o.di$di.pg$pg" 0 ""   $o -DTEST_DI_LEVEL=$di -DTEST_PRIGROUP=$pg
            run "fpu$o.di$di.pg$pg"  0 "-f" $o -DTEST_DI_LEVEL=$di -DTEST_PRIGROUP=$pg $FPU
        done
    done
    # ISR priority check: silent for correct ISRs (also at the level itself and
    # with grouping), fires for an OS call above the level
    run "chk$o.di5"         0 ""   $o -DTEST_DI_LEVEL=5 -DTEST_CHECK_ISR_PRIORITY
    run "chk$o.di6"         0 ""   $o -DTEST_DI_LEVEL=6 -DTEST_CHECK_ISR_PRIORITY
    run "chk$o.di5.pg5.a4"  0 ""   $o -DTEST_DI_LEVEL=5 -DTEST_CHECK_ISR_PRIORITY -DTEST_PRIGROUP=5 -DTEST_PRIO_A=4
    run "chkbad$o.di5"      0 ""   $o -DTEST_DI_LEVEL=5 -DTEST_CHECK_ISR_PRIORITY -DTEST_BAD_ISR
    run "chkbad$o.di5.pg5"  0 ""   $o -DTEST_DI_LEVEL=5 -DTEST_CHECK_ISR_PRIORITY -DTEST_BAD_ISR -DTEST_PRIGROUP=5
    # level in group priority 0 (2 group bits, level 2): os_start() check fires
    run "chkgrp0$o"         0 ""   $o -DTEST_DI_LEVEL=2 -DTEST_CHECK_ISR_PRIORITY -DTEST_PRIGROUP=5 -DTEST_EXPECT_CHECK
    # Cortex-M7 r0p1 erratum 837070 workaround
    run "m7err$o.di5"       0 "-f" $o -DTEST_DI_LEVEL=5 -DscmRTOS_CM7_R0P1_ERRATUM_837070=1 $FPU
done

echo "configurations: $n, runs passed: $pass, failed: $failn"
[ $failn -eq 0 ]
