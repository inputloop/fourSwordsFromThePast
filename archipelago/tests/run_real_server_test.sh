#!/bin/sh
# End-to-end test of the game's Archipelago client against the real server:
# generates a two-player ChecksFinder multiworld, runs MultiServer.py, and
# drives two clients (ap_client_test.c) through checks, items, the goal and
# DeathLink, over ws:// and then wss:// (a self-signed test CA).
#
#   archipelago/tests/run_real_server_test.sh AP_ROOT [PYTHON]
#
# AP_ROOT: an Archipelago 0.6 source tree (Generate.py, MultiServer.py and
# worlds/checksfinder). PYTHON: one with Archipelago's requirements (default
# python3). AP_TEST_PORT: the server's port (default 38511).
set -u
cd "$(dirname "$0")/../.."
REPO=$(pwd)
AP=$(cd "$1" && pwd) || exit 1
PY=${2:-python3}
PORT=${AP_TEST_PORT:-38511}
OUT=$(mktemp -d)
SERVER_PID=
stop_server() {
  [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null && wait "$SERVER_PID" 2>/dev/null
  SERVER_PID=
}
trap 'stop_server; rm -rf "$OUT"' EXIT
export SKIP_REQUIREMENTS_UPDATE=1

echo "Building the test driver"
# shellcheck disable=SC2046
cc -O2 -Werror -I. $(sdl2-config --cflags) archipelago/tests/ap_client_test.c src/ap_client.c src/ap_ws.c \
  src/ap_tls.c src/ap_json.c $(sdl2-config --libs) -lm -o "$OUT/ap_client_test" || exit 1
T=$OUT/ap_client_test

echo "Generating a ChecksFinder multiworld for Player1 and Player2"
mkdir -p "$OUT/players"
for p in Player1 Player2; do
  printf "name: %s\ngame: ChecksFinder\nChecksFinder: {}\n" $p > "$OUT/players/$p.yaml"
done
(cd "$AP" && "$PY" Generate.py --player_files_path "$OUT/players" --outputpath "$OUT/gen" --seed 1234) \
  > "$OUT/generate.log" 2>&1 || { cat "$OUT/generate.log"; exit 1; }
MULTIDATA=$(ls "$OUT"/gen/AP_*.zip 2>/dev/null | head -n 1)
[ -n "$MULTIDATA" ] || { echo "no multiworld was generated"; cat "$OUT/generate.log"; exit 1; }

# The server's console reads stdin: give it one that never ends.
mkfifo "$OUT/stdin"
exec 3<>"$OUT/stdin"

start_server() {  # extra MultiServer arguments
  (cd "$AP" && exec "$PY" MultiServer.py --host 0.0.0.0 --port "$PORT" --disable_save "$@" "$MULTIDATA") \
    <&3 > "$OUT/server.log" 2>&1 &
  SERVER_PID=$!
  for _ in $(seq 150); do
    grep -q "server listening" "$OUT/server.log" 2>/dev/null && return 0
    sleep 0.2
  done
  echo "the server didn't start:"; cat "$OUT/server.log"
  return 1
}

FAILED=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; [ -f "$OUT/$1.log" ] && cat "$OUT/$1.log"; FAILED=$((FAILED + 1)); }

# Runs a driver in the background; finish NAME waits for it.
background() {  # background NAME COMMAND...
  name=$1; shift
  "$@" > "$OUT/$name.log" 2>&1 &
  eval "PID_$(echo "$name" | tr -c 'a-z0-9\n' _)=\$!"
}
finish() {
  eval "pid=\$PID_$(echo "$1" | tr -c 'a-z0-9\n' _)"
  if wait "$pid"; then pass "$1"; else fail "$1"; fi
}
check() {  # check NAME COMMAND...
  name=$1; shift
  if "$@" > "$OUT/$name.log" 2>&1; then pass "$name"; else fail "$name"; fi
}

# Player1 checks all its locations and reaches the goal while Player2 waits
# for items from Player1's world; then Player1 dies and Player2 dies along.
play() {  # play LABEL ADDRESS_FOR_PLAYER1 ADDRESS_FOR_PLAYER2
  label=$1
  background "$label-items" "$T" real-items "$3" Player2 ChecksFinder 1
  sleep 1
  # shellcheck disable=SC2046
  check "$label-checks" "$T" real-checks "$2" Player1 ChecksFinder $(seq 81000 81024)
  finish "$label-items"
  if grep -q "Player1 (Team #1) has completed their goal" "$OUT/server.log"; then
    pass "$label-goal"
  else
    fail "$label-goal"
  fi
  background "$label-death-wait" "$T" real-death-wait "$3" Player2 ChecksFinder Player1
  sleep 1
  check "$label-death-send" "$T" real-death-send "$2" Player1 ChecksFinder
  finish "$label-death-wait"
}

echo "ws://"
start_server || exit 1
play ws "ws://127.0.0.1:$PORT" "127.0.0.1:$PORT"
grep -E "has joined|completed|DeathLink" "$OUT/server.log" | head -n 6
stop_server

echo "wss:// (a test CA; Player2's address has no scheme, so ws:// is tried first)"
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=AP Test CA" -keyout "$OUT/ca.key" -out "$OUT/ca.pem" \
  -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null || exit 1
openssl req -newkey rsa:2048 -nodes -subj "/CN=localhost" -keyout "$OUT/leaf.key" -out "$OUT/leaf.csr" 2>/dev/null
printf "subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\n" > "$OUT/leaf.ext"
openssl x509 -req -in "$OUT/leaf.csr" -CA "$OUT/ca.pem" -CAkey "$OUT/ca.key" -CAcreateserial -days 2 \
  -extfile "$OUT/leaf.ext" -out "$OUT/leaf.pem" 2>/dev/null || exit 1
start_server --cert "$OUT/leaf.pem" --cert_key "$OUT/leaf.key" || exit 1
export ZELDA3_AP_CAFILE="$OUT/ca.pem"
play wss "wss://localhost:$PORT" "localhost:$PORT"
if grep -q "(wss) as Player2" "$OUT/wss-items.log"; then pass wss-fallback; else fail wss-fallback; fi
stop_server

if [ $FAILED -eq 0 ]; then echo "All real-server tests passed"; else echo "$FAILED real-server test(s) failed"; fi
exit $FAILED
