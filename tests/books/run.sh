#!/bin/sh
# Tests of tools/books.py check, in POSIX sh (the tool is Python). Each test writes a small
# game book into a fixture tree and expects the check to accept or refuse it.
# usage: sh tests/books/run.sh [--prove-red]
#   --prove-red: plant faults in a copy of the tool; each must turn a test red.
here=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${BOOKS_TOOL:-$here/tools/books.py}
if [ "${1:-}" = "--prove-red" ]; then
  pr=$(mktemp -d); status=0
  while IFS='|' read -r what old new; do
    [ -n "$what" ] || continue
    cp "$TOOL" "$pr/books.py"
    python3 - "$pr/books.py" "$old" "$new" <<'PY' || { echo "NOT CHECKED: $what"; status=1; continue; }
import sys; p, old, new = sys.argv[1:]; s = open(p).read()
if old not in s: sys.exit(1)
open(p, 'w').write(s.replace(old, new, 1))
PY
    if BOOKS_TOOL="$pr/books.py" sh "$0" > "$pr/out" 2>&1; then echo "STILL GREEN: $what"; status=1
    else echo "RED as it should be: $what ($(grep -c '^FAIL' "$pr/out") test(s))"; fi
  done <<'FAULTS'
a route to nowhere let through|            if r not in have:|            if False:
an unreachable section let through|    for n in sorted(have - seen):|    for n in []:
a stranding section let through|        if not s['refs'] and not s['end']:|        if False:
a missing help page let through|            if t not in pages:|            if False:
numbering not checked|    if nums != list(range(1, len(nums) + 1)):|    if False:
FAULTS
  rm -rf "$pr"; exit $status
fi
passes=0; fails=0
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/tools" "$tmp/docs/books" "$tmp/src"
cp "$TOOL" "$tmp/tools/books.py"
printf 'const char *Helpbook_Topic(void) {\n  if (x) return "menus";\n  return "outdoors";\n}\n' > "$tmp/src/helpbook.c"
book() {  # name, then the sections' text on stdin
  { printf '<!-- GB:book title="T" author="A" -->\n# T\n'; cat; } > "$tmp/docs/books/$1.md"
}
expect() {  # want-rc name what
  python3 "$tmp/tools/books.py" check manual > "$tmp/out" 2>&1; rc=$?
  if [ "$rc" = "$1" ]; then passes=$((passes+1)); echo "PASS $2"
  else fails=$((fails+1)); echo "FAIL $2: exit $rc: $(cat "$tmp/out")"; fi
}
good='## §1 · One
<!-- page: menus -->
Turn to §2.
## §2 · Two
<!-- page: outdoors -->
Back to §1, or §3.
## §3 · Three
<!-- end -->
The end.'
printf '%s\n' "$good" | book manual;                                  expect 0 "a sound book passes"
printf '%s\n' "$good" | sed 's/Turn to §2./Turn to §2 or §3./; s/Back to §1, or §3./Back to §1, or §9./' | book manual; expect 1 "a route to a missing section is refused (and only that)"
printf '%s\n' "$good" | sed 's/, or §3//' | book manual;               expect 1 "a section nobody can reach is refused"
printf '%s\n' "$good" | sed '/<!-- end -->/d' | book manual;           expect 1 "a section that strands its reader is refused"
printf '%s\n' "$good" | sed 's/page: outdoors/page: elsewhere/' | book manual; expect 1 "a page the help key needs, missing, is refused"
printf '%s\n' "$good" | sed 's/§3 · Three/§4 · Three/; s/or §3/or §4/' | book manual; expect 1 "a gap in the numbering is refused"
printf '%s\n' "$good" | sed 's/page: menus/page: outdoors/' | book manual; expect 1 "a page name used twice is refused"
echo "$passes passed, $fails failed"
[ "$fails" = 0 ]
