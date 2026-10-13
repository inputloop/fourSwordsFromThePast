#!/bin/sh
# Tests the game's Archipelago client (src/ap_client.c, ap_ws.c, ap_tls.c)
# against mock_server.py: builds archipelago/tests/ap_client_test.c with the
# client sources and runs every scenario. Exit code: number of failures.
#
#   archipelago/tests/run_client_tests.sh [scenario...]
#
# PYTHON: a Python with websockets 13 (default: python3).
# AP_TEST_CFLAGS: compiler flags for the Linux driver (default: -O2 -Werror), e.g.
#   "-g -fsanitize=address,undefined".
# AP_TEST_WINDOWS_SDL: the MinGW SDL2 directory (the one holding include/,
#   lib/ and bin/SDL2.dll, e.g. build-windows/SDL2-2.28.2/x86_64-w64-mingw32):
#   also builds the driver for Windows and runs it under wine (Schannel).
set -u
cd "$(dirname "$0")/../.."
PYTHON=${PYTHON:-python3}
OUT=$(mktemp -d)
trap '[ -n "$MOCK_PID" ] && kill $MOCK_PID 2>/dev/null; rm -rf "$OUT"' EXIT
MOCK_PID=

if ! "$PYTHON" -c "import websockets" 2>/dev/null; then
  echo "FAIL: $PYTHON can't import websockets (pip install websockets==13.1, or set PYTHON)"
  exit 1
fi

SRCS="archipelago/tests/ap_client_test.c src/ap_client.c src/ap_ws.c src/ap_tls.c src/ap_json.c"
echo "Building the test driver"
# shellcheck disable=SC2046
cc ${AP_TEST_CFLAGS:--O2 -Werror} -I. $(sdl2-config --cflags) $SRCS $(sdl2-config --libs) -lm -o "$OUT/ap_client_test" || exit 1

RUNNERS="linux"
if [ -n "${AP_TEST_WINDOWS_SDL:-}" ]; then
  echo "Building the test driver for Windows"
  x86_64-w64-mingw32-gcc -O2 -Werror -I. -I"$AP_TEST_WINDOWS_SDL/include/SDL2" -Dmain=SDL_main $SRCS \
    -L"$AP_TEST_WINDOWS_SDL/lib" -lmingw32 -lSDL2main -lSDL2 -lws2_32 -o "$OUT/ap_client_test.exe" || exit 1
  cp "$AP_TEST_WINDOWS_SDL/bin/SDL2.dll" "$OUT/"
  RUNNERS="linux windows"
fi

# A test CA and a certificate for "localhost" signed by it.
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=AP Test CA" \
  -keyout "$OUT/ca.key" -out "$OUT/ca.pem" -addext "basicConstraints=critical,CA:TRUE" \
  -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null || exit 1
openssl req -newkey rsa:2048 -nodes -subj "/CN=localhost" -keyout "$OUT/leaf.key" -out "$OUT/leaf.csr" 2>/dev/null
printf "subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\n" > "$OUT/leaf.ext"
openssl x509 -req -in "$OUT/leaf.csr" -CA "$OUT/ca.pem" -CAkey "$OUT/ca.key" -CAcreateserial -days 2 \
  -extfile "$OUT/leaf.ext" -out "$OUT/leaf.pem" 2>/dev/null || exit 1
# And one for "localhost" that nobody vouches for (someone in the middle).
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost" \
  -keyout "$OUT/bad.key" -out "$OUT/bad.pem" 2>/dev/null || exit 1

# Starts the mock server; sets PORT.
start_mock() {
  rm -f "$OUT/mock.out"  # not the port of the last one
  "$PYTHON" archipelago/tests/mock_server.py "$@" > "$OUT/mock.out" 2> "$OUT/mock.err" &
  MOCK_PID=$!
  PORT=
  for _ in $(seq 100); do
    PORT=$(sed -n 's/^READY //p' "$OUT/mock.out" 2>/dev/null)
    [ -n "$PORT" ] && return 0
    sleep 0.1
  done
  echo "the mock server didn't start:"; cat "$OUT/mock.err"
  return 1
}

stop_mock() {
  [ -n "$MOCK_PID" ] && kill "$MOCK_PID" 2>/dev/null && wait "$MOCK_PID" 2>/dev/null
  MOCK_PID=
}

free_port() {
  "$PYTHON" -c "import socket; s = socket.socket(); s.bind(('127.0.0.1', 0)); print(s.getsockname()[1])"
}

FAILED=0
RESULTS=
run() {  # run RUNNER NAME DRIVER-ARGS...
  runner=$1; name=$2; shift 2
  if [ "$runner" = windows ]; then
    WINEDEBUG=-all wine "$OUT/ap_client_test.exe" "$@" > "$OUT/run.log" 2>&1
  else
    "$OUT/ap_client_test" "$@" > "$OUT/run.log" 2>&1
  fi
  rc=$?
  if [ $rc -eq 0 ]; then
    RESULTS="$RESULTS
PASS $name ($runner)"
  else
    cat "$OUT/run.log"
    [ -s "$OUT/mock.err" ] && { echo "-- mock server stderr:"; cat "$OUT/mock.err"; }
    RESULTS="$RESULTS
FAIL $name ($runner)"
    FAILED=$((FAILED + 1))
  fi
  grep -E '^(PASS|FAIL) ' "$OUT/run.log" | sed "s/^/  [$runner] /"
}

# The game's side, headless (./zelda3 --mptest SCRIPT, @PORT@ filled in):
# the script must pass, each MOCK OK line given must show up, and the mock
# must not complain.
game_test() {  # game_test NAME "MOCK ARGS" SCRIPT "MOCK OK LINE"...
  name=$1; mock_args=$2; script=$3; shift 3
  if [ ! -x ./zelda3 ] || [ ! -f zelda3_assets.dat ]; then
    RESULTS="$RESULTS
SKIP $name (needs ./zelda3 and zelda3_assets.dat)"
    return
  fi
  # shellcheck disable=SC2086
  start_mock $mock_args || { FAILED=$((FAILED + 1)); return; }
  sed "s/@PORT@/$PORT/g" "$script" > "$OUT/game.txt"
  ./zelda3 --mptest "$OUT/game.txt" > "$OUT/run.log" 2>&1
  ok=$?
  for line in "$@"; do
    grep -q "$line" "$OUT/run.log" || { echo "no \"$line\""; ok=1; }
  done
  grep -q "MOCK ERROR" "$OUT/run.log" "$OUT/mock.err" && ok=1
  stop_mock
  if [ $ok -eq 0 ]; then
    RESULTS="$RESULTS
PASS $name (game)"
  else
    grep -E "FAIL|MOCK|apwait" "$OUT/run.log"
    cat "$OUT/mock.err"
    RESULTS="$RESULTS
FAIL $name (game)"
    FAILED=$((FAILED + 1))
  fi
}

with_mock() {  # with_mock RUNNER NAME "MOCK ARGS" DRIVER-ARGS... (PORT is filled in)
  runner=$1; name=$2; mock_args=$3; shift 3
  # shellcheck disable=SC2086
  start_mock $mock_args || { FAILED=$((FAILED + 1)); return; }
  args=$(printf '%s ' "$@" | sed "s/PORT/$PORT/g")
  # shellcheck disable=SC2086
  run "$runner" "$name" $args
  stop_mock
}

ALL="addresses basic nocompress tls gap reconnect refused password bigdp bigdp-raw deathlink ping flood hang noserver
slowtls downgrade hostile dlswitch"
SELECTED=${*:-$ALL}

for runner in $RUNNERS; do
  ca=$OUT/ca.pem
  [ "$runner" = windows ] && command -v winepath >/dev/null && ca=$(WINEDEBUG=-all winepath -w "$OUT/ca.pem")
  for s in $SELECTED; do
    case $s in
      addresses) run "$runner" addresses addresses ;;
      basic) with_mock "$runner" basic "--scenario basic" basic ws://127.0.0.1:PORT ;;
      nocompress) with_mock "$runner" nocompress "--scenario nocompress --no-compress" nocompress 127.0.0.1:PORT ;;
      tls) with_mock "$runner" tls "--scenario tls --tls $OUT/leaf.pem $OUT/leaf.key" tls PORT "$ca" ;;
      gap) with_mock "$runner" gap "--scenario gap" gap ws://127.0.0.1:PORT ;;
      reconnect) with_mock "$runner" reconnect "--scenario reconnect" reconnect 127.0.0.1:PORT ;;
      refused) with_mock "$runner" refused "--scenario refused" refused ws://127.0.0.1:PORT ;;
      password) with_mock "$runner" password "--scenario password --password secret" password ws://127.0.0.1:PORT ;;
      bigdp) with_mock "$runner" bigdp "--scenario bigdp" bigdp ws://127.0.0.1:PORT ;;
      bigdp-raw) with_mock "$runner" bigdp-raw "--scenario bigdp --no-compress" bigdp ws://127.0.0.1:PORT ;;
      deathlink) with_mock "$runner" deathlink "--scenario deathlink" deathlink ws://127.0.0.1:PORT ;;
      ping) with_mock "$runner" ping "--scenario ping" ping ws://127.0.0.1:PORT ;;
      flood) with_mock "$runner" flood "--scenario flood" flood ws://127.0.0.1:PORT ;;
      hang) with_mock "$runner" hang "--scenario hang" hang PORT ;;
      noserver) run "$runner" noserver noserver "$(free_port)" ;;
      slowtls) with_mock "$runner" slowtls "--scenario slowtls --tls $OUT/leaf.pem $OUT/leaf.key" slowtls PORT "$ca" ;;
      downgrade) with_mock "$runner" downgrade \
        "--scenario downgrade --tls $OUT/leaf.pem $OUT/leaf.key --bad-tls $OUT/bad.pem $OUT/bad.key" downgrade PORT "$ca" ;;
      hostile) with_mock "$runner" hostile "--scenario hostile" hostile PORT ;;
      # The game's side: the DeathLink tag follows the room in every connection.
      dlswitch) [ "$runner" = linux ] && game_test dlswitch "--scenario dlswitch" \
        archipelago/tests/dlswitch_game.txt "MOCK OK tags AP,DeathLink" "MOCK OK dlswitch 2" ;;
      *) echo "unknown scenario $s"; FAILED=$((FAILED + 1)) ;;
    esac
  done
done

echo "$RESULTS"
if [ $FAILED -eq 0 ]; then
  echo "All client tests passed"
else
  echo "$FAILED client test(s) failed"
fi
exit $FAILED
