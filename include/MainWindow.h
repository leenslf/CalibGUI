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
class GstManager;

/** Peer discovery + robot picking + settings. Shown at startup and again whenever a
    CalibWindow session ends (the user disconnects, or a connection attempt fails);
    constructs a fresh CalibWindow per successful pick and embeds it as a second page
    in _stack, so the transition stays within a single top-level window instead of
    swapping to a separate one. */
class MainWindow : public QWidget {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  virtual ~MainWindow();

private slots:
  void _pollDiscovery();
  void _handleRobotSelectionChanged();
  void _handleConnect();
  void _handleSettingsClicked();
  void _handleCalibWindowClosed();

private:
  void _initUI();
  void _UpdateRobotListHeight();

  std::string _ConfigFilePath();
  void        _LoadSettings();
  void        _SaveSettings();

  std::string _GetFramesDirectory();
  int         _ClearSavedFrames();

  QStackedWidget *_stack = nullptr;
  QWidget        *_connectPage = nullptr;

  QListWidget *_robotListWidget = nullptr;
  QPushButton *_connectBtn = nullptr;
  QPushButton *_settingsBtn = nullptr;
  QTimer      *_discoveryTimer = nullptr;

  udpdiscovery::Peer *_peer = nullptr;
  std::vector<DiscoveredRobot> _robots;

  CalibSettings _settings;
  CalibWindow  *_calibWindow = nullptr;

  // Held for the whole app lifetime, since MainWindow is the one object that's always
  // alive from startup to exit (CalibWindow instances come and go per connection) —
  // see the GstManager refcount note in MainWindow.cpp's constructor for why that
  // matters (it's not decorative: dropping it lets GstManager's singleton refcount
  // hit zero, which triggers gst_deinit() and permanently breaks GStreamer for the
  // rest of the process).
  GstManager *_gm = nullptr;
};

#endif
