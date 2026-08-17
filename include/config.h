#ifndef CALIBGUI_CONFIG_H
#define CALIBGUI_CONFIG_H


/** Utility macro to set the CSS class for QT objects.*/
#define SETCLASS(ptr, name) {(ptr)->setProperty("cssclass",name);(ptr)->setStyleSheet(" ");}

/** Local port CalibGUI's UDP camera-stream socket binds to. Only one stream
    connection is ever open at a time, so a single fixed value is enough. */
constexpr const unsigned short LOCAL_PORT = 4720;

#endif
