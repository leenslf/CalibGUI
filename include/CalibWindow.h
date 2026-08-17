#ifndef CALIBWINDOW_H
#define CALIBWINDOW_H

#include <string>

#include <QString>
#include <QWidget>
#include <QFutureWatcher>
#include <QUdpSocket>

#include "CalibTypes.h"

class QVBoxLayout;
class QPushButton;
class QLabel;
class QTimer;

class CameraWidget;

struct CalibrationResult;

/** Owns a single stream-view + calibration workflow for one discovered camera.
    Constructed by MainWindow with a picked DiscoveredCamera and the current
    CalibSettings, then embedded as a page in MainWindow's QStackedWidget so the
    transition stays within one real top-level window. The constructor only builds a
    "Bağlanıyor..." skeleton; MainWindow calls start() right after making the page
    visible, which sends the initial UDP hello to the camera's stream port and builds
    the calibration UI immediately (there's no handshake to wait on — frames just
    start arriving once the stream script sees the hello). Emits closed() whenever
    MainWindow should take back control. */
class CalibWindow : public QWidget {
  Q_OBJECT

public:
  CalibWindow(const DiscoveredCamera &camera, const CalibSettings &settings,
              QWidget *parent = nullptr);
  virtual ~CalibWindow();

  // Sends the initial hello and builds the rest of the UI. Deferred out of the
  // constructor so MainWindow can make this page visible first — see the class
  // comment above.
  void start();

signals:
  void closed();

private slots:
  void _handleDisconnect();
  void _handleHelloTimer();
  void _handleReadyRead();
  void _handleSaveFrame();
  void _handleCalibrate();
  void _handleCalibrationFinished();
  void _handleClearFrames();

private:
  void _initCalibUI(QVBoxLayout *layout);

  std::string _GetFramesDirectory();
  std::string _GetCalibrationFilePath(int cameraIndex);
  void        _RefreshSavedFrameCount();
  void        _UpdateSaveFrameCounter();
  void        _ShowStatus(const std::string &msg, bool error = false);
  int         _ClearSavedFrames();

  DiscoveredCamera _camera;
  CalibSettings    _settings;

  QVBoxLayout *_layout = nullptr;
  QLabel      *_connectingLabel = nullptr;

  CameraWidget *_pview = nullptr;

  QPushButton *_saveFrameBtn = nullptr;
  QLabel      *_saveFrameCounter = nullptr;
  QLabel      *_calibInstructions = nullptr;
  QPushButton *_calibrateBtn = nullptr;
  QPushButton *_clearFramesBtn = nullptr;
  QPushButton *_disconnectBtn = nullptr;
  QLabel      *_statusLabel = nullptr;

  QTimer *_helloTimer = nullptr;

  QFutureWatcher<CalibrationResult> *_calibWatcher = nullptr;

  QUdpSocket *_socket = nullptr;
  bool        _connected = false;
  bool        _stalled = false;
  qint64      _lastFrameTimeMs = 0;

  int _frameIndex = 0;
  int _savedFrameCount = 0;
  int _lastRemovedCount = 0;
};

#endif
