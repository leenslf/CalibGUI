#!/usr/bin/env bash
#
# streamserver.sh
#
# Streams uncompressed video (v4l2 -> RTP raw video/UDP) from this device to whichever machine is currently SSH'd into it.
# Usage:
#   ./streamserver.sh -l          (list connected cameras and exit)
#   ./streamserver.sh [-d /dev/videoX]
set -euo pipefail

PORT=5600
DEVICE="/dev/video0"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAP_CAMS_SCRIPT="$SCRIPT_DIR/map_cams.sh"

usage() {
    echo "Usage: $0 [-d /dev/videoX|name]"
    echo "  -d   V4L2 device to stream from, either a /dev/videoX node or a"
    echo "       mapped name from map_cams.sh e.g. 'frontcam' (default: /dev/video0)"
    echo "  -l   List connected cameras/video devices and exit"
    exit 1
}

# Builds a sed script that rewrites every raw /dev/videoX node to its mapped
# display path (e.g. /dev/video2 -> /dev/frontcam), so callers only ever see and pass around the mapped name
device_name_sed_script() {
    local map name eq raw_dev script=""
    map=$("$MAP_CAMS_SCRIPT" 2>/dev/null || true)
    [[ -n "$map" ]] || return 0
    while read -r name eq raw_dev; do
        [[ -n "$name" && -n "$raw_dev" ]] || continue
        script+="s#${raw_dev}\\b#/dev/${name}#g;"
    done <<< "$map"
    printf '%s' "$script"
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

    local sed_script
    sed_script=$(device_name_sed_script)

    echo "Connected cameras:"
    v4l2-ctl --list-devices 2>&1 | sed "$sed_script" || true
    echo
    echo "Supported formats per device:"
    for dev in /dev/video*; do
        echo "--- $(sed "$sed_script" <<< "$dev") ---"
        v4l2-ctl --device="$dev" --list-formats-ext 2>/dev/null \
            || echo "  (could not query formats)"
        echo
    done
}

while getopts "d:l" opt; do
    case "$opt" in
        d) DEVICE="$OPTARG" ;;
        l) list_cameras; exit 0 ;;
        *) usage ;;
    esac
done

# Determine target IP from the SSH session
if [[ -z "${SSH_CONNECTION:-}" ]]; then
    echo "ERROR: \$SSH_CONNECTION is not set." >&2
    echo "This script must be run via SSH so the target IP can be" >&2
    echo "detected automatically (it streams back to whoever connected)." >&2
    exit 1
fi

# SSH_CONNECTION format: "<client_ip> <client_port> <server_ip> <server_port>"
TARGET_IP=${SSH_CONNECTION%% *}

# Resolve a mapped name (e.g. "frontcam" or "/dev/frontcam") to its real /dev/videoX node
RESOLVED=$("$MAP_CAMS_SCRIPT" -n "$DEVICE" 2>/dev/null || true)
[[ -n "$RESOLVED" ]] && DEVICE="$RESOLVED"

# Verify the capture device exists
if [[ ! -e "$DEVICE" ]]; then
    echo "ERROR: device '$DEVICE' does not exist." >&2
    echo "Available video devices:" >&2
    ls -1 /dev/video* 2>/dev/null || echo "  (none found)" >&2
    exit 1
fi

DEVICE_NAME=$("$MAP_CAMS_SCRIPT" -p "$DEVICE" 2>/dev/null || true)

# --- Self-heal: reclaim the device if a previous instance is still holding it ---
# The normal way to stop a stream is stop_stream.sh (run over SSH by CalibGUI).
# This only cleans up after streams that were never stopped that way: clients
# that disconnect abnormally (network drop, sleep, crash) and never ask us to
# stop. Without it, the next stream attempt fails V4L2 buffer allocation against
# a device an orphaned gst-launch-1.0 still has open. It deliberately doesn't
# call stop_stream.sh, so each script works on its own.
#
# The PID file holds gst-launch-1.0's PID (written below); stop_stream.sh and
# this section both use it to find the streamer. One file per device, so
# multiple devices on the same host never collide.
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

_reclaim_device "$(cat "$PIDFILE" 2>/dev/null || true)"
# Fallback for a missing/stale pidfile (e.g. this script was updated/redeployed
# after an old instance started): catch anything still matching our own
# pipeline shape for this exact device.
_reclaim_device "$(pgrep -f "v4l2src device=$DEVICE " | head -n1 || true)"

echo "Streaming from $DEVICE${DEVICE_NAME:+ (name: $DEVICE_NAME)} to $TARGET_IP:$PORT (uncompressed, 2fps)..."
echo "Press Ctrl+C to stop."

trap 'rm -f "$PIDFILE"' EXIT

# Main
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

# Under set -e a nonzero exit from gst-launch-1.0 ends the script with that
# same status (the EXIT trap still removes the PID file).
wait "$GST_PID"