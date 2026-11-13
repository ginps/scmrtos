#!/usr/bin/env python3
"""Merge gcov -b -l output of several builds and print line/branch coverage per source file.

Usage: gcov_summary.py <src-root> <gcov-dir>... [--uncovered]
A line is executable if any build compiled it, covered if any build ran it;
a branch is taken if it was taken in any build. Files outside core/ ext/ port/
(the tests and the framework) are left out.
"""
import os, re, sys
from collections import defaultdict

root = os.path.realpath(sys.argv[1])
dirs = [a for a in sys.argv[2:] if not a.startswith('--')]
show_uncovered = '--uncovered' in sys.argv

lines = defaultdict(dict)        # file -> line -> covered
branches = defaultdict(dict)     # file -> (line, n) -> taken
src_text = defaultdict(dict)

for d in dirs:
    for name in os.listdir(d):
        if not name.endswith('.gcov'):
            continue
        cur = None
        cur_line = 0
        bn = 0
        with open(os.path.join(d, name), errors='replace') as f:
            for l in f:
                m = re.match(r'\s*-:\s*0:Source:(.*)', l)
                if m:
                    p = os.path.realpath(m.group(1).strip())
                    rel = os.path.relpath(p, root)
                    cur = rel if rel.split(os.sep)[0] in ('core', 'ext', 'port') else None
                    continue
                if cur is None:
                    continue
                m = re.match(r'\s*([^:]+):\s*(\d+):(.*)', l)
                if m:
                    cnt, ln, text = m.group(1).strip(), int(m.group(2)), m.group(3)
                    cur_line, bn = ln, 0
                    if ln == 0:
                        continue
                    src_text[cur][ln] = text
                    if cnt == '-':
                        continue
                    hit = cnt not in ('#####', '=====') and not cnt.startswith('$$$')
                    lines[cur][ln] = lines[cur].get(ln, False) or hit
                    continue
                m = re.match(r'branch\s+(\d+)\s+(taken\s+(\d+)|never executed)', l)
                if m and cur_line:
                    taken = m.group(3) is not None and int(m.group(3)) > 0
                    key = (cur_line, int(m.group(1)))
                    branches[cur][key] = branches[cur].get(key, False) or taken

def pct(a, b):
    return 100.0 * a / b if b else 100.0

rows = []
tl = tc = tb = tt = 0
for f in sorted(lines):
    l_tot = len(lines[f]); l_cov = sum(lines[f].values())
    b_tot = len(branches[f]); b_cov = sum(branches[f].values())
    rows.append((f, l_cov, l_tot, b_cov, b_tot))
    tl += l_tot; tc += l_cov; tb += b_tot; tt += b_cov

w = max(len(r[0]) for r in rows) if rows else 10
print('%-*s  %15s  %17s' % (w, 'File', 'Lines', 'Branches'))
for f, lc, lt, bc, bt in rows:
    print('%-*s  %4d/%-4d %5.1f%%  %4d/%-4d %5.1f%%' % (w, f, lc, lt, pct(lc, lt), bc, bt, pct(bc, bt)))
print('%-*s  %4d/%-4d %5.1f%%  %4d/%-4d %5.1f%%' % (w, 'TOTAL', tc, tl, pct(tc, tl), tt, tb, pct(tt, tb)))

if show_uncovered:
    print('\nUncovered lines:')
    for f in sorted(lines):
        miss = sorted(l for l, c in lines[f].items() if not c)
        for l in miss:
            print('  %s:%d: %s' % (f, l, src_text[f].get(l, '').strip()))

if '--branches' in sys.argv:
    print('\nLines with untaken branches:')
    for f in sorted(branches):
        per = defaultdict(lambda: [0, 0])
        for (ln, n), t in branches[f].items():
            per[ln][1] += 1
            per[ln][0] += t
        for ln in sorted(per):
            t, n = per[ln]
            if t < n:
                print('  %s:%d: %d/%d  %s' % (f, ln, t, n, src_text[f].get(ln, '').strip()))

for a in sys.argv:
    if a.startswith('--dump-covered='):
        with open(a.split('=', 1)[1], 'w') as out:
            for f in sorted(lines):
                for ln in sorted(l for l, c in lines[f].items() if c):
                    out.write('%s:%d\n' % (f, ln))
