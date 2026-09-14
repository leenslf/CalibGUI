#!/usr/bin/env bash
#
# stop_stream.sh
#
# Stops the streamserver.sh session capturing from the given V4L2 device on
# this machine, waiting for it to actually exit before returning.
#
# Usage:
#   ./stop_stream.sh /dev/videoX
#
# Run over SSH by CalibGUI (see CalibWindow::_StopStreamProcess) instead of
# relying on the SSH connection itself to end the remote gst-launch-1.0 --
# confirmed by testing that killing/closing the local ssh client does NOT
# reliably terminate it, which leaves it still holding the V4L2 device and
# causes "Device busy"/buffer-allocation failures on the next stream attempt.
# gst-launch-1.0's own SIGTERM -> EOS -> NULL shutdown (which is what actually
# issues VIDIOC_STREAMOFF/REQBUFS(0) to release the V4L2 buffer pool) only
# runs if it actually receives that signal.
#
# Prefers streamserver.sh's PID file (exact match, safe with multiple
# devices/sessions on the same host) and falls back to a device-scoped pgrep
# pattern match if it's missing/stale (mirrors streamserver.sh's own
# _reclaim_device). Escalates to SIGKILL if the process doesn't exit within
# ~5s so a wedged remote process can't block the caller indefinitely.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 /dev/videoX" >&2
    exit 1
fi
DEVICE="$1"

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
