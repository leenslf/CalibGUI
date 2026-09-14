#ifndef CALIBGUI_CONFIG_H
#define CALIBGUI_CONFIG_H

#include <string>


/** Utility macro to set the CSS class for QT objects.*/
#define SETCLASS(ptr, name) {(ptr)->setProperty("cssclass",name);(ptr)->setStyleSheet(" ");}

// Fixed UDP port CalibGUI's local GStreamer receive pipeline (udpsrc) 
constexpr const unsigned short STREAM_PORT = 5600;

constexpr const char *const STREAMSERVER_PATH = "~/scripts/streamserver.sh";
constexpr const char *const STOPSTREAM_PATH = "~/scripts/stop_stream.sh";
constexpr const char *const APPLY_UNDISTORT_PATH = "~/scripts/copy_undistort.sh";

/** ssh(1) ConnectTimeout in seconds, used for both the one-shot `-l` listing call
    and the long-lived `-d` streaming session, so a bad/unreachable IP fails fast
    instead of hanging the UI. */
constexpr const int SSH_CONNECT_TIMEOUT_SECS = 5;

/** A camera entry parsed from `streamserver.sh -l`'s "Connected cameras:" block on
    some robot host, picked from MainWindow's camera-picker page and handed to
    CalibWindow, which needs nothing else to open both the local GStreamer receive
    pipeline and the long-lived SSH streamserver.sh session.

    sshTarget/sshPassword are used via `sshpass -e ssh ...` (password piped through
    the SSHPASS env var, not argv) -- a deliberately simple password-auth path for
    testing against a local machine, since the real robot flow uses passwordless
    key auth and never needs a password prompt at all. */
struct RemoteCamera {
  std::string sshTarget;   // "user@host", as typed into MainWindow's target field
  std::string sshPassword; // kept only in memory for the life of this session
  std::string label;       // v4l2-ctl group header text, e.g. "USB Camera (usb-...)"
                            // -- display text in the picker list only, not used for paths
  std::string device;      // v4l2 device node, e.g. "/dev/video0"
  std::string name;        // device index, e.g. "0" for /dev/video0 -- the
                            // SanitizeFolderName() input for Frames/<name>/ and
                            // Calibrations/<name>/, derived from `device` at parse time
};
struct CalibSettings {
  int   squaresX       = 8;
  int   squaresY       = 6;
  float squareLengthM  = 0.022f;
  float markerLengthM  = 0.016f;
  int   minValidFrames = 4;
};

#endif
