#!/bin/bash
# ensure_xvfb.sh — start Xvfb on :99 if not running, return 0 when ready.
set -e
pkill -9 Xvfb 2>/dev/null || true
sleep 1
rm -f /tmp/.X11-unix/X99 2>/dev/null || true
setsid Xvfb :99 -screen 0 1280x720x24 -nolisten tcp &>/tmp/xvfb.log &
sleep 2
if ! pgrep -x Xvfb >/dev/null; then
  echo "ERROR: Xvfb failed to start" >&2
  exit 1
fi
if [ ! -S /tmp/.X11-unix/X99 ]; then
  echo "ERROR: X99 socket missing" >&2
  exit 1
fi
echo "Xvfb ready on :99 (pid $(pgrep -x Xvfb))"
