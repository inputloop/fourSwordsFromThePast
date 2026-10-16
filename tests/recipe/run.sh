#!/bin/sh
# Tests of RECIPE.org's own check, in a copy of the tree: the check passes on the tree as it is, and
# with a fault planted (a broken catalog, a broken book) it must not say "every check passed", must
# exit non-zero, and must leave mods.cfg as it found it.
# usage: sh tests/recipe/run.sh [--prove-red]
#   --prove-red: also run the check with its first "set -e" removed; the planted faults must then
#   be caught by this test (showing the test, not only the check, can see a false "passed").
here=$(cd "$(dirname "$0")/../.." && pwd)
passes=0; fails=0
ok()  { passes=$((passes+1)); echo "PASS $1"; }
bad() { fails=$((fails+1)); echo "FAIL $1: $2"; }
copy() {  # a copy of the tree, without build output
  t=$(mktemp -d)
  (cd "$here" && tar cf - --exclude='*.o' --exclude='*.d' --exclude=./zelda3 --exclude=./tests/out --exclude=./help --exclude=./.git .) | (cd "$t" && tar xf -)
  echo "$t"
}
run_check() {  # tree, sabotage-the-set-e? -> prints output; sets RC
  awk -f "$1/tools/unfold.awk" "$1/RECIPE.org" > "$1/check.sh"
  [ "${2:-}" = weak ] && sed -i '0,/^set -e/{/^set -e/d}' "$1/check.sh"
  OUT=$(cd "$1" && sh check.sh 2>&1); RC=$?
}
mode=${1:-}
T=$(copy); run_check "$T" "$( [ "$mode" = --prove-red ] && echo weak)"
case $OUT in *"recipe: every check passed"*) ok "the tree as it is passes its own check";; *) bad "the tree passes" "$OUT";; esac
rm -rf "$T"
for fault in catalog book; do
  T=$(copy); printf 'my own choice\n' > "$T/mods.cfg"
  case $fault in
    catalog) printf 'nonsense line without tabs\n' >> "$T/mods/catalog.tsv";;
    book) sed -i 's/Turn to §2\./Turn to §99./' "$T/docs/books/manual.md";;
  esac
  run_check "$T" "$( [ "$mode" = --prove-red ] && echo weak)"
  case $OUT in *"recipe: every check passed"*) bad "a broken $fault is not called passed" "it said every check passed";; *) ok "a broken $fault is not called passed";; esac
  [ "$RC" != 0 ] && ok "a broken $fault makes the check exit non-zero" || bad "a broken $fault exits non-zero" "exit 0"
  [ "$(cat "$T/mods.cfg" 2>/dev/null)" = "my own choice" ] && ok "a broken $fault leaves mods.cfg as it was" || bad "mods.cfg kept" "$(cat "$T/mods.cfg" 2>/dev/null)"
  rm -rf "$T"
done
echo "$passes passed, $fails failed"
if [ "$mode" = --prove-red ]; then
  # With set -e removed the false "passed" comes back; the test must see it.
  if [ "$fails" -gt 0 ]; then echo "RED as it should be: without set -e, a broken tree is called passed ($fails test(s))"; exit 0
  else echo "STILL GREEN: the test cannot tell a weak check from a strong one"; exit 1; fi
fi
[ "$fails" = 0 ]
