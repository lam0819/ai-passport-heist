#!/usr/bin/env bash
# Flash the game into ota_1 without touching anything else on the board.
#
# The assistant (ai-passport-muse) owns the bootloader, the partition table and
# NVS; `idf.py flash` would overwrite all three, so this writes only the app.
#
#   tools/flash.sh [-p PORT] [--boot]
#
#   --boot  also make ota_1 the boot slot, so the board starts in the game.
#           Without it, the board keeps booting the assistant (which has no Game
#           menu entry yet), so use --boot to try the game.
set -euo pipefail

cd "$(dirname "$0")/.."
PORT=""
BOOT=0
while [ $# -gt 0 ]; do
    case "$1" in
        -p|--port) PORT="$2"; shift 2 ;;
        --boot) BOOT=1; shift ;;
        *) echo "usage: $0 [-p PORT] [--boot]" >&2; exit 2 ;;
    esac
done

: "${IDF_PATH:?run . \$IDF_PATH/export.sh first}"
APP=build/ai-passport-heist.bin
[ -f "$APP" ] || { echo "no $APP: run idf.py build first" >&2; exit 1; }

PORT_ARGS=()
[ -n "$PORT" ] && PORT_ARGS=(--port "$PORT")

python -m esptool --chip esp32c3 ${PORT_ARGS[@]+"${PORT_ARGS[@]}"} -b 921600 \
    write-flash 0x400000 "$APP"

if [ "$BOOT" = 1 ]; then
    python "$IDF_PATH/components/app_update/otatool.py" ${PORT_ARGS[@]+"${PORT_ARGS[@]}"} \
        --partition-table-offset 0x10000 switch_ota_partition --slot 1
fi
