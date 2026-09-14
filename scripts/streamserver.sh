#!/usr/bin/env bash
#
# streamserver.sh
#
# Streams uncompressed video (v4l2 -> RTP raw video/UDP) from this Jetson
# to whichever machine is currently SSH'd into it.
#
# Usage:
#   ./streamserver.sh -l          (list connected cameras and exit)
#   ./streamserver.sh [-d /dev/videoX]
#
# Must be run over an SSH session (uses $SSH_CONNECTION to find the
# client's IP). Running it locally on the Jetson will error out.
# (-l is the exception: it works locally too, since it doesn't stream.)

set -euo pipefail

PORT=5600
DEVICE="/dev/video0"

usage() {
    echo "Usage: $0 [-d /dev/videoX]"
    echo "  -d   V4L2 device to stream from (default: /dev/video0)"
    echo "  -l   List connected cameras/video devices and exit"
    exit 1
}

list_cameras() {
    if ! command -v v4l2-ctl >/dev/null 2>&1; then
        echo "ERROR: v4l2-ctl not found (install v4l-utils)." >&2
        exit 1
    fi

    if ! ls /dev/video* >/dev/null 2>&1; then
        echo "No /dev/video* devices found."
        exit 0
    fi

    echo "Connected cameras:"
    # v4l2-ctl --list-devices can exit non-zero if it fails to query one of the
    # nodes (e.g. a device still held busy by another process) even though it
    # still prints the full device list beforehand; don't let that abort the
    # whole listing under set -e.
    v4l2-ctl --list-devices || true
    echo
    echo "Supported formats per device:"
    for dev in /dev/video*; do
        echo "--- $dev ---"
        v4l2-ctl --device="$dev" --list-formats-ext 2>/dev/null \
            || echo "  (could not query formats)"
        echo
    done
}

while getopts "d:lh" opt; do
    case "$opt" in
        d) DEVICE="$OPTARG" ;;
        l) list_cameras; exit 0 ;;
        h) usage ;;
        *) usage ;;
    esac
done

# --- Determine target IP from the SSH session ---
if [[ -z "${SSH_CONNECTION:-}" ]]; then
    echo "ERROR: \$SSH_CONNECTION is not set." >&2
    echo "This script must be run via SSH so the target IP can be" >&2
    echo "detected automatically (it streams back to whoever connected)." >&2
    exit 1
fi

# SSH_CONNECTION format: "<client_ip> <client_port> <server_ip> <server_port>"
TARGET_IP=$(awk '{print $1}' <<< "$SSH_CONNECTION")

if [[ -z "$TARGET_IP" ]]; then
    echo "ERROR: could not parse client IP from \$SSH_CONNECTION ('$SSH_CONNECTION')." >&2
    exit 1
fi

# --- Verify the capture device exists ---
if [[ ! -e "$DEVICE" ]]; then
    echo "ERROR: device '$DEVICE' does not exist." >&2
    echo "Available video devices:" >&2
    ls -1 /dev/video* 2>/dev/null || echo "  (none found)" >&2
    exit 1
fi

# --- Self-heal: reclaim the device if a previous instance is still holding it ---
# Guards against clients that disconnect abnormally (network drop, sleep, crash)
# and never get a chance to ask us to stop cleanly -- without this, the next
# stream attempt fails V4L2 buffer allocation against a device an orphaned
# gst-launch-1.0 still has open. Device-scoped (by basename) so multiple
# devices/sessions on the same host never collide with each other.
PIDFILE="/tmp/streamserver-$(basename "$DEVICE").pid"

_reclaim_device() {
    local pid="$1"
    [[ -n "$pid" ]] || return 0
    kill -0 "$pid" 2>/dev/null || return 0
    echo "Found existing streamer (pid $pid) still holding $DEVICE; stopping it..." >&2
    kill -TERM "$pid" 2>/dev/null || true
    for _ in $(seq 1 25); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.2
    done
    echo "Existing streamer (pid $pid) did not exit in time; sending SIGKILL." >&2
    kill -KILL "$pid" 2>/dev/null || true
    sleep 0.2
}

if [[ -f "$PIDFILE" ]]; then
    _reclaim_device "$(cat "$PIDFILE" 2>/dev/null || true)"
    rm -f "$PIDFILE"
fi
# Fallback for a missing/stale pidfile (e.g. this script was updated/redeployed
# after an old instance started): catch anything still matching our own
# pipeline shape for this exact device.
_reclaim_device "$(pgrep -f "v4l2src device=$DEVICE " | head -n1 || true)"

echo "Streaming from $DEVICE to $TARGET_IP:$PORT (uncompressed, 2fps)..."
echo "Press Ctrl+C to stop."

trap 'rm -f "$PIDFILE"' EXIT

# Not exec'd (unlike before) so this script can stay around to forward signals
# to gst-launch-1.0 and clean up the PID file once it actually exits -- both
# needed so a caller can signal-and-confirm shutdown instead of just closing
# the SSH connection and hoping the remote side notices (it doesn't reliably).
#
# v4l2src is left to negotiate whatever native format/resolution/framerate the
# camera actually offers (varies a lot between USB cameras -- some only do MJPG,
# some only support their highest resolution at a lower framerate, etc.); decodebin
# decodes it if needed (a no-op if it's already raw), and videoconvert/videoscale/
# videorate normalize it down to the fixed UYVY 1280x720@2fps the receiving end
# expects, regardless of what the camera natively provides.
#
# The queue before rtpvrawpay is required at this frame size -- without it,
# rtpvrawpay's allocation query propagates back to v4l2src's own mmap buffer
# pool and fails pool activation ("Failed to allocate required memory"),
# confirmed by testing directly against a 1280x720 camera.
gst-launch-1.0 -e \
    v4l2src device="$DEVICE" \
    ! video/x-raw,format=YUY2,width=1280,height=720,framerate=10/1 \
    ! videoconvert \
    ! videoscale \
    ! videorate \
    ! video/x-raw,format=UYVY,width=1280,height=720,framerate=2/1 \
    ! queue \
    ! rtpvrawpay \
    ! udpsink host="$TARGET_IP" port=$PORT &
GST_PID=$!
echo "$GST_PID" > "$PIDFILE"

# Forward SIGINT/SIGTERM to gst-launch-1.0 so it runs its own -e/EOS -> NULL
# shutdown (which is what actually issues VIDIOC_STREAMOFF/REQBUFS(0) to release
# the V4L2 buffer pool) instead of dying uncleanly. Since this script no longer
# exec's into gst-launch-1.0, both normally receive the signal directly anyway
# (same process group under the ssh pty); this trap is a safety net for callers
# that signal only this script's PID (e.g. by pidfile) rather than the pipeline
# pattern match.
trap 'kill -TERM "$GST_PID" 2>/dev/null || true' INT TERM

set +e
wait "$GST_PID"
GST_EXIT=$?
set -e

exit "$GST_EXIT"