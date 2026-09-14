#ifndef CALIBWINDOW_H
#define CALIBWINDOW_H

#include <string>
#include <vector>

#include <QString>
#include <QImage>
#include <QWidget>
#include <QFutureWatcher>

#include <opencv2/core.hpp>

#include "config.h"

class QVBoxLayout;
class QPushButton;
class QLabel;

class CameraWidget;
class RemoteStreamSession;

struct CalibrationResult;

/** Owns a single stream-view + calibration workflow for one RemoteCamera picked from
    MainWindow's camera picker. Constructed by MainWindow with the picked RemoteCamera
    (robot IP, v4l2 device path, folder-naming index); board/capture parameters come
    from CalibSettings's own defaults (they're the same for every camera/session, so
    there's nothing for MainWindow to pass in). Then embedded as a page in
    MainWindow's QStackedWidget so the transition stays
    within one real top-level window. The constructor only builds a "Bağlanıyor..."
    skeleton; MainWindow calls start() right after making the page visible, which
    creates a RemoteStreamSession (the local GStreamer receive pipeline + the remote
    SSH `streamserver.sh` session -- see RemoteStreamSession.h) and starts it. Emits
    closed() whenever MainWindow should take back control.

    Also offers a checkable "undistort" preview: if this camera already has a
    calibration file, toggling it applies cv::undistort (using the loaded intrinsics)
    to the displayed frame so the calibration matrix can be sanity-checked visually.
    Saved calibration frames (Kare Kaydet) always come from the raw frame regardless
    of the toggle, since calibration must be computed from distorted images. */
class CalibWindow : public QWidget {
  Q_OBJECT

public:
  CalibWindow(const RemoteCamera &camera, QWidget *parent = nullptr);
  virtual ~CalibWindow();

  // Starts the local GStreamer receive pipeline and the remote SSH stream session,
  // and builds the rest of the UI. Deferred out of the constructor so MainWindow can
  // make this page visible first -- see the class comment above.
  void start();

signals:
  void closed();

private slots:
  void _handleDisconnect();
  void _handleFrameReady(const QImage &frame);
  void _handleSessionStatus(const QString &msg, bool error);
  void _handleSaveFrame();
  void _handleCalibrate();
  void _handleCalibrationFinished();
  void _handleClearFrames();
  void _handleUndistortToggled(bool checked);
  void _handlePushUndistort();
  void _handleInfoClicked();

private:
  void _initCalibUI(QVBoxLayout *layout);

  std::string _ResolveDataDir(const std::string &category);
  std::string _GetFramesDirectory();
  std::string _GetCalibrationFilePath(int cameraIndex);
  void        _RefreshSavedFrameCount();
  void        _UpdateSaveFrameCounter();
  void        _ShowStatus(const std::string &msg, bool error = false);
  int         _ClearSavedFrames();

  // Looks for this camera's Calibrations/<name>/0.yaml (written by
  // _handleCalibrationFinished) and, if present and valid, loads _cameraMatrix/
  // _distCoeffs from it. Returns whether a usable calibration was loaded -- the
  // return value directly drives whether _undistortBtn/_pushUndistortBtn are enabled.
  bool _LoadCalibrationForUndistort();

  // Raw read of Calibrations/<name>/0.yaml's fx/fy/cx/cy/distortion_coeffs, shared by
  // _LoadCalibrationForUndistort (which builds _cameraMatrix/_distCoeffs from it) and
  // _handlePushUndistort (which just needs the plain numbers to hand to the remote
  // copy_undistort.sh script). Returns false if no valid calibration file exists.
  bool _LoadCalibrationValues(float &fx, float &fy, float &cx, float &cy,
                               std::vector<float> &distCoeffs);

  // Renders _lastRawFrame into _pview, applying cv::undistort first if the toggle
  // is on and a calibration was loaded. Called on every new frame and immediately
  // on toggle so flipping the button doesn't wait for the next frame to arrive.
  void _UpdateDisplayFrame();

  RemoteCamera  _camera;
  CalibSettings _settings;

  QVBoxLayout *_layout = nullptr;
  QLabel      *_connectingLabel = nullptr;

  CameraWidget *_pview = nullptr;

  QPushButton *_saveFrameBtn = nullptr;
  QLabel      *_saveFrameCounter = nullptr;
  QLabel      *_calibInstructions = nullptr;
  QPushButton *_calibrateBtn = nullptr;
  QPushButton *_clearFramesBtn = nullptr;
  QPushButton *_disconnectBtn = nullptr;
  QPushButton *_undistortBtn = nullptr;
  QPushButton *_pushUndistortBtn = nullptr;
  QPushButton *_infoBtn = nullptr;
  QLabel      *_statusLabel = nullptr;

  QFutureWatcher<CalibrationResult> *_calibWatcher = nullptr;

  RemoteStreamSession *_session = nullptr;

  cv::Mat _cameraMatrix;   // empty until a calibration file is loaded
  cv::Mat _distCoeffs;
  bool    _undistortEnabled = false;
  QImage  _lastRawFrame;   // always the raw (distorted) frame -- used for both
                           // display-when-not-undistorting and for saved calibration
                           // frames, so toggling undistortion never taints Kare Kaydet

  int _frameIndex = 0;
  int _savedFrameCount = 0;
};

#endif
