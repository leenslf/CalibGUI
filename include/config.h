#ifndef CALIBGUI_CONFIG_H
#define CALIBGUI_CONFIG_H

#include <string>


/** Utility macro to set the CSS class for QT objects.*/
#define SETCLASS(ptr, name) {(ptr)->setProperty("cssclass",name);(ptr)->setStyleSheet(" ");}

/** Fixed UDP port CalibGUI's local GStreamer receive pipeline (udpsrc) listens on
    for the RTP raw-video stream pushed by streamserver.sh. Must match
    streamserver.sh's hardcoded PORT; not user-configurable. */
constexpr const unsigned short STREAM_PORT = 5600;

/** SSH user streamserver.sh is always invoked as. Passwordless root key auth is
    assumed already provisioned on every robot CalibGUI talks to. */
constexpr const char *const SSH_USER = "root";

/** Fixed path to streamserver.sh in root's home directory on the robot. */
constexpr const char *const STREAMSERVER_PATH = "~/streamserver.sh";

/** ssh(1) ConnectTimeout in seconds, used for both the one-shot `-l` listing call
    and the long-lived `-d` streaming session, so a bad/unreachable IP fails fast
    instead of hanging the UI. */
constexpr const int SSH_CONNECT_TIMEOUT_SECS = 5;

/** A camera entry parsed from `streamserver.sh -l`'s "Connected cameras:" block on
    some robot host, picked from MainWindow's camera-picker page and handed to
    CalibWindow, which needs nothing else to open both the local GStreamer receive
    pipeline and the long-lived SSH streamserver.sh session. */
struct RemoteCamera {
  std::string robotIp;  // e.g. "192.168.1.50", as typed into MainWindow's IP field
  std::string label;    // v4l2-ctl group header text, e.g. "USB Camera (usb-...)" --
                         // display text in the picker list only, not used for paths
  std::string device;   // v4l2 device node, e.g. "/dev/video0"
  std::string name;     // device index, e.g. "0" for /dev/video0 -- the
                         // SanitizeFolderName() input for Frames/<name>/ and
                         // Calibrations/<name>/, derived from `device` at parse time
};

/** ChArUco board / capture parameters, user-editable from MainWindow's settings
    dialog and persisted to config/config.yaml. Defaults match the values CalibGUI
    shipped with before the settings dialog existed. */
struct CalibSettings {
  int   squaresX       = 6;
  int   squaresY       = 8;
  float squareLengthM  = 0.022f;
  float markerLengthM  = 0.016f;
  int   minValidFrames = 4;
};

#endif
