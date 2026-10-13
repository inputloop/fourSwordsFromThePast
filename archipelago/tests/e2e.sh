#!/bin/sh
# End to end: generates a one-player multiworld with the Four Swords of the
# Past world, hosts it with Archipelago's own server and plays it with the
# game (headless): connect, make a file, open a chest, get the item back.
#
#   AP_ROOT=<Archipelago source tree> [PYTHON=<its python>] archipelago/tests/e2e.sh
#
# Needs zelda3_assets.dat (your ROM) and Archipelago 0.6.4 or newer from
# source (Generate.py and MultiServer.py with their requirements).
set -e
cd "$(dirname "$0")/../.."
: "${AP_ROOT:?set AP_ROOT to an Archipelago source tree}"
PYTHON=${PYTHON:-python3}
PORT=${PORT:-38591}
work=$(mktemp -d)
trap 'kill $server 2>/dev/null; rm -rf "$work"' EXIT
mkdir -p "$work/players" "$work/output"
python3 archipelago/build_apworld.py "$AP_ROOT/custom_worlds/fsotp.apworld" > /dev/null
cat > "$work/players/e2e.yaml" <<YAML
name: E2E
game: Four Swords of the Past
Four Swords of the Past:
  goal: ganon
  death_link: false
YAML
(cd "$AP_ROOT" && SKIP_REQUIREMENTS_UPDATE=1 "$PYTHON" Generate.py --player_files_path "$work/players" \
  --outputpath "$work/output" > "$work/generate.log" 2>&1) || { cat "$work/generate.log"; exit 1; }
zip=$(ls "$work"/output/AP_*.zip)
(cd "$AP_ROOT" && SKIP_REQUIREMENTS_UPDATE=1 exec "$PYTHON" MultiServer.py --port "$PORT" --disable_save "$zip" \
  > "$work/server.log" 2>&1) &
server=$!
sleep 4
sed "s/@PORT@/$PORT/" archipelago/tests/e2e_game.txt > "$work/game.txt"
if ./zelda3 --mptest "$work/game.txt" && grep -q "has completed their goal" "$work/server.log"; then
  echo "PASS e2e"
else
  echo "FAIL e2e (server log:)"
  tail -20 "$work/server.log"
  exit 1
fi
