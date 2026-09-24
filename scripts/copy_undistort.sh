#!/bin/bash
# copy_undistort.sh <device> <fx> <fy> <cx> <cy> <dist0> [dist1 ...]
#
# Writes a camera calibration result into the matching stream entry of
# multimedia_parameters.rc so the robot's own video pipeline applies
# undistortion (not just CalibGUI's local preview). Run on the robot itself
# (invoked over SSH by CalibGUI, see COPY_UNDISTORT_PATH in config.h).
#
# Finds the structArray block whose `uri = "v4l2://<device>";` matches the
# given device -- either its /dev/videoX path or its map_cams.sh name (e.g.
# "frontcam") -- then sets/inserts `bool undistort = true;` and a
# `struct undistort_params { ... }` with the given values in that block only.
# Every other block/line/comment in the file is left byte-for-byte unchanged.

set -u

if [ "$#" -lt 6 ]; then
  echo "Usage: $0 <device> <fx> <fy> <cx> <cy> <dist0> [dist1 ...]" >&2
  exit 1
fi

DEVICE=$1
FX=$2
FY=$3
CX=$4
CY=$5
shift 5
DIST="$*"

RC_FILE=~/TRHex/config/robots/$(hostname)/multimedia_parameters.rc

if [ ! -f "$RC_FILE" ]; then
  echo "copy_undistort.sh: no such file: $RC_FILE" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MAP_CAMS_SCRIPT="$SCRIPT_DIR/map_cams.sh"

# $DEVICE may be a raw /dev/videoX node or a map_cams.sh mapped name, passed
# either bare ("frontcam") or as a synthetic path ("/dev/frontcam") -- the
# mapping (not the filesystem) is authoritative here, so this works even if
# no /dev/frontcam node actually exists on disk.
MAPPED_DEVICE=$("$MAP_CAMS_SCRIPT" -n "$DEVICE" 2>/dev/null)
if [ -n "$MAPPED_DEVICE" ]; then
  DEVICE_NAME="${DEVICE#/dev/}"
  DEVICE="$MAPPED_DEVICE"
else
  DEVICE_NAME=$("$MAP_CAMS_SCRIPT" -p "$DEVICE" 2>/dev/null)
fi

# Naming on the rc sometimes differs from the device path (e.g. it uses the
# udev-mapped name like "v4l2://frontcam" instead of "v4l2:///dev/video2"), so
# match candidates against the resolved /dev/videoX path as well as the
# mapped name from map_cams.sh.
RESOLVED_DEVICE=$(readlink -f "$DEVICE" 2>/dev/null || echo "$DEVICE")
while IFS= read -r candidate; do
  [ -n "$candidate" ] || continue
  resolved_candidate=$(readlink -f "$candidate" 2>/dev/null || echo "$candidate")
  if [ "$resolved_candidate" = "$RESOLVED_DEVICE" ] || \
     { [ -n "$DEVICE_NAME" ] && [ "$candidate" = "$DEVICE_NAME" ]; }; then
    DEVICE=$candidate
    break
  fi
done < <(sed -n 's|.*uri[[:space:]]*=[[:space:]]*"v4l2://\([^"]*\)".*|\1|p' "$RC_FILE")

TMP_FILE="${RC_FILE}.tmp.$$"

awk -v device="$DEVICE" -v fx="$FX" -v fy="$FY" -v cx="$CX" -v cy="$CY" -v dist="$DIST" '
BEGIN {
  target_uri = "v4l2://" device
  found_device = 0
  depth = 0
  nlines = 0
}

# Buffer every line; we only decide what to do with a block once it closes.
{
  lines[nlines++] = $0

  # Track brace depth on a comment-stripped copy of the line (everything
  # from the first "#" onward), so braces inside commented-out example
  # structs in the file header do not throw off block detection.
  line = $0
  hash_pos = index(line, "#")
  if (hash_pos > 0) line = substr(line, 1, hash_pos - 1)
  for (i = 1; i <= length(line); i++) {
    c = substr(line, i, 1)
    if (c == "{") {
      depth++
      if (depth == 1) block_start = nlines - 1
    } else if (c == "}") {
      depth--
      if (depth == 0) {
        emit_block(block_start, nlines - 1)
        nlines = 0
      }
    }
  }
}

END {
  # Flush any trailing lines that were not part of a brace block (e.g. the
  # final ";" after the structArray, or trailing blank lines).
  for (i = 0; i < nlines; i++) print lines[i]

  if (!found_device) {
    print "copy_undistort.sh: no stream entry with uri v4l2://" device " found" > "/dev/stderr"
    exit 2
  }
}

function emit_block(start, end,    i, j, k, l, line2, c2, block_has_device,
                     out_lines, n_out, bool_written, struct_written,
                     inner_depth, indent) {
  # Lines before this blocks opening brace (blank lines, comments, etc.)
  # buffered since the last flush -- always pass these through verbatim.
  for (i = 0; i < start; i++) print lines[i]

  block_has_device = 0
  for (i = start; i <= end; i++) {
    if (index(lines[i], "uri") > 0 && index(lines[i], target_uri) > 0) block_has_device = 1
  }

  if (!block_has_device) {
    for (i = start; i <= end; i++) print lines[i]
    return
  }

  found_device = 1
  n_out = 0
  bool_written = 0
  struct_written = 0
  indent = "  "

  for (i = start; i <= end; i++) {
    l = lines[i]

    # Closing brace of the outer block: insert anything still missing, then
    # emit the closing brace itself.
    if (i == end) {
      if (!bool_written) out_lines[n_out++] = indent "bool undistort = true;"
      if (!struct_written) {
        out_lines[n_out++] = ""
        out_lines[n_out++] = indent "struct undistort_params {"
        out_lines[n_out++] = indent "  float fx = " fx ";"
        out_lines[n_out++] = indent "  float fy = " fy ";"
        out_lines[n_out++] = indent "  float cx = " cx ";"
        out_lines[n_out++] = indent "  float cy = " cy ";"
        out_lines[n_out++] = indent "  float distortion_coeffs = " dist ";"
        out_lines[n_out++] = indent "}"
      }
      out_lines[n_out++] = l
      continue
    }

    # Existing `bool undistort = ...;` line: rewrite the value in place.
    if (!bool_written && match(l, /^[ \t]*bool[ \t]+undistort[ \t]*=/)) {
      out_lines[n_out++] = indent "bool undistort = true;"
      bool_written = 1
      continue
    }

    # Existing `struct undistort_params { ... }`: replace its body in place,
    # tracking a nested brace depth so we find *its* closing brace and not
    # some unrelated one.
    if (!struct_written && match(l, /^[ \t]*struct[ \t]+undistort_params[ \t]*\{/)) {
      out_lines[n_out++] = indent "struct undistort_params {"
      out_lines[n_out++] = indent "  float fx = " fx ";"
      out_lines[n_out++] = indent "  float fy = " fy ";"
      out_lines[n_out++] = indent "  float cx = " cx ";"
      out_lines[n_out++] = indent "  float cy = " cy ";"
      out_lines[n_out++] = indent "  float distortion_coeffs = " dist ";"

      inner_depth = 1
      for (j = i + 1; j <= end && inner_depth > 0; j++) {
        line2 = lines[j]
        for (k = 1; k <= length(line2); k++) {
          c2 = substr(line2, k, 1)
          if (c2 == "{") inner_depth++
          else if (c2 == "}") inner_depth--
        }
        if (inner_depth == 0) {
          out_lines[n_out++] = indent "}"
          i = j
        }
      }
      struct_written = 1
      continue
    }

    out_lines[n_out++] = l
  }

  for (i = 0; i < n_out; i++) print out_lines[i]
}
' "$RC_FILE" > "$TMP_FILE"

AWK_STATUS=$?
if [ "$AWK_STATUS" -ne 0 ]; then
  rm -f "$TMP_FILE"
  exit 1
fi

BACKUP="${RC_FILE}.bak.$(date +%Y%m%d%H%M%S)"
cp "$RC_FILE" "$BACKUP" || { echo "copy_undistort.sh: failed to back up $RC_FILE" >&2; rm -f "$TMP_FILE"; exit 1; }

mv "$TMP_FILE" "$RC_FILE"

echo "Updated undistort params for $DEVICE in $RC_FILE"
exit 0
