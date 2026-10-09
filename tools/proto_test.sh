#!/usr/bin/env bash
# Runs tools/proto_test.js against prototype/index.html in headless Chrome
# (fresh profile, so progress and high score start empty). Fails if a check fails.
set -euo pipefail
cd "$(dirname "$0")/.."
CHROME=${CHROME:-}
for c in google-chrome chromium chromium-browser "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"; do
    [ -n "$CHROME" ] && break
    if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then CHROME=$c; fi
done
[ -n "$CHROME" ] || { echo "no Chrome found (set CHROME=...)" >&2; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cp prototype/levels.js tools/proto_test.js "$TMP/"
sed 's#</body>#<script src="proto_test.js"></script></body>#' prototype/index.html > "$TMP/t.html"
"$CHROME" --headless=new --disable-gpu --no-sandbox --user-data-dir="$TMP/profile" \
    --allow-file-access-from-files --virtual-time-budget=30000 --dump-dom "file://$TMP/t.html" 2>/dev/null \
    | python3 -c 'import sys,re,html; m=re.search(r"<div id=\"out\">(.*?)</div>", sys.stdin.read(), re.S); out=html.unescape(m.group(1)) if m else "no report"; print(out); sys.exit(0 if out.rstrip().endswith("all checks passed") else 1)'
