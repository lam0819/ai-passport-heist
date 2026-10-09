#!/usr/bin/env bash
# Renders the README screenshots into docs/ from the game's own drawing code,
# and plays scripted moments that check the rules (stun gun, jump, crates,
# springboard, banana, hide, doors, drop-one-loot, escape, bonus, game over),
# lets an autopilot finish every level with the real controls, and checks the
# level progress (locks, NVS save, reboot, reset) and the 4-floor view.
# Needs only cc and python3. Fails if a check fails.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -O1 -DGFX_COUNT_PIXELS -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Itools/shots/stub -Imain \
    tools/shots/shots.c main/world.c main/art.c main/gfx.c main/assets.c main/levels.c -lm -o "$TMP/shots"
"$TMP/shots" "$TMP"
python3 tools/shots/to_png.py "$TMP" docs
