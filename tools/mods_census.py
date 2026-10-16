#!/usr/bin/env python3
"""mods_census: what each mod defines that the rest of the program uses (its surface), measured
from the compiled objects' symbol tables, not guessed. Run it after a build with every mod on.

usage: python3 tools/mods_census.py [GROUPS.tsv] [OBJDIR...]
  With no GROUPS.tsv, the groups are the mods of mods/catalog.tsv (each mod's sources as .o).
  OBJDIR defaults to src and snes.
Output, TSV: group  symbol  kind  user_objects   (kind: nm's letter, T code, D/B data)
Counts only; it says nothing about what a symbol means (devguide §7 and §15)."""
import collections, glob, os, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

def groups_from_catalog():
    g = {}
    for line in open(os.path.join(ROOT, 'mods', 'catalog.tsv'), encoding='utf-8'):
        if not line.strip() or line.startswith('#'):
            continue
        f = line.rstrip('\n').split('\t')
        for s in f[3].split():
            g[s[:-2] + '.o'] = f[0]
    return g

def groups_from_file(path):
    g = {}
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        if line and not line.startswith('#'):
            name, objs = line.split('\t', 1)
            for o in objs.split():
                g[o] = name
    return g

def main(argv):
    if argv and argv[0].endswith('.tsv'):
        groups, argv = groups_from_file(argv[0]), argv[1:]
    else:
        groups = groups_from_catalog()
    dirs = argv or [os.path.join(ROOT, 'src'), os.path.join(ROOT, 'snes')]
    defs, uses = {}, collections.defaultdict(set)
    for p in (p for d in dirs for p in glob.glob(os.path.join(d, '*.o'))):
        b = os.path.basename(p)
        for l in subprocess.run(['nm', '-P', p], capture_output=True, text=True).stdout.splitlines():
            f = l.split()
            if len(f) < 2:
                continue
            if f[1] == 'U':
                uses[f[0]].add(b)
            elif f[1].isupper():
                defs[f[0]] = (b, f[1])
    print('group\tsymbol\tkind\tuser_objects')
    for sym, (b, kind) in sorted(defs.items()):
        g = groups.get(b)
        outside = sorted(u for u in uses.get(sym, ()) if groups.get(u) != g) if g else []
        if outside:
            print(f'{g}\t{sym}\t{kind}\t{" ".join(outside)}')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
