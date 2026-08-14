#ifndef CALIBWINDOW_H
#define CALIBWINDOW_H

#include <string>

#include <QString>
#include <QWidget>
#include <QFutureWatcher>

#include <RHexAPI.h>

#include "CalibTypes.h"

class QVBoxLayout;
class QPushButton;
class QLabel;
class QTimer;

class CameraWidget;
class CamListWidget;

struct CalibrationResult;

/** Owns a single connect attempt and calibration workflow for one robot. Constructed
    by MainWindow with a picked DiscoveredRobot and the current CalibSettings, then
    embedded as a page in MainWindow's QStackedWidget so the transition stays within
    one real top-level window. The constructor only builds a "Bağlanıyor..." skeleton;
    MainWindow calls start() right after making the page visible, which then attempts
    the connection (still a blocking call, matching the app's original connect UX) and
    either builds the calibration UI (on success) or shows an error with a way back to
    MainWindow (on failure). Emits closed() whenever MainWindow should take back
    control. */
class CalibWindow : public QWidget {
  Q_OBJECT

public:
  CalibWindow(const DiscoveredRobot &robot, const CalibSettings &settings,
              QWidget *parent = nullptr);
  virtual ~CalibWindow();

  // Attempts the connection and builds the rest of the UI. Deferred out of the
  // constructor so MainWindow can make this page visible first — see the class
  // comment above.
  void start();

signals:
  void closed();

private slots:
  void _handleDisconnect();
  void _checkConnection();
  void _handleSaveFrame();
  void _handleCalibrate();
  void _handleCalibrationFinished();
  void _handleClearFrames();

private:
  bool _attemptConnect(QString *errorMessage);
  void _initCalibUI(QVBoxLayout *layout);

  std::string _GetFramesDirectory();
  std::string _GetCalibrationFilePath(int cameraIndex);
  void        _RefreshSavedFrameCount();
  void        _UpdateSaveFrameCounter();
  void        _ShowStatus(const std::string &msg, bool error = false);
  int         _ClearSavedFrames();

  DiscoveredRobot _robot;
  CalibSettings   _settings;

  QVBoxLayout *_layout = nullptr;
  QLabel      *_connectingLabel = nullptr;

  CameraWidget  *_pview = nullptr;
  CameraWidget  *_sview = nullptr;
  CamListWidget *_camlist_widget = nullptr;

  QPushButton *_saveFrameBtn = nullptr;
  QLabel      *_saveFrameCounter = nullptr;
  QLabel      *_calibInstructions = nullptr;
  QPushButton *_calibrateBtn = nullptr;
  QPushButton *_clearFramesBtn = nullptr;
  QPushButton *_disconnectBtn = nullptr;
  QLabel      *_statusLabel = nullptr;

  QTimer *_connectionTimer = nullptr;

  QFutureWatcher<CalibrationResult> *_calibWatcher = nullptr;

  RHexAPI *rhex_ = nullptr;
  bool     connected_ = false;

  int _frameIndex = 0;
  int _savedFrameCount = 0;
  int _lastRemovedCount = 0;
};

#endif
