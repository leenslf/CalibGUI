# Minimal camera-calibration console. Modeled on RobotGUI.pro's environment-variable
# and library conventions, but trimmed: desktop Linux only (no RG_HARDWARE/RG_LAYOUT
# matrix, no Android target), and no Qwt dependency (CalibGUI has no Qwt-based
# widgets). Camera frames arrive as plain JPEG-over-UDP datagrams (see
# tools/stream_camera.py) rather than through rhex-api, so there's no GStreamer/QML
# or rhex-api/robometu dependency to configure.
QT += core gui widgets concurrent network

CONFIG += c++14 link_pkgconfig

UDPD_TEST = $$(UDPD_DIR)

isEmpty(UDPD_TEST) {
   error("UDPD_DIR must be defined when invoking qmake")
}

SOURCES += \
  src/main.cpp \
  src/MainWindow.cpp \
  src/CalibWindow.cpp \
  src/CameraWidget.cpp

HEADERS += \
  include/config.h \
  include/debuglog.h \
  include/Theme.h \
  include/CalibTypes.h \
  include/MainWindow.h \
  include/CalibWindow.h \
  include/CameraWidget.h

INCLUDEPATH += $$PWD/include

DESTDIR = build
OBJECTS_DIR = build/.obj
MOC_DIR = build/.moc
RCC_DIR = build/.rcc

PKGCONFIG += opencv4

LIBS += \
    -L$$(INSTALL_PREFIX)/lib \
    -lpthread -ludp-discovery

INCLUDEPATH += \
    $$(INSTALL_PREFIX)/include
