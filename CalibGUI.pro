# Minimal camera-calibration console. Modeled on RobotGUI.pro's environment-variable
# and library conventions, but trimmed: desktop Linux only (no RG_HARDWARE/RG_LAYOUT
# matrix, no Android target), and no Qwt dependency (CalibGUI has no Qwt-based
# widgets).
QT += core gui widgets quick quickwidgets concurrent

CONFIG += c++14 link_pkgconfig

ROBOMETU_TEST = $$(ROBOMETU_DIR)
RHEXAPI_TEST = $$(RHEXAPI_DIR)
UDPD_TEST = $$(UDPD_DIR)

isEmpty(ROBOMETU_TEST) {
   error("ROBOMETU_DIR must be defined when invoking qmake")
}
isEmpty(RHEXAPI_TEST) {
   error("RHEXAPI_DIR must be defined when invoking qmake")
}
isEmpty(UDPD_TEST) {
   error("UDPD_DIR must be defined when invoking qmake")
}

SOURCES += \
  src/main.cpp \
  src/MainWindow.cpp \
  src/CalibWindow.cpp \
  src/CameraWidget.cpp \
  src/CamListWidget.cpp

HEADERS += \
  include/config.h \
  include/debuglog.h \
  include/Theme.h \
  include/CalibTypes.h \
  include/MainWindow.h \
  include/CalibWindow.h \
  include/CameraWidget.h \
  include/CamListWidget.h

RESOURCES += qml/qml.qrc

INCLUDEPATH += $$PWD/include

DESTDIR = build
OBJECTS_DIR = build/.obj
MOC_DIR = build/.moc
RCC_DIR = build/.rcc

PKGCONFIG += gstreamer-1.0 gstreamer-video-1.0 gstreamer-rtp-1.0 opencv4

PRE_TARGETDEPS += $$(RHEXAPI_DIR)/lib/librhexapi.a

LIBS += \
    -L$$(INSTALL_PREFIX)/lib \
    -L$$(RHEXAPI_DIR)/lib \
    -lrhexapi \
    -lcairo \
    -lpthread -lrt -lssl -lcrypto -largon2 -ludp-discovery

INCLUDEPATH += \
    $$(INSTALL_PREFIX)/include \
    $$(RHEXAPI_DIR)/include
