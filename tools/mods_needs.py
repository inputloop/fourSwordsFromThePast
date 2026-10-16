#!/usr/bin/env python3
"""mods_needs: which tests really need which mod, read from a mods_matrix run, set against the
catalog's `tests` column. Data in, data out; it changes nothing.

usage: python3 tools/mods_needs.py MATRIX_DIR
Output, TSV: test  mod  finding  evidence
  finding  NEEDED-UNTAGGED  fails with the mod off, passes shipped; the catalog does not tag it
           TAGGED-NEEDED    fails with the mod off; tagged (the catalog was right)
           TAGGED-UNNEEDED  tagged, but passes with the mod off (the tag skips a test that could run)
           SHIPPED-FAILS    fails even in the shipped build (not a mod question; read its log)
A composition is named no-MOD (MOD off, plus any mods that require it) as tools/mods_matrix.sh names them.
"""
import csv, fnmatch, os, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

def main(argv):
    if len(argv) != 1:
        print(__doc__, file=sys.stderr); return 2
    res = {}
    for r in csv.DictReader(open(os.path.join(argv[0], 'tests.tsv')), delimiter='\t'):
        res.setdefault(r['composition'], {})[r['test']] = r['result']
    tags = {}
    for line in open(os.path.join(ROOT, 'mods', 'catalog.tsv'), encoding='utf-8'):
        if line.strip() and not line.startswith('#'):
            f = line.rstrip('\n').split('\t')
            tags[f[0]] = [] if f[4] == '-' else f[4].split()
    shipped = res.get('shipped', {})
    print('test\tmod\tfinding\tevidence')
    for t, r in sorted(shipped.items()):
        if r != 'PASS':
            print(f'{t}\t-\tSHIPPED-FAILS\tshipped:{r}')
    for comp, tests in sorted(res.items()):
        if not comp.startswith('no-'):
            continue
        mod = comp[3:]
        for t, r in sorted(tests.items()):
            if shipped.get(t) != 'PASS':
                continue
            tagged = any(fnmatch.fnmatch(t, g) for g in tags.get(mod, []))
            if r != 'PASS':
                print(f'{t}\t{mod}\t{"TAGGED-NEEDED" if tagged else "NEEDED-UNTAGGED"}\t{comp}:{r}')
            elif tagged:
                print(f'{t}\t{mod}\tTAGGED-UNNEEDED\t{comp}:PASS')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
