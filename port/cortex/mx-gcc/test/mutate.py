#!/usr/bin/env python3
"""Mutation testing of the interrupt masking in the Cortex-M GCC port.

Each mutant is one deliberate defect in a copy of port/cortex/mx-gcc.
A mutant is killed when at least one test run fails (wrong exit code,
model error or cycle limit). Usage: mutate.py [--work DIR]
"""
import os, shutil, subprocess, sys, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..'))

FPU = ['-mfloat-abi=hard', '-mfpu=fpv4-sp-d16']
SOFT5 = ([], ['-O2', '-DTEST_DI_LEVEL=5'])
FPU5 = (['-f'], ['-O2', '-DTEST_DI_LEVEL=5'] + FPU)
SOFT6 = ([], ['-O2', '-DTEST_DI_LEVEL=6'])
SOFT0 = ([], ['-O2', '-DTEST_DI_LEVEL=0'])
FPU0 = (['-f'], ['-O2', '-DTEST_DI_LEVEL=0'] + FPU)
CHK6 = ([], ['-O2', '-DTEST_DI_LEVEL=6', '-DTEST_CHECK_ISR_PRIORITY'])
CHKPG5 = ([], ['-O2', '-DTEST_DI_LEVEL=5', '-DTEST_CHECK_ISR_PRIORITY', '-DTEST_PRIGROUP=5', '-DTEST_PRIO_A=4'])
CHKBAD = ([], ['-O2', '-DTEST_DI_LEVEL=5', '-DTEST_CHECK_ISR_PRIORITY', '-DTEST_BAD_ISR'])
CHKGRP0 = ([], ['-O2', '-DTEST_DI_LEVEL=2', '-DTEST_CHECK_ISR_PRIORITY', '-DTEST_PRIGROUP=5', '-DTEST_EXPECT_CHECK'])
ALL = [SOFT5, FPU5, SOFT6, SOFT0, FPU0, CHK6, CHKPG5, CHKBAD, CHKGRP0]

# (name, file, old, new, configs, note)
MUTANTS = [
    ('critsect_no_mask', 'os_target.h',
     '        "MSR     BASEPRI_MAX, %0\\n"\n        "ISB                    \\n"\n        : : "r"(scmRTOS_OS_DI_BASEPRI)',
     '        "NOP\\n"\n        "ISB                    \\n"\n        : : "r"(scmRTOS_OS_DI_BASEPRI)', [SOFT5, FPU5], ''),
    ('critsect_restore_zero', 'os_target.h',
     '~TCritSect() { set_interrupt_state(StatusReg); }', '~TCritSect() { set_interrupt_state(0); }', [SOFT5, SOFT0], ''),
    ('critsect_save_primask', 'os_target.h',
     '__asm__ __volatile__ ("MRS %0, BASEPRI" : "=r"(sr) : : "memory");',
     '__asm__ __volatile__ ("MRS %0, PRIMASK" : "=r"(sr) : : "memory");', [SOFT5, FPU5], ''),
    ('critsect_basepri_not_max', 'os_target.h',
     '        "MSR     BASEPRI_MAX, %0\\n"\n        "ISB                    \\n"\n        : : "r"(scmRTOS_OS_DI_BASEPRI)',
     '        "MSR     BASEPRI, %0\\n"\n        "ISB                    \\n"\n        : : "r"(scmRTOS_OS_DI_BASEPRI)', [SOFT5, FPU5], ''),
    ('critsect_no_isb', 'os_target.h',
     '        "MSR     BASEPRI_MAX, %0\\n"\n        "ISB                    \\n"\n        : : "r"(scmRTOS_OS_DI_BASEPRI)',
     '        "MSR     BASEPRI_MAX, %0\\n"\n        : : "r"(scmRTOS_OS_DI_BASEPRI)', [SOFT5],
     'not observable: the model applies MSR at the next instruction'),
    ('level_not_shifted', 'os_target.h',
     '(((scmRTOS_OS_DI_LEVEL) << (8 - (CORE_PRIORITY_BITS))) & 0xFF)', '((scmRTOS_OS_DI_LEVEL) & 0xFF)', [SOFT5], ''),
    ('level_plus_one', 'os_target.h',
     '(((scmRTOS_OS_DI_LEVEL) << (8 - (CORE_PRIORITY_BITS))) & 0xFF)',
     '(((scmRTOS_OS_DI_LEVEL + 1) << (8 - (CORE_PRIORITY_BITS))) & 0xFF)', [SOFT6], ''),
    ('level_minus_one', 'os_target.h',
     '(((scmRTOS_OS_DI_LEVEL) << (8 - (CORE_PRIORITY_BITS))) & 0xFF)',
     '(((scmRTOS_OS_DI_LEVEL - 1) << (8 - (CORE_PRIORITY_BITS))) & 0xFF)', [([], ['-O2', '-DTEST_DI_LEVEL=4'])], ''),
    ('enable_context_switch_noop', 'os_target.h',
     'INLINE void enable_context_switch()  { enable_os_interrupts(); }', 'INLINE void enable_context_switch()  { }', [SOFT5], ''),
    ('disable_context_switch_noop', 'os_target.h',
     'INLINE void disable_context_switch() { disable_os_interrupts(); }', 'INLINE void disable_context_switch() { }', [SOFT5, FPU5], ''),
    ('enable_os_interrupts_noop', 'os_target.h',
     '        "MSR     BASEPRI, %0    \\n"\n        "ISB                    \\n"\n        : : "r"(0) : "memory"',
     '        "NOP\\n"\n        : : "r"(0) : "memory"', [SOFT5], ''),
    ('pendsv_no_mask', 'os_target.cpp',
     '        "    MOV     R1, %[basepri]            \\n" /* mask OS interrupts  */ \\\n        "    MSR     BASEPRI, R1               \\n"',
     '        "    MOV     R1, %[basepri]            \\n" /* mask OS interrupts  */ \\\n        "    NOP                               \\n"', [SOFT5, FPU5], ''),
    ('pendsv_mask_all_levels', 'os_target.cpp',
     '#define OS_PENDSV_BASEPRI   scmRTOS_OS_DI_BASEPRI', '#define OS_PENDSV_BASEPRI   0x10', [SOFT5], ''),
    ('pendsv_no_unmask', 'os_target.cpp',
     '        "    MOV     R1, #0                    \\n"                           \\\n        "    MSR     BASEPRI, R1               \\n"\n#define OS_PENDSV_BASEPRI',
     '        "    MOV     R1, #0                    \\n"                           \\\n        "    NOP                               \\n"\n#define OS_PENDSV_BASEPRI', [SOFT5], ''),
    ('pendsv_primask_no_mask', 'os_target.cpp',
     '        "    CPSID   I                         \\n" /* mask all interrupts */', '        "    NOP                               \\n"', [SOFT0, FPU0], ''),
    ('pendsv_fp_not_saved', 'os_target.cpp',
     '        "    VSTMDBEQ R0!, {S16-S31}           \\n"', '        "    SUBEQ   R0, R0, #64               \\n"', [FPU5, FPU0], ''),
    ('os_start_no_basepri_clear', 'os_target.cpp',
     '        "    MOV     R0, #0                    \\n" // Processes run with OS interrupts unmasked\n        "    MSR     BASEPRI, R0               \\n"\n',
     '', [SOFT5], ''),
    ('check_le', 'os_target.cpp',
     'if((prio & group_mask) < (scmRTOS_OS_DI_BASEPRI & group_mask))', 'if((prio & group_mask) <= (scmRTOS_OS_DI_BASEPRI & group_mask))', [CHK6], ''),
    ('check_ignores_grouping', 'os_target.cpp',
     '    uint32_t group_mask = group_priority_mask();', '    uint32_t group_mask = 0xFF;', [CHKPG5], ''),
    ('check_grp0_removed', 'os_target.cpp',
     '    check_os_di_level();\n#endif', '#endif', [CHKGRP0], ''),
    ('check_not_called', 'os_target.h',
     '    #if (scmRTOS_CRITSECT_BASEPRI == 1) && (scmRTOS_CHECK_ISR_PRIORITY == 1)\n        os_check_isr_priority();\n    #endif',
     '', [CHKBAD], ''),
]

def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **kw)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', default='/tmp/cm4mut')
    ap.add_argument('--seeds', default='1 2')
    ap.add_argument('--rates', default='3000 8000')
    args = ap.parse_args()
    os.makedirs(args.work, exist_ok=True)
    sim = os.path.join(args.work, 'cm4sim')
    if run(['cc', '-O2', '-o', sim, os.path.join(HERE, 'cm4sim.c')]).returncode:
        sys.exit('cannot build model')

    killed = survived = errors = 0
    for name, fname, old, new, configs, note in MUTANTS:
        port = os.path.join(args.work, name)
        shutil.rmtree(port, ignore_errors=True)
        shutil.copytree(PORT, port, ignore=shutil.ignore_patterns('test'))
        path = os.path.join(port, fname)
        src = open(path).read()
        if src.count(old) != 1:
            print(f'{name:30} ERROR: pattern found {src.count(old)} times'); errors += 1; continue
        open(path, 'w').write(src.replace(old, new))
        how = None
        for simf, bflags in configs:
            elf = os.path.join(port, 'test.elf')
            b = run([os.path.join(HERE, 'build.sh'), elf] + bflags, env=dict(os.environ, PORT_DIR=port))
            if b.returncode:
                how = 'BUILD ERROR: ' + ' '.join(l for l in b.stdout.splitlines() if 'error' in l)[:200]; break
            for seed in args.seeds.split():
                for rate in args.rates.split():
                    r = run([sim, '-s', seed, '-r', rate, '-m', '30000000'] + simf + [elf])
                    if r.returncode:
                        last = [l for l in r.stdout.splitlines() if 'FAIL' in l or 'cm4sim:' in l and 'warning' not in l]
                        how = f'{" ".join(bflags[1:])} seed={seed} rate={rate}: ' + (last[0] if last else f'rc={r.returncode}')
                        break
                if how: break
            if how: break
        if how and how.startswith('BUILD ERROR'):
            errors += 1; print(f'{name:30} {how}')
        elif how:
            killed += 1; print(f'{name:30} killed   {how[:110]}')
        else:
            survived += 1; print(f'{name:30} SURVIVED {note}')
    print(f'mutants: {len(MUTANTS)}, killed: {killed}, survived: {survived}, build errors: {errors}')
    sys.exit(1 if survived or errors else 0)

if __name__ == '__main__':
    main()
