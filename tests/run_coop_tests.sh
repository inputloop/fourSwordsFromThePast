#!/bin/sh
# Runs the headless co-op regression tests. Requires zelda3_assets.dat (make).
cd "$(dirname "$0")/.."
mkdir -p tests/out
status=0
for t in local_coop combat_revive friendly_fire group_exits fall_holes free_roam roam_downed roam_wander fairy ghost_world game_over; do
  if ./zelda3 --mptest tests/coop/$t.txt > tests/out/$t.log 2>&1; then echo "PASS $t"; else echo "FAIL $t (see tests/out/$t.log)"; status=1; fi
done
./zelda3 --mptest tests/coop/net_host.txt > tests/out/net_host.log 2>&1 &
host=$!
sleep 1
if ./zelda3 --mptest tests/coop/net_client.txt > tests/out/net_client.log 2>&1; then echo "PASS net_client"; else echo "FAIL net_client"; status=1; fi
if wait $host; then echo "PASS net_host"; else echo "FAIL net_host"; status=1; fi
exit $status
