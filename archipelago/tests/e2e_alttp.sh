#!/bin/sh
# End to end with Archipelago's own "A Link to the Past" world: generates a
# one-player multiworld with it (without patching a ROM), hosts it with
# Archipelago's own server and plays it with the game (headless): connect as
# that world, make a file, open a chest, get the item back, send the goal.
#
#   AP_ROOT=<Archipelago source tree> [PYTHON=<its python>] archipelago/tests/e2e_alttp.sh
#
# Needs zelda3_assets.dat (your ROM) and Archipelago 0.6.4 or newer from
# source (Generate.py and MultiServer.py with their requirements).
set -e
cd "$(dirname "$0")/../.."
: "${AP_ROOT:?set AP_ROOT to an Archipelago source tree}"
PYTHON=${PYTHON:-python3}
PORT=${PORT:-38592}
work=$(mktemp -d)
trap 'kill $server 2>/dev/null; rm -rf "$work"' EXIT
mkdir -p "$work/players" "$work/output"
cat > "$work/players/e2e.yaml" <<YAML
name: E2E
game: A Link to the Past
A Link to the Past:
  mode: open
  goal: ganon
  misery_mire_medallion: bombos
  turtle_rock_medallion: ether
  crystals_needed_for_gt: 5
YAML
(SKIP_REQUIREMENTS_UPDATE=1 "$PYTHON" "$(pwd)/archipelago/tests/gen_alttp_norom.py" "$AP_ROOT" \
  --player_files_path "$work/players" --outputpath "$work/output" > "$work/generate.log" 2>&1) || { cat "$work/generate.log"; exit 1; }
zip=$(ls "$work"/output/AP_*.zip)
(cd "$AP_ROOT" && SKIP_REQUIREMENTS_UPDATE=1 exec "$PYTHON" MultiServer.py --port "$PORT" --disable_save "$zip" \
  > "$work/server.log" 2>&1) &
server=$!
sleep 4
sed "s/@PORT@/$PORT/" archipelago/tests/e2e_alttp_game.txt > "$work/game.txt"
if ./zelda3 --mptest "$work/game.txt" && grep -q "has completed their goal" "$work/server.log"; then
  echo "PASS e2e_alttp"
else
  echo "FAIL e2e_alttp (server log:)"
  tail -20 "$work/server.log"
  exit 1
fi
