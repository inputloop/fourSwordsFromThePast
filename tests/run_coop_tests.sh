#!/bin/sh
# Runs the headless co-op regression tests. Requires zelda3_assets.dat (make).
cd "$(dirname "$0")/.."
mkdir -p tests/out tests/baseline
status=0
# Tests that need a mod this build leaves out are skipped, and say so (mods/SPEC.md, M13).
skip=" $(python3 tools/mods.py skip-tests | tr '\n' ' ') "
skipped() { case $skip in *" $1 "*) echo "SKIP $1 (needs a mod this build leaves out)"; return 0;; esac; return 1; }
# Test vectors for archipelago_logic.txt: the APWorld's logic, to compare with the game's.
python3 archipelago/tools/logic_vectors.py tests/out/ap_logic_vectors.txt 3000 1 > /dev/null || status=1
# Every script in tests/coop runs on its own; the net_* pairs run below.
for f in tests/coop/*.txt; do
  t=$(basename "$f" .txt)
  case $t in net_*) continue;; esac
  skipped "$t" && continue
  if ./zelda3 --mptest "$f" > tests/out/$t.log 2>&1; then echo "PASS $t"; else echo "FAIL $t (see tests/out/$t.log)"; status=1; fi
done
if ! skipped net_client; then
./zelda3 --mptest tests/coop/net_host.txt > tests/out/net_host.log 2>&1 &
host=$!
sleep 1
if ./zelda3 --mptest tests/coop/net_client.txt > tests/out/net_client.log 2>&1; then echo "PASS net_client"; else echo "FAIL net_client"; status=1; fi
if wait $host; then echo "PASS net_host"; else echo "FAIL net_host"; status=1; fi
fi
# The same over a simulated bad connection: 60 ms delay, 30 ms jitter, 3% loss.
if ! skipped net_lag_client; then
export ZELDA3_NETSIM=60,30,3
./zelda3 --mptest tests/coop/net_lag_host.txt > tests/out/net_lag_host.log 2>&1 &
host=$!
sleep 1
if ./zelda3 --mptest tests/coop/net_lag_client.txt > tests/out/net_lag_client.log 2>&1; then echo "PASS net_lag_client"; else echo "FAIL net_lag_client"; status=1; fi
if wait $host; then echo "PASS net_lag_host"; else echo "FAIL net_lag_host"; status=1; fi
unset ZELDA3_NETSIM
fi
# Fights over a lagging link (150 ms and 300 ms each way, set by the scripts'
# netsim command): the worlds stay in sync, nobody's own buttons are guessed,
# and killed enemies stay dead (at most a few undone by rollbacks). Then a
# client that loses its connection, restarts and is removed (net_reconnect).
# Archipelago items and DeathLinks over netplay (net_ap, net_ap_worlds).
# The 3D view's turned d-pad over netplay: the client's Link walks where its
# camera looks and both stay in sync (net_hd3d). "Watch everyone" online:
# both computers draw every player's world and stay in sync (net_multiview).
# Sound online: each computer hears its own player's world, and the host its
# couch player's sound effects too, in sync (net_sound). A guess about the
# other player's buttons never hops down a ledge (net_ledge).
for pair in net_combat net_combat_slow net_reconnect net_ap net_ap_worlds net_hd3d net_multiview net_sound net_ledge; do
  skipped "${pair}_client" && continue
  ./zelda3 --mptest tests/coop/${pair}_host.txt > tests/out/${pair}_host.log 2>&1 &
  host=$!
  sleep 1
  if ./zelda3 --mptest tests/coop/${pair}_client.txt > tests/out/${pair}_client.log 2>&1; then echo "PASS ${pair}_client"; else echo "FAIL ${pair}_client"; status=1; fi
  if wait $host; then echo "PASS ${pair}_host"; else echo "FAIL ${pair}_host"; status=1; fi
done
exit $status
