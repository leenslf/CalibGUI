# CalibGUI

Minimal Qt console for camera calibration on a live robot. It reuses RobotGUI's
camera-handling widgets and calibration workflow, scoped down to just:

1. Connect to a live robot.
2. Choose a camera from the set of available cameras on that robot.
3. Capture calibration images (`SaveCameraFrame`).
4. Run calibration against the captured images.
5. Call `SetCameraUndistortParams` with the result.
6. Clear/reset the saved-frames folder on demand.

## Building

Requires the same sibling submodules as RobotGUI: `rhex-api`, `RoboMETU`, and
`udp-discovery`. CalibGUI is a
standalone qmake project.
TODO: THESE VARIABLES SHOULD BE ALREADY SET?

```sh
export INSTALL_PREFIX=/home/leen/robot.linux       
export RHEXAPI_DIR=/home/leen/robot.linux/rhex-api
export ROBOMETU_DIR=/home/leen/robot.linux/RoboMETU
export UDPD_DIR=/home/leen/robot.linux/udp-discovery
qmake CalibGUI.pro
make
```
TODO: PROVIDE BOARDS AS PDFs