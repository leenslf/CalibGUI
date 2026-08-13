#ifndef CALIBGUI_CALIBTYPES_H
#define CALIBGUI_CALIBTYPES_H

#include <string>

/** A robot found via peer discovery, picked from MainWindow's list and handed to
    CalibWindow to attempt a connection to. */
struct DiscoveredRobot {
  std::string name;
  std::string type; // "net" -> unencrypted
  std::string ip;
  int         port;
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
