#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <string>
#include <vector>

#include <QWidget>

#include <udp_discovery_peer.hpp>

#include "CalibTypes.h"

class QStackedWidget;
class QListWidget;
class QPushButton;
class QLabel;
class QTimer;

class CalibWindow;

/** Peer discovery + camera picking + settings. Shown at startup and again whenever a
    CalibWindow session ends (the user disconnects); constructs a fresh CalibWindow per
    successful pick and embeds it as a second page in _stack, so the transition stays
    within a single top-level window instead of swapping to a separate one. */
class MainWindow : public QWidget {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  virtual ~MainWindow();

private slots:
  void _pollDiscovery();
  void _handleCameraSelectionChanged();
  void _handleConnect();
  void _handleSettingsClicked();
  void _handleCalibWindowClosed();

private:
  void _initUI();
  void _UpdateCameraListHeight();

  std::string _ConfigFilePath();
  void        _LoadSettings();
  void        _SaveSettings();

  std::string _GetFramesDirectory();
  int         _ClearSavedFrames();

  QStackedWidget *_stack = nullptr;
  QWidget        *_connectPage = nullptr;

  QListWidget *_cameraListWidget = nullptr;
  QPushButton *_connectBtn = nullptr;
  QPushButton *_settingsBtn = nullptr;
  QTimer      *_discoveryTimer = nullptr;

  udpdiscovery::Peer *_peer = nullptr;
  std::vector<DiscoveredCamera> _cameras;

  CalibSettings _settings;
  CalibWindow  *_calibWindow = nullptr;
};

#endif
