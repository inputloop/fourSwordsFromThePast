#!/bin/sh
# mods_matrix: build the game in several compositions and run every co-op test in
# each, WITHOUT skipping, so which tests need which mod is measured, not guessed.
# usage: tools/mods_matrix.sh OUTDIR [COMPOSITION...]
#   a COMPOSITION is NAME=WORDS, e.g. 'no-hd2d=-hd2d' or 'all-off=-story -hd2d ...'
#   (no compositions given: the shipped one, each mod off alone, and all off)
# Writes OUTDIR/builds.tsv (composition, built, bytes, seconds, mods_on) and
# OUTDIR/tests.tsv (composition, test, result: PASS/FAIL/TIMEOUT).
# Needs zelda3_assets.dat and tests/baseline/ from a normal build and test run.
set -u
out=$1; shift
src=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
[ -f "$out/builds.tsv" ] || printf 'composition\tbuilt\tbytes\tseconds\tmods_on\n' > "$out/builds.tsv"
[ -f "$out/tests.tsv" ] || printf 'composition\ttest\tresult\n' > "$out/tests.tsv"
if [ $# -eq 0 ]; then
  set -- "shipped="
  for m in $(python3 "$src/tools/mods.py" show | awk '$2=="on"{print $1}'); do
    req=$(awk -F'\t' -v m="$m" '!/^#/ && $3 ~ "(^| )"m"( |$)" {printf " -%s", $1}' "$src/mods/catalog.tsv")
    set -- "$@" "no-$m=-$m$req"
  done
  set -- "$@" "all-off=$(python3 "$src/tools/mods.py" show | awk '$2=="on"{printf "-%s ", $1}')"
fi
for comp in "$@"; do
  name=${comp%%=*}; words=${comp#*=}
  w="$out/tree-$name"
  rm -rf "$w"; mkdir -p "$w"
  (cd "$src" && tar cf - --exclude='*.o' --exclude='*.d' --exclude=./zelda3 --exclude='./tests/out' --exclude=./mods.cfg .) | (cd "$w" && tar xf -)
  printf '%s\n' "$words" > "$w/mods.cfg"
  t0=$(date +%s)
  if (cd "$w" && make -j2 zelda3 > build.log 2>&1); then built=yes; else built=no; fi
  bytes=$(stat -c %s "$w/zelda3" 2>/dev/null || echo 0)
  on=$( [ $built = yes ] && "$w/zelda3" --mods 2>/dev/null)
  printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$built" "$bytes" "$(( $(date +%s) - t0 ))" "$on" >> "$out/builds.tsv"
  [ $built = yes ] || continue
  (cd "$w" && mkdir -p tests/out && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    sh "$src/tools/mods_matrix_run.sh" "$name") >> "$out/tests.tsv"
  rm -f "$w"/src/*.o "$w"/snes/*.o "$w"/third_party/*/*.o
done
