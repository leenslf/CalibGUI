#!/usr/bin/env bash
#
# stop_stream.sh
#
# Stops the streamserver.sh session capturing from the given V4L2 device on
# this machine, waiting for it to actually exit before returning.
#
# Usage:
#   ./stop_stream.sh /dev/videoX
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 /dev/videoX" >&2
    exit 1
fi
DEVICE="$1"

# Resolve a mapped name (e.g. "frontcam" or "/dev/frontcam") to its real /dev/videoX node
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAP_CAMS_SCRIPT="$SCRIPT_DIR/map_cams.sh"
RESOLVED=$("$MAP_CAMS_SCRIPT" -n "$DEVICE" 2>/dev/null || true)
[[ -n "$RESOLVED" ]] && DEVICE="$RESOLVED"

PIDFILE="/tmp/streamserver-$(basename "$DEVICE").pid"
PID=$(cat "$PIDFILE" 2>/dev/null || true)
if [[ -z "$PID" ]] || ! kill -0 "$PID" 2>/dev/null; then
    PID=$(pgrep -f "v4l2src device=$DEVICE " | head -n1 || true)
fi

if [[ -n "$PID" ]]; then
    kill -TERM "$PID" 2>/dev/null || true
    for _ in $(seq 1 25); do
        kill -0 "$PID" 2>/dev/null || exit 0
        sleep 0.2
    done
    kill -KILL "$PID" 2>/dev/null || true
    sleep 0.2
fi
