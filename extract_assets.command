#!/bin/sh
# Builds zelda3_assets.dat from your own ROM of
# "Legend of Zelda, The - A Link to the Past (USA)".
# Put the ROM (.sfc or .smc, any file name) in this folder and run this file
# (on macOS a double-click opens it in Terminal; the game's "Extract now"
# runs it too). It gets the two Python libraries the extraction needs.
cd "$(dirname "$0")" || exit 1

fail() {
  echo
  echo "ERROR: $1"
  echo
  # (Keep the window when opened by a double-click.)
  if [ -t 0 ]; then printf "Press Return to close. "; read -r _; fi
  exit 1
}

# Python 3. (On macOS without the command line tools, python3 offers to
# install them and fails this time: run this file again after that.)
PY=
for c in python3 python; do
  if "$c" -c 'import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)' >/dev/null 2>&1; then PY=$c; break; fi
done
[ -n "$PY" ] || fail "Python 3 is needed once to extract the game assets.
  macOS: install the command line tools when python3 offers to (or run 'xcode-select --install'),
  Linux: install python3 with your package manager, then run this file again."

# The ROM: zelda3.sfc/.smc, else any .sfc/.smc here.
ROM=
for f in zelda3.sfc zelda3.smc *.sfc *.smc *.SFC *.SMC; do
  if [ -z "$ROM" ] && [ -f "$f" ]; then ROM=$f; fi
done
[ -n "$ROM" ] || fail "No ROM found. Copy your \"A Link to the Past (USA)\" ROM (.sfc or .smc)
  into this folder: $(pwd)"
echo "Using ROM: $ROM"

# Pillow and PyYAML, if missing.
if ! "$PY" -c 'import PIL, yaml' >/dev/null 2>&1; then
  echo "Installing the Python libraries Pillow and PyYAML..."
  "$PY" -m pip install --user pillow pyyaml 2>/dev/null ||
    "$PY" -m pip install --user --break-system-packages pillow pyyaml ||
    "$PY" -m pip install pillow pyyaml ||
    fail "Could not install Pillow and PyYAML. Try: $PY -m pip install --user pillow pyyaml"
fi

rm -f zelda3_assets.dat
"$PY" assets/restool.py --extract-from-rom -r "$ROM"
[ -f zelda3_assets.dat ] || fail "Asset extraction failed! See the messages above."

echo
echo "Complete! zelda3_assets.dat was created. You can now start zelda3."
if [ -t 0 ]; then printf "Press Return to close. "; read -r _; fi
