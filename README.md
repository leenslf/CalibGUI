# CalibGUI

Minimal Qt console for camera calibration. Scoped to just:

1. Enter a robot's IP address; CalibGUI SSHes in (`root`, passwordless key auth) and
   runs `streamserver.sh -l` to list its connected cameras.
2. Pick a camera; CalibGUI SSHes in again to run `streamserver.sh -d <device>`, and
   receives the resulting RTP raw-video stream locally via GStreamer.
3. Capture calibration images.
4. Run calibration against the captured images.
5. Clear/reset the saved-frames folder on demand.

## Streaming a camera

`streamserver.sh` must already be present at `~/streamserver.sh` on the robot. It
streams uncompressed video from a v4l2 device to whichever machine ran it over SSH:

```sh
ssh root@<robot-ip> ~/streamserver.sh -l          # list connected cameras
ssh root@<robot-ip> ~/streamserver.sh -d /dev/video0
```

CalibGUI runs both of these itself (from the IP-entry and camera-picker steps) --
there's no need to run them by hand except to debug connectivity.

## Building CalibGUI

CalibGUI is a standalone qmake project. Requires GStreamer 1.0 (`gstreamer-1.0`,
`gstreamer-video-1.0` via pkg-config) in addition to Qt and OpenCV.

```sh
qmake CalibGUI.pro
make
```
TODO: PROVIDE BOARDS AS PDFs
