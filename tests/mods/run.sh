#!/bin/sh
# The tests of tools/mods.py, one or more per clause of mods/SPEC.md, written in POSIX sh
# (a second language: the tool is Python, so a bug in how Python reads the spec is not
# copied into its own tests). Each test works in a fixture tree of its own, never the real one.
#
# usage: sh tests/mods/run.sh               run the tests (exit 1 if any fails)
#        sh tests/mods/run.sh --prove-red   plant faults in a copy of the tool; each fault
#                                           must turn at least one test red (exit 1 if not)
# Output: one line per test, "PASS Mn name" or "FAIL Mn name: why".
here=$(cd "$(dirname "$0")/../.." && pwd)
if [ "${1:-}" = "--prove-red" ]; then
  # Each fault is "WHAT|OLD|NEW": a plain-text edit to a copy of the tool.
  pr=$(mktemp -d); status=0
  while IFS='|' read -r what old new; do
    [ -n "$what" ] || continue
    cp "$here/tools/mods.py" "$pr/mods.py"
    python3 - "$pr/mods.py" "$old" "$new" <<'PY' || { echo "NOT CHECKED: $what (the edit did not apply)"; status=1; continue; }
import sys; p, old, new = sys.argv[1:]; s = open(p).read()
if old not in s: sys.exit(1)
open(p, 'w').write(s.replace(old, new, 1))
PY
    if MODS_TOOL="$pr/mods.py" sh "$0" > "$pr/out" 2>&1; then echo "STILL GREEN: $what"; status=1
    else echo "RED as it should be: $what ($(grep -c '^FAIL' "$pr/out") test(s))"; fi
  done <<'FAULTS'
requirements not checked|if not state[r][0]:|if False:
mods.cfg ignored|mine = parse_composition(CFG or os.path.join(root, 'mods.cfg'), names)|mine = {}
header rewritten every time|    if old != text:|    if True:
unknown names accepted|            if name not in names:|            if False:
sim tag always 0|    if all(state[m['name']][0] == default[m['name']] for m in mods if m['sim']):|    if True:
a campaign typo dropped in silence|        print(f'mods: {x}', file=sys.stderr)|        pass
half guards patched|            if fix and not partial:|            if fix:
a byte-order mark refused|encoding='utf-8-sig'|encoding='utf-8'
--cfg ignored|    mine = parse_composition(CFG or os.path.join(root, 'mods.cfg'), names)|    mine = parse_composition(os.path.join(root, 'mods.cfg'), names)
stale default header let through|    if os.path.exists(path) and open(path, encoding='utf-8').read() != default_header(root):|    if False:
FAULTS
  # A fault outside the tool: the guard header that stopped with #error when mods_build.h was missing.
  printf '#ifndef ZELDA3_MODS_H_\n#define ZELDA3_MODS_H_\n#if defined(__has_include)\n#if !__has_include("mods_build.h")\n#error "src/mods_build.h is missing"\n#endif\n#endif\n#include "mods_build.h"\n#endif\n' > "$pr/mods.h"
  if MODS_H="$pr/mods.h" sh "$0" > "$pr/out" 2>&1; then echo "STILL GREEN: the old erroring mods.h"; status=1
  else echo "RED as it should be: the old erroring mods.h ($(grep -c '^FAIL' "$pr/out") test(s))"; fi
  rm -rf "$pr"; exit $status
fi
TOOL=${MODS_TOOL:-$here/tools/mods.py}
fails=0; passes=0
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

fixture() {  # a small tree with three mods: a (sim), b (look), c requires a
  r=$tmp/t$$.$1; rm -rf "$r"; mkdir -p "$r/mods" "$r/src" "$r/tests/coop"
  printf '# comment\na\tyes\t-\ta.c\ta_*\tMod a.\nb\tno\t-\tb1.c b2.c\tb_*\tMod b.\nc\tyes\ta\tc.c\tc_* both\tMod c.\n' > "$r/mods/catalog.tsv"
  printf 'a on\nb on\nc on   # c needs a\n' > "$r/mods/default.cfg"
  for f in a b1 b2 c; do printf 'int %s_x;\n' $f > "$r/src/$f.c"; done
  for t in a_one b_two c_three both plain; do : > "$r/tests/coop/$t.txt"; done
  echo "$r"
}
mods() { python3 "$TOOL" --root "$R" "$@"; }
ok()   { passes=$((passes+1)); echo "PASS $1"; }
bad()  { fails=$((fails+1)); echo "FAIL $1: $2"; }
expect_rc() {  # clause name want-rc cmd...
  c=$1; want=$2; shift 2; out=$("$@" 2>&1); rc=$?
  if [ "$rc" = "$want" ]; then ok "$c"; else bad "$c" "exit $rc, wanted $want: $out"; fi
}
expect_has() {  # clause text cmd...
  c=$1; want=$2; shift 2; out=$("$@" 2>&1)
  case $out in *"$want"*) ok "$c";; *) bad "$c" "no \"$want\" in: $out";; esac
}

R=$(fixture 1)
expect_rc  "M1 the catalog is read"                  0 mods check
expect_has "M2 the default composition shows"        "a  on " mods show
printf -- '-b\n' > "$R/mods.cfg"
expect_has "M3 mods.cfg changes only what it names"  "b  off  (mods.cfg:1)" mods show
expect_has "M3 the rest keep their default"          "a  on   (default)" mods show
printf '\357\273\277-b\n' > "$R/mods.cfg"
expect_rc  "M4 a byte-order mark at the start is fine" 0 mods check
printf 'B off   # upper case\n' > "$R/mods.cfg"
expect_rc  "M4 NAME off, any case"                   0 mods check
printf -- '-b +b\n' > "$R/mods.cfg"
expect_rc  "M8 on and off in one file is refused"    1 mods check
printf -- '-b\n+a\n' > "$R/mods.cfg"
expect_rc  "M4 +NAME and -NAME words"                0 mods check
printf 'zzz on\n' > "$R/mods.cfg"
expect_has "M5 an unknown name is refused, in words" '"zzz" is not a mod' mods check
printf 'b maybe\n' > "$R/mods.cfg"
expect_rc  "M6 a value other than on/off is refused" 1 mods check
printf -- '-a\n' > "$R/mods.cfg"
expect_has "M7 a requirement switched off is refused" "c is on" mods check
printf -- '-a -c\n' > "$R/mods.cfg"
expect_rc  "M7 ...and allowed once both are off"     0 mods check
rm -f "$R/mods.cfg"

R=$(fixture 2); rm "$R/src/b2.c"
expect_has "M9 a missing source is refused"          "src/b2.c does not exist" mods check
R=$(fixture 3); printf 'd\tno\t-\ta.c\t-\tClaims a.c too.\n' >> "$R/mods/catalog.tsv"; printf 'd on\n' >> "$R/mods/default.cfg"
expect_has "M9 a source claimed twice is refused"    "claimed by both" mods check

R=$(fixture 4); mods write > /dev/null
expect_has "M10 a #define per mod"                   "#define MOD_B 1" cat "$R/src/mods_build.h"
expect_has "M10 the shipped composition has tag 0"   "#define MODS_SIM_TAG 0" cat "$R/src/mods_build.h"
printf -- '-b\n' > "$R/mods.cfg"; mods write > /dev/null
expect_has "M10 a look-only change keeps tag 0"      "#define MODS_SIM_TAG 0" cat "$R/src/mods_build.h"
printf -- '-c\n' > "$R/mods.cfg"; mods write > /dev/null
t1=$(sed -n 's/#define MODS_SIM_TAG //p' "$R/src/mods_build.h")
if [ "$t1" -ge 1 ] && [ "$t1" -le 32767 ]; then ok "M10 a game-changing change gives a tag 1-32767"; else bad "M10 tag range" "tag $t1"; fi
printf -- '-c\n-b\n' > "$R/mods.cfg"; mods write > /dev/null
t2=$(sed -n 's/#define MODS_SIM_TAG //p' "$R/src/mods_build.h")
if [ "$t1" = "$t2" ]; then ok "M10 the tag depends only on the sim mods"; else bad "M10 tag depends on look mods" "$t1 vs $t2"; fi
expect_has "M10 MODS_COMPOSITION lists what is on"   '#define MODS_COMPOSITION "a"' cat "$R/src/mods_build.h"
before=$(stat -c %Y.%s "$R/src/mods_build.h"); sleep 1; mods write > /dev/null
after=$(stat -c %Y.%s "$R/src/mods_build.h")
if [ "$before" = "$after" ]; then ok "M11 an unchanged header is left alone"; else bad "M11 rewritten for nothing" "$before -> $after"; fi

R=$(fixture 5); printf -- '-a -c\n' > "$R/mods.cfg"
out=$(mods skip-tests | tr '\n' ' ')
if [ "$out" = "a_one both c_three " ]; then ok "M13 the tests that need an off mod"; else bad "M13 skip-tests" "got: $out"; fi

R=$(fixture 6); printf '* Mods\n#+begin_src mods\n-b   # no 2D-HD on this laptop\n#+end_src\n' > "$R/RECIPE.org"
expect_rc  "M14 from-recipe takes the block"         0 mods from-recipe "$R/RECIPE.org"
expect_has "M14 ...into mods.cfg"                     "-b" cat "$R/mods.cfg"
printf '#+begin_src mods\n+nope\n#+end_src\n' > "$R/BAD.org"
expect_rc  "M14 a bad block is refused"               1 mods from-recipe "$R/BAD.org"
expect_has "M14 ...and mods.cfg is untouched"         "-b" cat "$R/mods.cfg"

R=$(fixture 7); printf 'x.c\n' > /dev/null
expect_rc  "M19 an unguarded source is refused"      1 mods guard
expect_rc  "M19 guard --fix guards them"             0 mods guard --fix
expect_rc  "M19 ...then guard passes"                0 mods guard
expect_has "M19 the guard names the mod"             "#if MOD_B" cat "$R/src/b1.c"
printf '#include "mods.h"\nint half;\n' > "$R/src/a.c"
expect_rc  "M19 half a guard is refused, not patched" 1 mods guard --fix

R=$(fixture 8)
printf '# a campaign\nc4.m.mods ADD b as(off)\nthe laptop is slow\nc4.m.mods ADD zz as(off)\nc4.m.mods ADD a as(on)\n' > "$R/C.assy"
out=$(mods from-assy "$R/C.assy" 2>/dev/null)
if [ "$out" = "-b +a" ]; then ok "M20 a campaign gives +/- words"; else bad "M20 from-assy" "got: $out"; fi
expect_has "M20 a line that is not a mod is said aloud" "kept as a comment" mods from-assy "$R/C.assy"
printf 'text\n#+begin_src mods\nold\n#+end_src\nmore text\n' > "$R/RECIPE.org"
mods from-assy "$R/C.assy" --into "$R/RECIPE.org" > /dev/null 2>&1
want=$(printf 'text\n#+begin_src mods\n-b +a\n#+end_src\nmore text')
if [ "$(cat "$R/RECIPE.org")" = "$want" ]; then ok "M20 --into rewrites only the block"; else bad "M20 --into" "$(cat "$R/RECIPE.org")"; fi
# The second reader: when a C4 tree is at hand, its own tokenizer must see the same acts.
if [ -n "${C4_ROOT:-}" ] && [ -f "$C4_ROOT/h/assy.sh" ]; then
  n=$(sh "$C4_ROOT/h/assy.sh" tokens "$R/C.assy" | awk -F'\t' '$1=="OP" && $3=="as("' | wc -l)
  if [ "$n" = 3 ]; then ok "M20 C4's assy.sh reads the same three as( acts"; else bad "M20 C4 second reader" "$n acts"; fi
fi

R=$(fixture 9); mods write-default > /dev/null
expect_has "M21 write-default holds the shipped composition" "#define MOD_A 1" cat "$R/src/mods_default.h"
expect_rc  "M21 ...and check agrees with it"         0 mods check
printf 'a on\nb off\nc on\n' > "$R/mods/default.cfg"
expect_has "M21 a stale mods_default.h is refused"   "does not match" mods check

R=$(fixture 10); printf -- '-b\n' > "$R/mods.cfg"; printf -- '-a -c\n' > "$R/plat.cfg"
expect_has "M23 --cfg reads the named file"          "a  off  (plat.cfg:1)" python3 "$TOOL" --root "$R" --cfg "$R/plat.cfg" show
expect_has "M23 ...in place of mods.cfg"             "b  on   (default)" python3 "$TOOL" --root "$R" --cfg "$R/plat.cfg" show
expect_rc  "M23 a --cfg file that does not exist is refused" 1 python3 "$TOOL" --root "$R" --cfg "$R/nope.cfg" check
printf -- '-a\n' > "$R/plat.cfg"
expect_has "M23 the requirements hold for it too"    "c is on" python3 "$TOOL" --root "$R" --cfg "$R/plat.cfg" check

# M22: every mod's own file compiles alone in a clean tree (no src/mods_build.h), as a test driver or
# a packager compiles it. Needs cc and sdl2-config; without them this clause is NOT CHECKED, said aloud.
if command -v cc >/dev/null && command -v sdl2-config >/dev/null; then
  C=$tmp/clean; mkdir -p "$C"
  (cd "$here" && tar cf - src snes third_party) | (cd "$C" && tar xf -)
  rm -f "$C/src/mods_build.h"
  [ -n "${MODS_H:-}" ] && cp "$MODS_H" "$C/src/mods.h"
  bad22=""
  for f in $(awk -F'\t' '!/^#/ && NF==6 {print $4}' "$here/mods/catalog.tsv"); do
    (cd "$C" && cc -fsyntax-only -I. $(sdl2-config --cflags) "src/$f" > /dev/null 2>&1) || bad22="$bad22 $f"
  done
  if [ -z "$bad22" ]; then ok "M22 every mod file compiles alone, without mods_build.h"; else bad "M22 compiles alone" "failed:$bad22"; fi
else
  echo "NOT CHECKED M22 (no cc or sdl2-config here)"
fi

echo "$passes passed, $fails failed"
[ "$fails" = 0 ]
