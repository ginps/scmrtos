#!/usr/bin/env python3
"""Mutation testing of scmRTOS with the unit tests.

Every mutant is one small change of one covered source line (an operator,
a constant, a negated condition, a deleted statement). The unit tests are
built and run against it; a mutant that no test notices "survives" and
points at behaviour the tests do not pin down.

Usage:
  mutate.py --covered covered.txt --work DIR [--src TREE] [--jobs N]
            [--files REGEX] [--ops OP,OP] [--limit N] [--report FILE] [--list]
            [--retest REPORT.json] [--cxxflags "FLAGS"] [--fuzz-seeds N]

  --covered  "file:line" list of executed lines (coverage.sh --dump-covered=FILE);
             only these lines are mutated - an unexecuted line survives trivially
  --work     scratch directory (one copy of the tree per job)
  --files    mutate only files matching the regex (default: core/ and ext/)
  --list     print the mutants and exit
  --retest   test only the mutants that survived in an earlier report
  --cxxflags extra compiler flags, e.g. "-fsanitize=address,undefined
             -fno-sanitize-recover=undefined": sanitizers kill the mutants
             that only read or write out of bounds
  --fuzz-seeds N  also run the fuzz tests with seeds 1..N (kills the mutants
             that only misbehave under preemption, e.g. a removed critical section)

A mutant is run in two configurations that together cover both context
switch schemes and both priority orders (launch.h: the launch program;
core/: also the 7- and 8-process programs without suspended-start support).
Result: killed (a test failed, crashed or hung, or a static_assert fired),
survived, or invalid (does not compile).
"""
import argparse, json, os, re, shutil, subprocess, sys, threading, queue

CONFIGS = [
    ['-DscmRTOS_CONTEXT_SWITCH_SCHEME=0', '-DscmRTOS_PRIORITY_ORDER=0', '-DscmRTOS_PROCESS_RESTART_ENABLE=1'],
    ['-DscmRTOS_CONTEXT_SWITCH_SCHEME=1', '-DscmRTOS_PRIORITY_ORDER=1', '-DscmRTOS_PROCESS_RESTART_ENABLE=1'],
]

# (name, regex, replacement) - applied to each match separately
SUBST = [
    ('ROR', r'==', '!='), ('ROR', r'!=', '=='),
    ('ROR', r' < ', ' <= '), ('ROR', r' <= ', ' < '), ('ROR', r' > ', ' >= '), ('ROR', r' >= ', ' > '),
    ('LCR', r'&&', '||'), ('LCR', r'\|\|', '&&'),
    ('AOR', r' \+ ', ' - '), ('AOR', r' - ', ' + '),
    ('AOR', r'\+\+', '--'), ('AOR', r'--', '++'),
    ('AOR', r'\+=', '-='), ('AOR', r'-=', '+='),
    ('BIT', r' & ', ' | '), ('BIT', r' \| ', ' & '), ('BIT', r'\|=', '&='), ('BIT', r'&=', '|='),
    ('BIT', r'~', ''), ('BIT', r' << ', ' >> '), ('BIT', r' >> ', ' << '),
    ('CONST', r'= 0;', '= 1;'), ('CONST', r'= 1;', '= 0;'),
    ('CONST', r'\btrue\b', 'false'), ('CONST', r'\bfalse\b', 'true'),
    ('CONST', r'\(0\)', '(1)'),
]
KEYWORDS = ('return|if|else|for|while|do|case|break|continue|typedef|using|static|const|volatile|INLINE|'
            'template|friend|extern|enum|struct|class|public|private|protected|new|delete|goto')
STMT_CALL   = re.compile(r'^\s*(?!(?:%s)\b)[A-Za-z_][\w:.\[\]>-]*\s*\(.*\)\s*;\s*$' % KEYWORDS)
STMT_ASSIGN = re.compile(r'^\s*(?!(?:%s)\b)[A-Za-z_*][\w:.\[\]()>*-]*\s*(?:=|\|=|&=|\+=|-=)\s*[^=].*;\s*$' % KEYWORDS)
CRITSECT    = re.compile(r'^\s*TCritSect\s+\w+;\s*$')


def split_comment(line):
    i = line.find('//')
    return (line, '') if i < 0 else (line[:i], line[i:])


def negate_conditions(code):
    out = []
    for m in re.finditer(r'\b(if|while)\s*\(', code):
        start = m.end()
        depth, i = 1, start
        while i < len(code) and depth:
            depth += code[i] == '('
            depth -= code[i] == ')'
            i += 1
        if depth == 0:
            out.append(code[:start] + '!(' + code[start:i - 1] + ')' + code[i - 1:])
    return out


def mutants_of_line(text):
    code, comment = split_comment(text.rstrip('\n'))
    if not code.strip() or code.lstrip().startswith('#'):
        return
    for name, rx, rep in SUBST:
        for m in re.finditer(rx, code):
            yield name, code[:m.start()] + rep + code[m.end():] + comment
    for new in negate_conditions(code):
        yield 'NEG', new + comment
    if CRITSECT.match(code):
        yield 'CS', re.sub(r'TCritSect\s+\w+;', ';', code) + comment
    elif STMT_CALL.match(code) or STMT_ASSIGN.match(code):
        indent = re.match(r'\s*', code).group(0)
        yield 'DEL', indent + ';' + comment


def generate(src, covered, files_rx, ops):
    by_file = {}
    for l in open(covered):
        f, ln = l.strip().rsplit(':', 1)
        if re.search(files_rx, f):
            by_file.setdefault(f, set()).add(int(ln))
    result = []
    for f in sorted(by_file):
        lines = open(os.path.join(src, f), newline='').read().split('\n')
        for ln in sorted(by_file[f]):
            for op, new in mutants_of_line(lines[ln - 1]):
                if ops and op not in ops:
                    continue
                result.append({'file': f, 'line': ln, 'op': op, 'old': lines[ln - 1].strip(), 'new': new.strip(), 'text': new})
    return result


def run(cmd, env=None, timeout=300):
    try:
        p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return p.returncode, p.stdout.decode(errors='replace')
    except subprocess.TimeoutExpired:
        return -1, 'TIMEOUT'


EXTRA_FLAGS = []
FUZZ_SEEDS = 0


def test_mutant(m, tree):
    path = os.path.join(tree, m['file'])
    orig = open(path, newline='').read()
    lines = orig.split('\n')
    lines[m['line'] - 1] = m['text']
    open(path, 'w', newline='').write('\n'.join(lines))
    try:
        env = dict(os.environ, SRC=tree, PORT=os.path.join(tree, 'port/linux/gcc'), OPT='-O0',
                   ASAN_OPTIONS='detect_stack_use_after_return=0:detect_leaks=0')
        is_launch = m['file'].startswith('ext/launch')
        if is_launch:
            env['LAUNCH'] = '1'
        exe = os.path.join(tree, 'ut')
        for cfg in CONFIGS:
            rc, out = run([os.path.join(tree, 'test/unit/build.sh'), exe] + cfg + EXTRA_FLAGS, env)
            if rc != 0:
                if 'static assertion failed' in out:
                    return 'killed', 'static_assert'
                return 'invalid', out.strip().split('\n')[0][:200]
            cmds = [[exe, '-x', '--timeout=1500']]
            if FUZZ_SEEDS and not is_launch:
                cmds.append([exe, '-x', '--timeout=3000', '--seeds=1-%d' % FUZZ_SEEDS, 'fuzz'])
            for cmd in cmds:
                rc, out = run(cmd, env=env, timeout=600)
                if rc != 0:
                    why = [l for l in out.split('\n') if l.startswith('[ FAIL ]')]
                    return 'killed', (why[0][9:] if why else 'runner: ' + out[-100:]).strip()
        if m['file'].startswith('core/'):
            # process-count programs without suspended-start support: 7 processes fill an
            # 8-bit map; 8 processes with descending priorities use a wider map, where
            # stray bits above the idle process are the highest priority
            for wide, cfg in (('7', []), ('8', CONFIGS[1][:2])):
                env['WIDE'] = wide
                rc, out = run([os.path.join(tree, 'test/unit/build.sh'), exe, '-DscmRTOS_SUSPENDED_PROCESS_ENABLE=0'] + cfg + EXTRA_FLAGS, env)
                if rc != 0:
                    return ('killed', 'static_assert') if 'static assertion failed' in out else ('invalid', out.strip().split('\n')[0][:200])
                rc, out = run([exe, '-x', '--timeout=1500'], env=env, timeout=180)
                if rc != 0:
                    why = [l for l in out.split('\n') if l.startswith('[ FAIL ]')]
                    return 'killed', (why[0][9:] if why else 'runner: ' + out[-100:]).strip()
        return 'survived', ''
    finally:
        open(path, 'w', newline='').write(orig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    ap.add_argument('--covered', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    ap.add_argument('--files', default=r'^(core|ext)/')
    ap.add_argument('--ops', default='')
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--report', default='')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--retest', default='')
    ap.add_argument('--cxxflags', default='')
    ap.add_argument('--fuzz-seeds', type=int, default=0)
    a = ap.parse_args()
    src = os.path.realpath(a.src)
    global EXTRA_FLAGS, FUZZ_SEEDS
    EXTRA_FLAGS = a.cxxflags.split()
    FUZZ_SEEDS = a.fuzz_seeds

    mutants = generate(src, a.covered, a.files, set(a.ops.split(',')) if a.ops else None)
    if a.retest:
        keep = set((m['file'], m['line'], m['op'], m['new']) for m in json.load(open(a.retest)) if m['status'] == 'survived')
        mutants = [m for m in mutants if (m['file'], m['line'], m['op'], m['new']) in keep]
    if a.limit:
        step = max(1, len(mutants) // a.limit)
        mutants = mutants[::step][:a.limit]
    if a.list:
        for m in mutants:
            print('%s:%d [%s] %s' % (m['file'], m['line'], m['op'], m['new']))
        print(len(mutants), 'mutants')
        return

    os.makedirs(a.work, exist_ok=True)
    trees = []
    for j in range(a.jobs):
        t = os.path.join(a.work, 'w%d' % j)
        shutil.rmtree(t, ignore_errors=True)
        for d in ('core', 'ext', 'port/linux', 'test/unit'):
            shutil.copytree(os.path.join(src, d), os.path.join(t, d))
        trees.append(t)

    # the unmutated tree must pass
    env = dict(os.environ, SRC=trees[0], PORT=os.path.join(trees[0], 'port/linux/gcc'), OPT='-O0',
               ASAN_OPTIONS='detect_stack_use_after_return=0:detect_leaks=0')
    for cfg in CONFIGS:
        rc, out = run([os.path.join(trees[0], 'test/unit/build.sh'), os.path.join(trees[0], 'ut')] + cfg + EXTRA_FLAGS, env)
        rc2, out2 = run([os.path.join(trees[0], 'ut'), '-x', '--timeout=1500'], env=env) if rc == 0 else (1, '')
        if rc or rc2:
            sys.exit('baseline does not pass:\n' + out + out2)

    q = queue.Queue()
    for i, m in enumerate(mutants):
        q.put((i, m))
    lock = threading.Lock()
    done = [0]

    def worker(tree):
        while True:
            try:
                i, m = q.get_nowait()
            except queue.Empty:
                return
            m['status'], m['detail'] = test_mutant(m, tree)
            with lock:
                done[0] += 1
                if done[0] % 25 == 0 or m['status'] == 'survived':
                    print('[%d/%d] %-8s %s:%d [%s] %s' % (done[0], len(mutants), m['status'], m['file'], m['line'], m['op'], m['new']), flush=True)

    threads = [threading.Thread(target=worker, args=(t,)) for t in trees]
    for t in threads: t.start()
    for t in threads: t.join()

    stats = {}
    for m in mutants:
        s = stats.setdefault(m['file'], {'killed': 0, 'survived': 0, 'invalid': 0})
        s[m['status']] += 1
    lines = []
    w = max(len(f) for f in stats)
    lines.append('%-*s  %7s %8s %7s  %s' % (w, 'File', 'killed', 'survived', 'invalid', 'score'))
    tk = ts = ti = 0
    for f in sorted(stats):
        s = stats[f]
        valid = s['killed'] + s['survived']
        lines.append('%-*s  %7d %8d %7d  %5.1f%%' % (w, f, s['killed'], s['survived'], s['invalid'], 100.0 * s['killed'] / valid if valid else 100))
        tk += s['killed']; ts += s['survived']; ti += s['invalid']
    lines.append('%-*s  %7d %8d %7d  %5.1f%%' % (w, 'TOTAL', tk, ts, ti, 100.0 * tk / (tk + ts) if tk + ts else 100))
    lines.append('')
    lines.append('Survived mutants:')
    for m in mutants:
        if m['status'] == 'survived':
            lines.append('  %s:%d [%s]\n      - %s\n      + %s' % (m['file'], m['line'], m['op'], m['old'], m['new']))
    text = '\n'.join(lines)
    print(text)
    if a.report:
        open(a.report, 'w').write(text + '\n')
        json.dump([{k: v for k, v in m.items() if k != 'text'} for m in mutants], open(a.report + '.json', 'w'), indent=1, ensure_ascii=False)


if __name__ == '__main__':
    main()
