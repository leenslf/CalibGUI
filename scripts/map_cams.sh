#!/bin/sh
# map_cams.sh — resolve /dev/videoX nodes to udev symlink names
# by matching KERNELS (USB port path) from the rules file.
# Usage: ./map_cams.sh [-n name | -p /dev/videoX] [rules_file]
#
#   (no option)  print every "<name> = /dev/videoX" pair
#   -n name      print the /dev/videoX node for a mapped name ("frontcam" or
#                "/dev/frontcam"); prints nothing if unmapped
#   -p /dev/videoX
#                print the mapped name for a node; prints nothing if unmapped

LOOKUP=""
LOOKUP_ARG=""
while getopts "n:p:" opt; do
    case "$opt" in
        n) LOOKUP=name; LOOKUP_ARG="${OPTARG#/dev/}" ;;
        p) LOOKUP=path; LOOKUP_ARG="$OPTARG" ;;
        *) echo "Usage: $0 [-n name | -p /dev/videoX] [rules_file]" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

RULES_FILE="${1:-/etc/udev/rules.d/99-trhex_cameras.rules}"

[ -r "$RULES_FILE" ] || { echo "Cannot read rules file: $RULES_FILE" >&2; exit 1; }

# "port name" pairs from the video4linux rules only
MAP=$(sed -n 's/.*SUBSYSTEM=="video4linux".*KERNELS=="\([^"]*\)".*SYMLINK+="\([^"]*\)".*/\1 \2/p' "$RULES_FILE")

[ -n "$MAP" ] || { echo "No video4linux KERNELS/SYMLINK pairs found in $RULES_FILE" >&2; exit 1; }

list_map() {
    for dev in /dev/video*; do
        [ -e "$dev" ] || continue
        base=$(basename "$dev")

        # Same filter as the rules: only the first node (index 0) of each camera
        [ "$(cat /sys/class/video4linux/$base/index 2>/dev/null)" = "0" ] || continue

        info=$(udevadm info -a -n "$dev" 2>/dev/null)

        printf '%s\n' "$MAP" | while read -r port name; do
            if printf '%s\n' "$info" | grep -q "KERNELS==\"$port\""; then
                echo "$name = $dev"
            fi
        done
    done
}

case "$LOOKUP" in
    name) list_map | awk -v n="$LOOKUP_ARG" '$1==n {print $3; exit}' ;;
    path) list_map | awk -v d="$LOOKUP_ARG" '$3==d {print $1; exit}' ;;
    *)    list_map ;;
esac
