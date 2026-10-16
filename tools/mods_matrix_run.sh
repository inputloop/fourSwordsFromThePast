#!/bin/sh
# One composition's tests, as TSV rows: NAME  test  PASS|FAIL|TIMEOUT. Run inside a built tree.
name=$1
one() {  # test-name script
  timeout 300 ./zelda3 --mptest "$2" > "tests/out/$1.log" 2>&1; rc=$?
  if [ $rc = 0 ]; then r=PASS; elif [ $rc = 124 ]; then r=TIMEOUT; else r=FAIL; fi
  printf '%s\t%s\t%s\n' "$name" "$1" "$r"
}
# (As run_coop_tests.sh does first: the APWorld's logic as test vectors for archipelago_logic.)
python3 archipelago/tools/logic_vectors.py tests/out/ap_logic_vectors.txt 3000 1 > /dev/null 2>&1
for f in tests/coop/*.txt; do
  t=$(basename "$f" .txt)
  case $t in net_*) continue;; esac
  one "$t" "$f"
done
for pair in net net_lag net_combat net_combat_slow net_reconnect net_ap net_ap_worlds net_hd3d net_multiview net_sound net_ledge; do
  [ $pair = net_lag ] && export ZELDA3_NETSIM=60,30,3
  h=tests/coop/${pair}_host.txt; c=tests/coop/${pair}_client.txt
  timeout 300 ./zelda3 --mptest "$h" > "tests/out/${pair}_host.log" 2>&1 &
  hp=$!; sleep 1
  one "${pair}_client" "$c"
  wait $hp; rc=$?
  if [ $rc = 0 ]; then r=PASS; elif [ $rc = 124 ]; then r=TIMEOUT; else r=FAIL; fi
  printf '%s\t%s\t%s\n' "$name" "${pair}_host" "$r"
  unset ZELDA3_NETSIM
done
