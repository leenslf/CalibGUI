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
    v4l2-ctl --list-devices
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

echo "Streaming from $DEVICE to $TARGET_IP:$PORT (uncompressed, 2fps)..."
echo "Press Ctrl+C to stop."

exec gst-launch-1.0 -e \
    v4l2src device="$DEVICE" \
    ! video/x-raw,format=YUY2,width=640,height=480,framerate=30/1 \
    ! videorate \
    ! video/x-raw,framerate=2/1 \
    ! videoconvert \
    ! video/x-raw,format=UYVY \
    ! rtpvrawpay \
    ! udpsink host="$TARGET_IP" port=$PORT