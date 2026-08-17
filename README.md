# CalibGUI

Minimal Qt console for camera calibration. Scoped to just:

1. Discover a camera stream (`tools/stream_camera.py`, run on the robot/camera
   host) advertising itself on the LAN.
2. Connect to it and view the live JPEG-over-UDP feed.
3. Capture calibration images.
4. Run calibration against the captured images.
5. Clear/reset the saved-frames folder on demand.

## Streaming a camera

On the machine with the camera attached:

```sh
python3 tools/stream_camera.py --camera 0
```

See `--help` for options (resolution, fps, JPEG quality, port, advertised name).

## Building CalibGUI

Requires the sibling `udp-discovery` submodule (used for finding camera streams
on the LAN). CalibGUI is a standalone qmake project.

```sh
export INSTALL_PREFIX=$HOME/robot.linux
export UDPD_DIR=$INSTALL_PREFIX/udp-discovery
qmake CalibGUI.pro
make
```
TODO: PROVIDE BOARDS AS PDFs
