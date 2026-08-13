#include <csignal>

#include <gst/gst.h>

#include <QApplication>

#include "MainWindow.h"

static QApplication *app = nullptr;

static void exit_on_ctrl_c(int) {
  if (app) app->quit();
  else exit(0);
}

int main(int argc, char *argv[]) {
  QCoreApplication::setOrganizationName("Teknolus");
  QCoreApplication::setOrganizationDomain("teknolus.com");
  QCoreApplication::setApplicationName("CalibGUI");

  gst_init(&argc, &argv);
  // The qmlglsink plugin must be loaded before any QML that uses GstGLVideoItem is
  // instantiated, same requirement as RobotGUI/src/main.cpp.
  GstElement *sink = gst_element_factory_make("qmlglsink", NULL);
  if (!sink) {
    fprintf(stderr, "CalibGUI: Could not create qmlglsink element. "
                     "Is gstreamer1.0-plugins-... (qt/qml) installed?\n");
  }

  app = new QApplication(argc, argv);

  signal(SIGINT, exit_on_ctrl_c);
  signal(SIGTERM, exit_on_ctrl_c);

  MainWindow *window = new MainWindow();
  window->show();

  int result = app->exec();
  // Explicit order matters here: window (and its child QTimers/QFutureWatcher) must
  // be destroyed while QApplication is still alive, or their teardown runs against a
  // gone event dispatcher (QBasicTimer::start warning, then a crash).
  delete window;
  delete app;
  return result;
}
