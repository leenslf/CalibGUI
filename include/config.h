#ifndef CALIBGUI_CONFIG_H
#define CALIBGUI_CONFIG_H


/** Utility macro to set the CSS class for QT objects.*/
#define SETCLASS(ptr, name) {(ptr)->setProperty("cssclass",name);(ptr)->setStyleSheet(" ");}

/** Local port CalibGUI's RHexAPI instance binds to. Only one robot connection is ever
    open at a time, so unlike RobotGUI's RobotInfo::local_port (which increments per
    discovered robot), a single fixed value is enough. */
constexpr const unsigned short LOCAL_PORT = 4720;

/** an empty password skips authorization entirely and falls back to the remote's default access level. */
constexpr const char * PLACEHOLDER_PASSWORD = "";

#endif
