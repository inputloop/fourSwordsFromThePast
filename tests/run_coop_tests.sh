#!/bin/sh
# Runs the headless co-op regression tests. Requires zelda3_assets.dat (make).
cd "$(dirname "$0")/.."
mkdir -p tests/out tests/baseline
status=0
# Every script in tests/coop runs on its own; the net_* pairs run below.
for f in tests/coop/*.txt; do
  t=$(basename "$f" .txt)
  case $t in net_*) continue;; esac
  if ./zelda3 --mptest "$f" > tests/out/$t.log 2>&1; then echo "PASS $t"; else echo "FAIL $t (see tests/out/$t.log)"; status=1; fi
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
# Fights over a lagging link (150 ms and 300 ms each way, set by the scripts'
# netsim command): the worlds stay in sync, nobody's own buttons are guessed,
# and killed enemies stay dead (at most a few undone by rollbacks). Then a
# client that loses its connection, restarts and is removed (net_reconnect).
for pair in net_combat net_combat_slow net_reconnect; do
  ./zelda3 --mptest tests/coop/${pair}_host.txt > tests/out/${pair}_host.log 2>&1 &
  host=$!
  sleep 1
  if ./zelda3 --mptest tests/coop/${pair}_client.txt > tests/out/${pair}_client.log 2>&1; then echo "PASS ${pair}_client"; else echo "FAIL ${pair}_client"; status=1; fi
  if wait $host; then echo "PASS ${pair}_host"; else echo "FAIL ${pair}_host"; status=1; fi
done
exit $status
