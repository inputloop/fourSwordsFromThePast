#!/usr/bin/env python3
"""unfold.py: the recipe's second reader. Prints the body of every '#+begin_src sh :tangle check'
block of an Org file, in order; must agree byte for byte with tools/unfold.awk.
usage: python3 tools/unfold.py RECIPE.org"""
import re, sys
out, inb = [], False
for line in open(sys.argv[1], encoding='utf-8'):
    if not inb and re.match(r'^[ \t]*#\+begin_src[ \t]+sh[ \t]+.*:tangle[ \t]+check([ \t]|$)', line):
        inb = True
    elif inb and re.match(r'^[ \t]*#\+end_src', line):
        inb = False
    elif inb:
        out.append(line if line.endswith('\n') else line + '\n')
sys.stdout.write(''.join(out))
