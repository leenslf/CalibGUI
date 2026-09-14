# Minimal camera-calibration console. Modeled on RobotGUI.pro's environment-variable
# and library conventions, but trimmed: desktop Linux only (no RG_HARDWARE/RG_LAYOUT
# matrix, no Android target), and no Qwt dependency (CalibGUI has no Qwt-based
# widgets). Camera frames arrive as an uncompressed RTP raw-video stream (see
# streamserver.sh, run on the robot over SSH), decoded locally via a GStreamer
# udpsrc/appsink pipeline (see CalibWindow) -- no QML/qmlglsink and no
# rhex-api/robometu dependency, unlike RobotGUI's video paths.
QT += core gui widgets concurrent network

CONFIG += c++14 link_pkgconfig

SOURCES += \
  src/main.cpp \
  src/MainWindow.cpp \
  src/CalibWindow.cpp \
  src/CalibTools.cpp \
  src/CameraWidget.cpp \
  src/SshProcess.cpp \
  src/RemoteStreamSession.cpp

HEADERS += \
  include/config.h \
  include/debuglog.h \
  include/Theme.h \
  include/MainWindow.h \
  include/CalibWindow.h \
  include/CalibTools.h \
  include/CameraWidget.h \
  include/SshProcess.h \
  include/RemoteStreamSession.h

INCLUDEPATH += $$PWD/include

DESTDIR = build
OBJECTS_DIR = build/.obj
MOC_DIR = build/.moc
RCC_DIR = build/.rcc

PKGCONFIG += opencv4 gstreamer-1.0 gstreamer-video-1.0

LIBS += -lpthread
