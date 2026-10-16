#!/usr/bin/env python3
"""Build-time mods for Four Swords of the Past. The rules are in mods/SPEC.md (clauses M1-M18).

usage: python3 tools/mods.py [--root DIR] COMMAND
  write              make src/mods_build.h from the composition (M10, M11)
  write-default      make src/mods_default.h: the shipped composition, for builds without Python (M21)
  show               the composition in words (M12)
  check              only check the composition (M5-M9); exit 1 if it is wrong
  skip-tests         tests/coop names that need a mod that is off (M13)
  from-recipe FILE   copy the first '#+begin_src mods' block of an Org file to mods.cfg (M14)
  guard [--fix]      check (or put in place) the guard around every mod source file (M19)
  from-assy FILE [--into ORG]
                     read a C4 CAMPAIGN (.assy) of mod lines; print the composition, or write it
                     into the mods block of ORG (M20)
"""
import fnmatch, os, re, sys, zlib

class Refusal(Exception):
    pass

def read_catalog(root):  # M1
    mods, owners = [], {}
    path = os.path.join(root, 'mods', 'catalog.tsv')
    for n, line in enumerate(open(path, encoding='utf-8'), 1):
        if not line.strip() or line.startswith('#'):
            continue
        f = line.rstrip('\n').split('\t')
        if len(f) != 6:
            raise Refusal(f'{path}:{n}: needs 6 columns separated by tabs, has {len(f)}')
        name, sim, req, srcs, tests, summary = f
        m = dict(name=name.lower(), sim=sim == 'yes',
                 requires=[] if req == '-' else req.lower().split(),
                 sources=[] if srcs == '-' else srcs.split(),
                 tests=[] if tests == '-' else tests.split(), summary=summary)
        for s in m['sources']:  # M9
            if not os.path.exists(os.path.join(root, 'src', s)):
                raise Refusal(f'{path}:{n}: {name}: src/{s} does not exist')
            if s in owners:
                raise Refusal(f'{path}:{n}: src/{s} is claimed by both {owners[s]} and {name}')
            owners[s] = name
        mods.append(m)
    names = {m['name'] for m in mods}
    for m in mods:
        for r in m['requires']:
            if r not in names:
                raise Refusal(f"catalog: {m['name']} requires {r}, which is not a mod")
    return mods

def parse_composition(path, names):  # M4, M5, M6, M8
    got = {}
    if not os.path.exists(path):
        return got
    for n, raw in enumerate(open(path, encoding='utf-8-sig'), 1):  # (-sig: a BOM from an old Notepad is fine)
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        words = line.split()
        pairs = []
        if all(w[0] in '+-' for w in words):
            pairs = [(w[1:], 'on' if w[0] == '+' else 'off') for w in words]
        elif len(words) == 2:
            pairs = [(words[0], words[1].lower())]
        else:
            raise Refusal(f'{path}:{n}: cannot read "{line}": write "NAME on", "NAME off", "+NAME" or "-NAME"')
        for name, val in pairs:
            name = name.lower()
            if name not in names:
                raise Refusal(f'{path}:{n}: "{name}" is not a mod. The mods are: {", ".join(names)}')
            if val not in ('on', 'off'):
                raise Refusal(f'{path}:{n}: {name}: "{val}" should be on or off')
            if name in got and got[name][0] != (val == 'on'):
                raise Refusal(f'{path}:{n}: {name} is switched both on and off in this file')
            got[name] = (val == 'on', f'{os.path.basename(path)}:{n}')
    return got

def compose(root):
    mods = read_catalog(root)
    names = [m['name'] for m in mods]
    default = parse_composition(os.path.join(root, 'mods', 'default.cfg'), names)
    for nm in names:
        if nm not in default:
            raise Refusal(f'mods/default.cfg does not say whether {nm} is on or off')
    mine = parse_composition(os.path.join(root, 'mods.cfg'), names)
    state = {nm: (mine.get(nm) or default[nm]) for nm in names}
    for m in mods:  # M7
        if state[m['name']][0]:
            for r in m['requires']:
                if not state[r][0]:
                    raise Refusal(f"{m['name']} is on ({state[m['name']][1]}) but needs {r}, which is off "
                                  f"({state[r][1]}). Switch {r} on, or {m['name']} off.")
    return mods, state, {nm: default[nm][0] for nm in names}

def sim_tag(mods, state, default):  # M10
    if all(state[m['name']][0] == default[m['name']] for m in mods if m['sim']):
        return 0
    key = ' '.join(m['name'] for m in mods if m['sim'] and state[m['name']][0])
    return zlib.crc32(('sim:' + key).encode()) % 32767 + 1

def header(mods, state, default):
    on = [m['name'] for m in mods if state[m['name']][0]]
    out = ['// Made by tools/mods.py from mods/catalog.tsv, mods/default.cfg and mods.cfg. Do not edit.',
           '#ifndef ZELDA3_MODS_BUILD_H_', '#define ZELDA3_MODS_BUILD_H_']
    out += [f"#define MOD_{m['name'].upper()} {int(state[m['name']][0])}" for m in mods]
    out += [f'#define MODS_COMPOSITION "{" ".join(on)}"',
            f'#define MODS_SIM_TAG {sim_tag(mods, state, default)}', '#endif', '']
    return '\n'.join(out)

def write(root):  # M10, M11
    mods, state, default = compose(root)
    check_default(root)
    text = header(mods, state, default)
    path = os.path.join(root, 'src', 'mods_build.h')
    old = open(path, encoding='utf-8').read() if os.path.exists(path) else None
    if old != text:
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        print(f'mods: wrote src/mods_build.h ({sum(s[0] for s in state.values())} of {len(mods)} mods on)')

def default_header(root):
    mods, _, default = compose(root)
    state = {m['name']: (default[m['name']], 'default') for m in mods}
    return header(mods, state, default).replace(
        '// Made by tools/mods.py from mods/catalog.tsv, mods/default.cfg and mods.cfg. Do not edit.',
        '// The shipped composition, made by tools/mods.py write-default from mods/catalog.tsv and\n'
        '// mods/default.cfg. Used only where Python is not at hand (run_with_tcc.bat). Do not edit.'
    ).replace('ZELDA3_MODS_BUILD_H_', 'ZELDA3_MODS_DEFAULT_H_')

def write_default(root):  # M21
    with open(os.path.join(root, 'src', 'mods_default.h'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(default_header(root))
    print('mods: wrote src/mods_default.h')

def check_default(root):  # M21
    path = os.path.join(root, 'src', 'mods_default.h')
    if os.path.exists(path) and open(path, encoding='utf-8').read() != default_header(root):
        raise Refusal('src/mods_default.h does not match mods/default.cfg: run python3 tools/mods.py write-default')

def show(root):  # M12
    mods, state, default = compose(root)
    w = max(len(m['name']) for m in mods)
    for m in mods:
        on, src = state[m['name']]
        src = 'default' if src.startswith('default.cfg') else src
        print(f"{m['name']:<{w}}  {'on ' if on else 'off'}  ({src})  {m['summary']}")
    tag = sim_tag(mods, state, default)
    print('online: ' + ('plays with the shipped build' if tag == 0 else
                        f'plays only with builds that have the same game-changing mods (tag {tag})'))

def skip_tests(root):  # M13
    mods, state, _ = compose(root)
    tests = sorted(f[:-4] for f in os.listdir(os.path.join(root, 'tests', 'coop')) if f.endswith('.txt'))
    for t in tests:
        if any(not state[m['name']][0] and any(fnmatch.fnmatch(t, g) for g in m['tests']) for m in mods):
            print(t)

def from_recipe(root, org):  # M14
    text = open(org, encoding='utf-8').read()
    m = re.search(r'^[ \t]*#\+begin_src[ \t]+mods\b[^\n]*\n(.*?)^[ \t]*#\+end_src', text, re.M | re.S | re.I)
    if not m:
        raise Refusal(f'{org}: no "#+begin_src mods" block')
    body = m.group(1)
    names = [x['name'] for x in read_catalog(root)]
    tmp = os.path.join(root, 'mods.cfg.new')
    with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'# From {os.path.basename(org)} by tools/mods.py from-recipe.\n' + body)
    try:
        parse_composition(tmp, names)
        os.replace(tmp, os.path.join(root, 'mods.cfg'))
        compose(root)
    except Refusal:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    print(f'mods: mods.cfg now holds the block from {org}')

ASSY_LINE = re.compile(r'^\s*(?P<ns>[A-Za-z0-9_.-]*\bmods)\s+ADD\s+(?P<name>[A-Za-z0-9_-]+)\s+as\((?P<val>[^)]*)\)')

def from_assy(root, assy, into=None):  # M20
    """C4-LANG has no error state: a line that does not resolve is a comment, kept. So a line
    naming something that is not a mod is not refused here; it is said aloud and kept."""
    names = [x['name'] for x in read_catalog(root)]
    words, said = [], []
    for n, line in enumerate(open(assy, encoding='utf-8'), 1):
        if line.lstrip().startswith('#'):
            continue
        m = ASSY_LINE.match(line)
        if not m:
            continue
        name, val = m.group('name').lower(), m.group('val').strip().lower()
        if name not in names or val not in ('on', 'off'):
            said.append(f'{assy}:{n}: kept as a comment (not a mod, or not on/off): {line.strip()}')
            continue
        words.append(('+' if val == 'on' else '-') + name)
    for x in said:
        print(f'mods: {x}', file=sys.stderr)
    block = ' '.join(words)
    if not into:
        print(block)
        return
    text = open(into, encoding='utf-8').read()
    pat = re.compile(r'(^[ \t]*#\+begin_src[ \t]+mods\b[^\n]*\n)(.*?)(^[ \t]*#\+end_src)', re.M | re.S | re.I)
    if not pat.search(text):
        raise Refusal(f'{into}: no "#+begin_src mods" block to write into')
    text = pat.sub(lambda m: m.group(1) + block + '\n' + m.group(3), text, count=1)
    with open(into, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print(f'mods: the mods block of {into} is now: {block or "(empty: the shipped composition)"}')

GUARD_HEAD = '#include "mods.h"  // (mods/SPEC.md: this file is the {name} mod)\n#if MOD_{NAME}\n'
GUARD_TAIL = '#endif  // MOD_{NAME}\n'

def guard(root, fix):  # M19
    bad = 0
    for m in read_catalog(root):
        for s in m['sources']:
            path = os.path.join(root, 'src', s)
            text = open(path, encoding='utf-8', newline='').read()
            head = GUARD_HEAD.format(name=m['name'], NAME=m['name'].upper())
            tail = GUARD_TAIL.format(NAME=m['name'].upper())
            ok = text.startswith(head) and text.endswith(tail)
            if ok:
                continue
            partial = '#include "mods.h"' in text or f"MOD_{m['name'].upper()}" in text
            if fix and not partial:
                body = text if text.endswith('\n') else text + '\n'
                with open(path, 'w', encoding='utf-8', newline='') as f:
                    f.write(head + body + tail)
                print(f'mods: guarded src/{s}')
            else:
                print(f'mods: src/{s} is not guarded as the {m["name"]} mod (run: python3 tools/mods.py guard --fix)')
                bad += 1
    if bad:
        raise Refusal(f'{bad} source file(s) without their guard')

def main(argv):
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    if len(argv) >= 2 and argv[0] == '--root':
        root, argv = argv[1], argv[2:]
    cmd = argv[0] if argv else 'show'
    try:
        if cmd == 'write': write(root)
        elif cmd == 'show': show(root)
        elif cmd == 'check': compose(root); check_default(root); print('mods: the composition is good')
        elif cmd == 'write-default': write_default(root)
        elif cmd == 'skip-tests': skip_tests(root)
        elif cmd == 'from-recipe' and len(argv) == 2: from_recipe(root, argv[1])
        elif cmd == 'guard': guard(root, '--fix' in argv[1:])
        elif cmd == 'from-assy' and len(argv) in (2, 4) and (len(argv) == 2 or argv[2] == '--into'):
            from_assy(root, argv[1], argv[3] if len(argv) == 4 else None)
        else:
            print(__doc__, file=sys.stderr); return 2
    except Refusal as e:
        print(f'mods: {e}', file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
