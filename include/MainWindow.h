#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <string>
#include <vector>

#include <QWidget>
#include <QProcess>

#include "config.h"

class QStackedWidget;
class QListWidget;
class QPushButton;
class QLabel;
class QLineEdit;

class CalibWindow;

/** Robot IP entry + SSH camera picking. Shown at startup and again
    whenever a CalibWindow session ends (the user disconnects); constructs a fresh
    CalibWindow per successful pick and embeds it as a second page in _stack, so the
    transition stays within a single top-level window instead of swapping to a
    separate one.

    The connect flow itself is a two-page sub-stack (_connectSubStack) inside
    _connectPage: the user first types the robot's IP (_ipEntryPage), which SSHes in
    to run `streamserver.sh -l` and list connected cameras; picking one
    (_cameraPickPage) reuses the same QListWidget/styling the old LAN-discovery flow
    used. */
class MainWindow : public QWidget {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  virtual ~MainWindow();

private slots:
  void _handleCameraSelectionChanged();
  void _handleConnect();
  void _handleCalibWindowClosed();

  void _handleIpSubmit();
  void _handleListProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void _handleListProcessError(QProcess::ProcessError error);
  void _handleBackToIpEntry();
  void _handleRefreshCameras();

private:
  void _initUI();
  void _initIpEntryPage();
  void _initCameraPickPage();
  void _UpdateCameraListHeight();
  void _StartListProcess();

  std::string _ConfigFilePath();
  void        _LoadSettings();

  QStackedWidget *_stack = nullptr;
  QWidget        *_connectPage = nullptr;

  QStackedWidget *_connectSubStack = nullptr;
  QWidget        *_ipEntryPage = nullptr;
  QWidget        *_cameraPickPage = nullptr;

  QLineEdit   *_ipLineEdit = nullptr;
  QPushButton *_ipSubmitBtn = nullptr;
  QLabel      *_ipStatusLabel = nullptr;

  QListWidget *_cameraListWidget = nullptr;
  QPushButton *_connectBtn = nullptr;
  QPushButton *_backBtn = nullptr;
  QPushButton *_refreshBtn = nullptr;
  QLabel      *_pickStatusLabel = nullptr;

  QProcess *_listProcess = nullptr;
  std::string _robotIp;
  std::vector<RemoteCamera> _cameras;

  CalibSettings _settings;
  CalibWindow  *_calibWindow = nullptr;
};

#endif
