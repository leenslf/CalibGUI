#include <csignal>

#include <QApplication>

#include <gst/gst.h>

#include "MainWindow.h"

static QApplication *app = nullptr;

static void exit_on_ctrl_c(int) {
  if (app) app->quit();
  else exit(0);
}

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);

  QCoreApplication::setOrganizationName("Teknolus");
  QCoreApplication::setOrganizationDomain("teknolus.com");
  QCoreApplication::setApplicationName("CalibGUI");

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
  gst_deinit();
  return result;
}
