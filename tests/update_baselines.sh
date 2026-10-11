#!/bin/sh
# Remakes the screenshot baselines of tests/coop/visual.txt (after an intended
# change to how things look). They are made from your own ROM's graphics, so
# they stay on your computer: tests/baseline/ is never committed.
cd "$(dirname "$0")/.."
mkdir -p tests/baseline tests/out
ZELDA3_UPDATE_BASELINES=1 ./zelda3 --mptest tests/coop/visual.txt
