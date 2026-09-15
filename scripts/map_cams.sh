#!/bin/sh
# map_cams.sh — resolve /dev/videoX nodes to their udev symlink names
# by tracing ATTRS{serial} against /etc/udev/rules.d/99-trhex_cameras.rules
#
# Usage: ./map_cams.sh [rules_file]
# Prints one "<name> = /dev/videoX" line per resolved capture device, e.g.:
#   frontcam = /dev/video2
#   rightcam = /dev/video4
#   leftcam  = /dev/video6
#   rearcam  = /dev/video8

RULES_FILE="${1:-/etc/udev/rules.d/99-trhex_cameras.rules}"

if [ ! -r "$RULES_FILE" ]; then
    echo "Cannot read rules file: $RULES_FILE" >&2
    exit 1
fi

# Build "serial name" pairs, only from the capture (non-hidraw) lines
# e.g. ACTION=="add", ATTRS{serial}=="2C059700", ENV{ID_V4L_CAPABILITIES}==":capture:", SYMLINK+="frontcam"
MAP=$(grep 'ID_V4L_CAPABILITIES' "$RULES_FILE" | \
    sed -n 's/.*ATTRS{serial}=="\([^"]*\)".*SYMLINK+="\([^"]*\)".*/\1 \2/p')

if [ -z "$MAP" ]; then
    echo "No capture-mode serial/symlink pairs found in $RULES_FILE" >&2
    exit 1
fi

for dev in /dev/video*; do
    [ -e "$dev" ] || continue

    # Only consider capture nodes (skip metadata-only nodes)
    caps=$(udevadm info -q property -n "$dev" 2>/dev/null | grep '^ID_V4L_CAPABILITIES=')
    case "$caps" in
        *:capture:*) ;;
        *) continue ;;
    esac

    serial=$(udevadm info -a -n "$dev" 2>/dev/null | \
        grep -m1 'ATTRS{serial}==' | sed 's/.*ATTRS{serial}=="\([^"]*\)".*/\1/')

    [ -z "$serial" ] && continue

    name=$(printf '%s\n' "$MAP" | awk -v s="$serial" '$1==s {print $2; exit}')

    if [ -n "$name" ]; then
        echo "$name = $dev"
    fi
done
