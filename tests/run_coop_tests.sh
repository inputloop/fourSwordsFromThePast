#!/bin/sh
# Runs the headless co-op regression tests. Requires zelda3_assets.dat (make).
cd "$(dirname "$0")/.."
mkdir -p tests/out
status=0
for t in local_coop combat_revive friendly_fire group_exits fall_holes free_roam roam_downed roam_wander fairy ghost_world ghost_textbox ghost_follower game_over randomizer randomizer_sword randomizer_sword0 randomizer_uncle randomizer_open randomizer_oldsave randomizer_off story story_off; do
  if ./zelda3 --mptest tests/coop/$t.txt > tests/out/$t.log 2>&1; then echo "PASS $t"; else echo "FAIL $t (see tests/out/$t.log)"; status=1; fi
done
./zelda3 --mptest tests/coop/net_host.txt > tests/out/net_host.log 2>&1 &
host=$!
sleep 1
if ./zelda3 --mptest tests/coop/net_client.txt > tests/out/net_client.log 2>&1; then echo "PASS net_client"; else echo "FAIL net_client"; status=1; fi
if wait $host; then echo "PASS net_host"; else echo "FAIL net_host"; status=1; fi
# The same over a simulated bad connection: 60 ms delay, 30 ms jitter, 3% loss.
export ZELDA3_NETSIM=60,30,3
./zelda3 --mptest tests/coop/net_lag_host.txt > tests/out/net_lag_host.log 2>&1 &
host=$!
sleep 1
if ./zelda3 --mptest tests/coop/net_lag_client.txt > tests/out/net_lag_client.log 2>&1; then echo "PASS net_lag_client"; else echo "FAIL net_lag_client"; status=1; fi
if wait $host; then echo "PASS net_lag_host"; else echo "FAIL net_lag_host"; status=1; fi
unset ZELDA3_NETSIM
exit $status
