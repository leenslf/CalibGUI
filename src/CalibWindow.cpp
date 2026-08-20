#include <array>
#include <cctype>
#include <string>

#include <QApplication>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QShortcut>
#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <gst/video/video.h>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>

#include "CalibWindow.h"
#include "CameraWidget.h"

#include "config.h"
#include "Theme.h"
#undef DEBUG_LOCAL
#include "debuglog.h"

static const int kCalibFrameCount = 30;

static const char *kFrameExtensions[] = { "*.png", "*.jpg", "*.jpeg" };

// How long without a received frame before we consider the stream unreachable.
static const qint64 kLivenessTimeoutMs = 4000;

// Bus-message poll interval, mirroring RoboMETU's GstManager::checkMessages pattern
// (a plain QTimer poll rather than a GLib main loop).
static const int kBusPollIntervalMs = 100;
static const int kLivenessCheckIntervalMs = 1000;

static void GlobFrameFiles(const std::string &dirPath, std::vector<cv::String> *filenames) {
  filenames->clear();
  for (const char *ext : kFrameExtensions) {
    std::vector<cv::String> matches;
    cv::glob(dirPath + ext, matches, false);
    filenames->insert(filenames->end(), matches.begin(), matches.end());
  }
}

static std::string SanitizeFolderName(const std::string &name) {
  std::string out;
  for (char c : name) {
    out += (isalnum((unsigned char) c) || c == '-' || c == '_') ? c : '_';
  }
  return out.empty() ? "kamera" : out;
}

// ===========================================================================================================
static const QString kPrimaryButtonStyle =
  "QPushButton {"
  "  background-color: " + kColorAccent + "; color: white; border: none;"
  "  border-radius: 6px; padding: 10px 28px; font-weight: bold; font-size: 14px;"
  "}"
  "QPushButton:hover:!disabled { background-color: " + kColorAccentHover + "; }"
  "QPushButton:pressed { background-color: " + kColorAccentPressed + "; }"
  "QPushButton:disabled { background-color: " + kColorAccentMuted + "; color: #eef2fb; }";

static const QString kSecondaryButtonStyle =
  "QPushButton {"
  "  background-color: transparent; color: " + kColorTextMuted + ";"
  "  border: 1px solid " + kColorBorder + "; border-radius: 6px;"
  "  padding: 6px 20px; font-size: 13px;"
  "}"
  "QPushButton:hover { background-color: " + kColorHover + "; }"
  "QPushButton:pressed { background-color: " + kColorBorder + "; }";

static const QString kDetailLabelStyle =
  "color: " + kColorTextMuted + "; font-size: 11pt; padding: 4px 2px;";

// Checkable button: looks like kSecondaryButtonStyle unchecked, accent-filled when
// checked (undistort preview on), and greyed out when disabled (no calibration file).
static const QString kToggleButtonStyle =
  "QPushButton {"
  "  background-color: transparent; color: " + kColorTextMuted + ";"
  "  border: 1px solid " + kColorBorder + "; border-radius: 6px;"
  "  padding: 6px 20px; font-size: 13px;"
  "}"
  "QPushButton:hover:!disabled { background-color: " + kColorHover + "; }"
  "QPushButton:checked { background-color: " + kColorAccent + "; color: white; border: none; }"
  "QPushButton:checked:hover { background-color: " + kColorAccentHover + "; }"
  "QPushButton:disabled { color: " + kColorBorder + "; }";

// Small circular "i" info button (fixed 22x22, border-radius half that => perfect circle).
static const QString kInfoButtonStyle =
  "QPushButton {"
  "  background-color: " + kColorAccent + "; color: white; border: none;"
  "  border-radius: 11px; font-weight: bold; font-size: 12px;"
  "}"
  "QPushButton:hover { background-color: " + kColorAccentHover + "; }"
  "QPushButton:pressed { background-color: " + kColorAccentPressed + "; }";

// ===========================================================================================================

struct CalibrationResult {
  bool    success = false;
  QString errorMessage;
  float   fx = 0.0f;
  float   fy = 0.0f;
  float   cx = 0.0f;
  float   cy = 0.0f;
  cv::Mat distCoeffs;
  std::vector<float> perViewErrors;
};

// Runs entirely on a QtConcurrent worker thread.
static CalibrationResult _RunCalibration(std::string path, CalibSettings settings) {
  CalibrationResult result;

  std::vector<cv::String> filenames;
  GlobFrameFiles(path, &filenames);

  if ((int) filenames.size() < kCalibFrameCount) {
    result.errorMessage = QString("HATA: Kalibrasyon için yeterli kare yok (%1 / %2).")
      .arg((int) filenames.size()).arg(kCalibFrameCount);
    return result;
  }

  cv::Ptr<cv::aruco::Dictionary> dictionary =
    cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
  cv::Ptr<cv::aruco::CharucoBoard> board = cv::aruco::CharucoBoard::create(
    settings.squaresX, settings.squaresY,
    settings.squareLengthM, settings.markerLengthM, dictionary);

  std::vector<std::vector<cv::Point2f>> allCharucoCorners;
  std::vector<std::vector<int>> allCharucoIds;
  cv::Size imageSize;

  for (const cv::String &fn : filenames) {
    cv::Mat image = cv::imread(fn);
    if (image.empty()) continue;
    imageSize = image.size();

    std::vector<int> markerIds;
    std::vector<std::vector<cv::Point2f>> markerCorners;
    cv::aruco::detectMarkers(image, dictionary, markerCorners, markerIds);
    if (markerIds.empty()) continue;

    std::vector<cv::Point2f> charucoCorners;
    std::vector<int> charucoIds;
    cv::aruco::interpolateCornersCharuco(markerCorners, markerIds, image, board,
                                          charucoCorners, charucoIds);
    if ((int) charucoIds.size() < settings.minValidFrames) continue;

    allCharucoCorners.push_back(charucoCorners);
    allCharucoIds.push_back(charucoIds);
  }

  if ((int) allCharucoCorners.size() < settings.minValidFrames) {
    result.errorMessage = "HATA: Kalibrasyon tahtası yeterli sayıda karede tespit edilemedi.";
    return result;
  }

  std::vector<cv::Mat> rvecs, tvecs;
  cv::Mat cameraMatrix;
  cv::Mat stdDeviationsIntrinsics, stdDeviationsExtrinsics, perViewErrors;
  try {
    cv::aruco::calibrateCameraCharuco(
      allCharucoCorners, allCharucoIds, board, imageSize,
      cameraMatrix, result.distCoeffs, rvecs, tvecs,
      stdDeviationsIntrinsics, stdDeviationsExtrinsics, perViewErrors);
  } catch (const cv::Exception &e) {
    ERRPRINTF("[EE] CalibWindow::_RunCalibration: %s\n", e.what());
    result.errorMessage = "HATA: Kalibrasyon başarısız oldu.";
    return result;
  }
  result.fx = (float) cameraMatrix.at<double>(0, 0);
  result.fy = (float) cameraMatrix.at<double>(1, 1);
  result.cx = (float) cameraMatrix.at<double>(0, 2);
  result.cy = (float) cameraMatrix.at<double>(1, 2);

  result.perViewErrors.reserve(perViewErrors.total());
  for (int i = 0; i < (int) perViewErrors.total(); ++i) {
    result.perViewErrors.push_back((float) perViewErrors.at<double>(i));
  }

  result.success = true;
  return result;
}

CalibWindow::CalibWindow(const RemoteCamera &camera, const CalibSettings &settings,
                          QWidget *parent)
  : QWidget(parent), _camera(camera), _settings(settings) {
  _layout = new QVBoxLayout(this);

  _connectingLabel = new QLabel("Bağlanıyor...");
  _connectingLabel->setWordWrap(true);
  _connectingLabel->setAlignment(Qt::AlignCenter);
  _layout->addWidget(_connectingLabel);
}

void CalibWindow::start() {
  delete _connectingLabel;
  _connectingLabel = nullptr;
  _initCalibUI(_layout);
  _undistortBtn->setEnabled(_LoadCalibrationForUndistort());

  _connected = true;

  _StartReceivePipeline();

  _busTimer = new QTimer(this);
  connect(_busTimer, &QTimer::timeout, this, &CalibWindow::_handleGstBusTimer);
  _busTimer->start(kBusPollIntervalMs);

  _livenessTimer = new QTimer(this);
  connect(_livenessTimer, &QTimer::timeout, this, &CalibWindow::_handleLivenessTimer);
  _livenessTimer->start(kLivenessCheckIntervalMs);

  _StartStreamProcess();
}

CalibWindow::~CalibWindow() {
  // Wait out any background calibration before tearing down.
  if (_calibWatcher && _calibWatcher->isRunning()) _calibWatcher->waitForFinished();
  _StopStreamProcess();
  _StopReceivePipeline();
}

void CalibWindow::_initCalibUI(QVBoxLayout *layout) {
  QHBoxLayout *topBar = new QHBoxLayout;
  _disconnectBtn = new QPushButton("Bağlantıyı Kes");
  _disconnectBtn->setStyleSheet(kSecondaryButtonStyle);
  connect(_disconnectBtn, &QPushButton::released, this, &CalibWindow::_handleDisconnect);
  topBar->addWidget(_disconnectBtn);

  _undistortBtn = new QPushButton("Distorsiyonu Düzelt");
  _undistortBtn->setCheckable(true);
  _undistortBtn->setStyleSheet(kToggleButtonStyle);
  _undistortBtn->setToolTip("Bu kameranın kayıtlı kalibrasyon dosyasını kullanarak "
                             "görüntüyü distorsiyondan arındır.");
  _undistortBtn->setEnabled(false);
  connect(_undistortBtn, &QPushButton::toggled, this, &CalibWindow::_handleUndistortToggled);
  topBar->addWidget(_undistortBtn);
  topBar->addStretch();
  _saveFrameCounter = new QLabel;
  _saveFrameCounter->setStyleSheet(kDetailLabelStyle);
  topBar->addWidget(_saveFrameCounter);
  layout->addLayout(topBar);

  _RefreshSavedFrameCount();
  _UpdateSaveFrameCounter();

  _pview = new CameraWidget;
  _pview->setState(true);
  layout->addWidget(_pview);

  QHBoxLayout *instructionsRow = new QHBoxLayout;
  _infoBtn = new QPushButton("i");
  _infoBtn->setFixedSize(22, 22);
  _infoBtn->setStyleSheet(kInfoButtonStyle);
  _infoBtn->setToolTip("Kalibrasyon talimatları");
  connect(_infoBtn, &QPushButton::released, this, &CalibWindow::_handleInfoClicked);
  instructionsRow->addWidget(_infoBtn);

  _calibInstructions = new QLabel(
    "İpucu: Tahtanın tamamı net görünsün; farklı açı, mesafe ve konumlardan "
    "(kenarlar dahil) kamerayı sabit tutarak kaydedin.");
  _calibInstructions->setWordWrap(true);
  _calibInstructions->setStyleSheet(kDetailLabelStyle);
  instructionsRow->addWidget(_calibInstructions, 1);
  layout->addLayout(instructionsRow);

  QHBoxLayout *btnRow = new QHBoxLayout;
  _saveFrameBtn = new QPushButton("Kare Kaydet");
  _saveFrameBtn->setStyleSheet(kPrimaryButtonStyle);
  _saveFrameBtn->setFocusPolicy(Qt::NoFocus);
  connect(_saveFrameBtn, &QPushButton::released, this, &CalibWindow::_handleSaveFrame);
  btnRow->addWidget(_saveFrameBtn, 2);

  QShortcut *saveFrameShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  saveFrameShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(saveFrameShortcut, &QShortcut::activated, this, &CalibWindow::_handleSaveFrame);

  _calibrateBtn = new QPushButton("KALİBRE ET");
  _calibrateBtn->setStyleSheet(kPrimaryButtonStyle);
  connect(_calibrateBtn, &QPushButton::released, this, &CalibWindow::_handleCalibrate);
  btnRow->addWidget(_calibrateBtn, 2);

  _clearFramesBtn = new QPushButton("Kareleri Temizle");
  _clearFramesBtn->setStyleSheet(kSecondaryButtonStyle);
  connect(_clearFramesBtn, &QPushButton::released, this, &CalibWindow::_handleClearFrames);
  btnRow->addWidget(_clearFramesBtn, 1);

  QPushButton *calibExitBtn = new QPushButton("Çıkış");
  calibExitBtn->setStyleSheet(kSecondaryButtonStyle);
  calibExitBtn->setMinimumWidth(80);
  connect(calibExitBtn, &QPushButton::released, qApp, &QApplication::quit);
  btnRow->addWidget(calibExitBtn);

  layout->addLayout(btnRow);

  _statusLabel = new QLabel;
  _statusLabel->setWordWrap(true);
  layout->addWidget(_statusLabel);

  layout->addStretch();

  _ShowStatus("Görüntü bekleniyor...");
}

void CalibWindow::_handleDisconnect() {
  if (!_connected) return;
  _disconnectBtn->setEnabled(false);
  if (_livenessTimer) _livenessTimer->stop();
  if (_busTimer) _busTimer->stop();
  _connected = false;

  _StopStreamProcess();
  _StopReceivePipeline();

  emit closed();
}

void CalibWindow::_StartReceivePipeline() {
  std::string desc =
    "udpsrc port=" + std::to_string(STREAM_PORT) + " "
    "caps=\"application/x-rtp,media=video,clock-rate=90000,encoding-name=RAW,"
    "sampling=YCbCr-4:2:2,depth=(string)8,width=(string)640,height=(string)480,"
    "colorimetry=SMPTE240M,payload=96\" "
    "! rtpvrawdepay ! videoconvert ! video/x-raw,format=RGB "
    "! appsink name=sink emit-signals=true max-buffers=1 drop=true sync=false";

  GError *error = nullptr;
  _pipeline = gst_parse_launch(desc.c_str(), &error);
  if (!_pipeline || error) {
    ERRPRINTF("[EE] CalibWindow::_StartReceivePipeline: %s\n",
              error ? error->message : "gst_parse_launch returned null");
    if (error) g_error_free(error);
    _ShowStatus("HATA: GStreamer pipeline başlatılamadı.", true);
    return;
  }

  _appsink = gst_bin_get_by_name(GST_BIN(_pipeline), "sink");
  g_signal_connect(_appsink, "new-sample", G_CALLBACK(&CalibWindow::_OnNewSample), this);
  _bus = gst_pipeline_get_bus(GST_PIPELINE(_pipeline));
  gst_element_set_state(_pipeline, GST_STATE_PLAYING);
}

void CalibWindow::_StopReceivePipeline() {
  if (_pipeline) gst_element_set_state(_pipeline, GST_STATE_NULL);
  if (_appsink)  { gst_object_unref(_appsink); _appsink = nullptr; }
  if (_bus)      { gst_object_unref(_bus); _bus = nullptr; }
  if (_pipeline) { gst_object_unref(_pipeline); _pipeline = nullptr; }
}

GstFlowReturn CalibWindow::_OnNewSample(GstElement *sink, gpointer userData) {
  CalibWindow *self = static_cast<CalibWindow *>(userData);

  GstSample *sample = nullptr;
  g_signal_emit_by_name(sink, "pull-sample", &sample);
  if (!sample) return GST_FLOW_ERROR;

  GstBuffer *buffer = gst_sample_get_buffer(sample);
  GstCaps   *caps   = gst_sample_get_caps(sample);
  GstVideoInfo vinfo;
  if (buffer && caps && gst_video_info_from_caps(&vinfo, caps)) {
    GstVideoFrame vframe;
    if (gst_video_frame_map(&vframe, &vinfo, buffer, GST_MAP_READ)) {
      const uchar *data = static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0));
      int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0);
      int w = GST_VIDEO_FRAME_WIDTH(&vframe);
      int h = GST_VIDEO_FRAME_HEIGHT(&vframe);
      // Deep copy now -- vframe/buffer are unmapped/unreffed right after this callback.
      QImage copy = QImage(data, w, h, stride, QImage::Format_RGB888).copy();
      gst_video_frame_unmap(&vframe);
      QMetaObject::invokeMethod(self, "_handleFrameReady", Qt::QueuedConnection,
                                 Q_ARG(QImage, copy));
    }
  }
  gst_sample_unref(sample);
  return GST_FLOW_OK;
}

void CalibWindow::_handleFrameReady(const QImage &frame) {
  if (!_connected || !_pview || frame.isNull()) return;

  bool wasStale = _lastFrameTimeMs == 0 || _stalled;
  _lastFrameTimeMs = QDateTime::currentMSecsSinceEpoch();
  _lastRawFrame = frame;
  _UpdateDisplayFrame();
  if (wasStale) {
    _stalled = false;
    _ShowStatus("Görüntü alınıyor.");
  }
}

bool CalibWindow::_LoadCalibrationForUndistort() {
  cv::FileStorage fs(_GetCalibrationFilePath(0), cv::FileStorage::READ);
  if (!fs.isOpened()) return false;

  float fx = 0.0f, fy = 0.0f, cx = 0.0f, cy = 0.0f;
  std::vector<float> dc;
  fs["fx"] >> fx;
  fs["fy"] >> fy;
  fs["cx"] >> cx;
  fs["cy"] >> cy;
  fs["distortion_coeffs"] >> dc;
  fs.release();

  if (fx <= 0.0f || fy <= 0.0f || dc.empty()) return false;

  _cameraMatrix = (cv::Mat_<double>(3, 3) << fx, 0, cx, 0, fy, cy, 0, 0, 1);
  _distCoeffs = cv::Mat((int) dc.size(), 1, CV_64F);
  for (size_t i = 0; i < dc.size(); ++i) _distCoeffs.at<double>((int) i) = dc[i];
  return true;
}

void CalibWindow::_handleUndistortToggled(bool checked) {
  _undistortEnabled = checked;
  _UpdateDisplayFrame();
}

void CalibWindow::_UpdateDisplayFrame() {
  if (!_pview || _lastRawFrame.isNull()) return;

  if (!_undistortEnabled || _cameraMatrix.empty()) {
    _pview->setFrame(_lastRawFrame);
    return;
  }

  // cv::undistort is a purely geometric per-pixel remap, so the RGB/BGR channel
  // order doesn't matter here -- wrapping the QImage's own buffer (no copy) is safe
  // since it's only read from.
  cv::Mat rgb(_lastRawFrame.height(), _lastRawFrame.width(), CV_8UC3,
              const_cast<uchar *>(_lastRawFrame.constBits()), _lastRawFrame.bytesPerLine());
  cv::Mat undistorted;
  cv::undistort(rgb, undistorted, _cameraMatrix, _distCoeffs);

  QImage out(undistorted.data, undistorted.cols, undistorted.rows,
             (int) undistorted.step, QImage::Format_RGB888);
  _pview->setFrame(out.copy());  // copy: `undistorted` goes out of scope right after
}

void CalibWindow::_handleInfoClicked() {
  QString msg = QString(
    "Kalibrasyon tahtası nasıl kullanılır?\n\n"
    "1. Tahtanın tamamı kadrajda ve net görünsün (bulanıklık kalibrasyonu bozar).\n"
    "2. Tahtayı farklı açılardan, mesafelerden ve konumlardan gösterin; kadrajın "
    "kenarlarını ve köşelerini de mutlaka dahil edin.\n"
    "3. Her pozisyonda tahtayı bir an sabit tutup 'Kare Kaydet' düğmesine (veya "
    "Boşluk tuşuna) basarak kare kaydedin.\n"
    "4. Kalibrasyon için en az %1 kare gerekir; ne kadar çeşitli açı ve mesafe "
    "kullanırsanız sonuç o kadar güvenilir olur.\n"
    "5. Yeterli kare biriktikten sonra 'KALİBRE ET' düğmesine basın.\n"
    "6. Kalibrasyon tamamlandığında 'Distorsiyonu Düzelt' düğmesini işaretleyerek "
    "sonucu görsel olarak kontrol edebilirsiniz.\n\n"
    "Kullanılan tahta ayarları: %2 x %3 kare, kare boyutu %4 mm, marker boyutu %5 mm.")
    .arg(kCalibFrameCount)
    .arg(_settings.squaresX)
    .arg(_settings.squaresY)
    .arg(_settings.squareLengthM * 1000.0, 0, 'f', 1)
    .arg(_settings.markerLengthM * 1000.0, 0, 'f', 1);

  QMessageBox::information(this, "Kalibrasyon Talimatları", msg);
}

void CalibWindow::_handleGstBusTimer() {
  if (!_bus) return;
  while (gst_bus_have_pending(_bus)) {
    GstMessage *msg = gst_bus_pop(_bus);
    switch (GST_MESSAGE_TYPE(msg)) {
      case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        ERRPRINTF("[EE] CalibWindow: GStreamer error: %s\n", err->message);
        _ShowStatus(std::string("HATA: ") + err->message, true);
        g_error_free(err);
        g_free(debug);
        break;
      }
      case GST_MESSAGE_EOS:
        // Not expected on a live UDP source; defensive only.
        _ShowStatus("Akış sonlandı (EOS).", true);
        break;
      default:
        break;
    }
    gst_message_unref(msg);
  }
}

void CalibWindow::_handleLivenessTimer() {
  if (!_connected) return;
  if (_lastFrameTimeMs > 0 &&
      QDateTime::currentMSecsSinceEpoch() - _lastFrameTimeMs > kLivenessTimeoutMs) {
    _stalled = true;
    _ShowStatus("Şu anda kameraya ulaşılamıyor!", true);
  }
}

void CalibWindow::_StartStreamProcess() {
  _streamProcess = new QProcess(this);
  QStringList args {
    "-tt",
    "-o", "BatchMode=yes",
    "-o", QString("ConnectTimeout=%1").arg(SSH_CONNECT_TIMEOUT_SECS),
    "-o", "StrictHostKeyChecking=accept-new",
    QString("%1@%2").arg(SSH_USER, QString::fromStdString(_camera.robotIp)),
    QString::fromUtf8(STREAMSERVER_PATH), "-d", QString::fromStdString(_camera.device)
  };
  connect(_streamProcess, &QProcess::errorOccurred, this, &CalibWindow::_handleStreamProcessError);
  connect(_streamProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &CalibWindow::_handleStreamProcessFinished);
  _streamProcess->start("ssh", args);
}

void CalibWindow::_StopStreamProcess() {
  if (!_streamProcess) return;
  if (_streamProcess->state() != QProcess::NotRunning) {
    // SIGTERM -> local ssh drops -> remote pty hangs up -> SIGHUP to the remote
    // process group, the same mechanism that stops a remote command when you
    // Ctrl-C or close an interactive SSH session's terminal (see -tt in
    // _StartStreamProcess).
    _streamProcess->terminate();
    if (!_streamProcess->waitForFinished(2000)) {
      _streamProcess->kill();
      _streamProcess->waitForFinished(1000);
    }
  }
  _streamProcess->deleteLater();
  _streamProcess = nullptr;
}

void CalibWindow::_handleStreamProcessError(QProcess::ProcessError error) {
  if (!_connected) return;
  if (error == QProcess::FailedToStart) {
    _ShowStatus("HATA: ssh çalıştırılamadı (sistemde kurulu mu?).", true);
  }
}

void CalibWindow::_handleStreamProcessFinished(int, QProcess::ExitStatus) {
  if (!_connected) return;
  _ShowStatus("Kamera akışı sonlandı (SSH bağlantısı koptu).", true);
}

std::string CalibWindow::_GetFramesDirectory() {
  QDir dir(QCoreApplication::applicationDirPath());
  dir.cdUp();
  QString relPath = "Frames/" + QString::fromStdString(SanitizeFolderName(_camera.name));
  dir.mkpath(relPath);
  return dir.filePath(relPath).toStdString() + "/";
}

std::string CalibWindow::_GetCalibrationFilePath(int cameraIndex) {
  QDir dir(QCoreApplication::applicationDirPath());
  dir.cdUp();
  QString relPath = "Calibrations/" + QString::fromStdString(SanitizeFolderName(_camera.name));
  dir.mkpath(relPath);
  return dir.filePath(relPath).toStdString() + "/" + std::to_string(cameraIndex) + ".yaml";
}

// Syncs _savedFrameCount/_frameIndex with what's actually on disk.
void CalibWindow::_RefreshSavedFrameCount() {
  std::vector<cv::String> filenames;
  GlobFrameFiles(_GetFramesDirectory(), &filenames);

  _savedFrameCount = (int) filenames.size();
  _frameIndex = 0;
  for (const cv::String &fn : filenames) {
    std::string base = fn;
    size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    try {
      int idx = std::stoi(base);
      if (idx > _frameIndex) _frameIndex = idx;
    } catch (const std::invalid_argument &) {
      // Not a plain numeric frame filename (e.g. left over from an older naming scheme); skip it.
    }
  }
}

void CalibWindow::_UpdateSaveFrameCounter() {
  _saveFrameCounter->setText(
    QString("Kare: %1/%2    Silinen: %3")
      .arg(_savedFrameCount).arg(kCalibFrameCount).arg(_lastRemovedCount));
}

void CalibWindow::_ShowStatus(const std::string &msg, bool error) {
  _statusLabel->setText(QString::fromStdString(msg));
  _statusLabel->setStyleSheet(error ? "color: #c0392b;" : "color: #2c3e50;");
}

void CalibWindow::_handleSaveFrame() {
  if (!_connected) return;

  // Always the raw (distorted) frame, regardless of the undistort preview toggle --
  // calibration must be computed from distorted images.
  QImage frame = _lastRawFrame;
  if (frame.isNull()) {
    _ShowStatus("Kaydetmeden önce görüntü alınmasını bekleyin.", true);
    return;
  }

  int nextIndex = _frameIndex + 1;
  char buf[16];
  snprintf(buf, sizeof(buf), "%03d.png", nextIndex);
  std::string path = _GetFramesDirectory() + buf;

  if (!frame.save(QString::fromStdString(path), "PNG")) {
    ERRPRINTF("[EE] CalibWindow::_handleSaveFrame: failed to write %s\n", path.c_str());
    _ShowStatus("HATA: Kare kaydedilemedi.", true);
    return;
  }

  _frameIndex = nextIndex;
  _ShowStatus("Kare kaydedildi: " + path);
  ++_savedFrameCount;
  _UpdateSaveFrameCounter();
}

void CalibWindow::_handleCalibrate() {
  if (_calibWatcher && _calibWatcher->isRunning()) return;

  if (!_calibWatcher) {
    _calibWatcher = new QFutureWatcher<CalibrationResult>(this);
    connect(_calibWatcher, &QFutureWatcher<CalibrationResult>::finished, this, &CalibWindow::_handleCalibrationFinished);
  }

  // The heavy lifting (image decode, ChArUco detection, bundle adjustment) runs on a QtConcurrent worker thread via _RunCalibration
  _calibrateBtn->setEnabled(false);
  _saveFrameBtn->setEnabled(false);
  _clearFramesBtn->setEnabled(false);
  _ShowStatus("Kalibrasyon çalışıyor, lütfen bekleyin...");

  _calibWatcher->setFuture(QtConcurrent::run(_RunCalibration, _GetFramesDirectory(), _settings));
}

void CalibWindow::_handleCalibrationFinished() {
  CalibrationResult result = _calibWatcher->result();

  _calibrateBtn->setEnabled(true);
  _saveFrameBtn->setEnabled(true);
  _clearFramesBtn->setEnabled(true);

  if (!result.success) {
    _ShowStatus(result.errorMessage.toStdString(), true);
    return;
  }

  if (!(result.distCoeffs.total() >= 5 && result.fx > 0.0f && result.fy > 0.0f)) {
    _ShowStatus("Kalibrasyon başarılı. UYARI: Sonuç geçersiz.", true);
    return;
  }

  std::array<float, 5> dc {
    (float) result.distCoeffs.at<double>(0), (float) result.distCoeffs.at<double>(1),
    (float) result.distCoeffs.at<double>(2), (float) result.distCoeffs.at<double>(3),
    (float) result.distCoeffs.at<double>(4)
  };
  std::string yamlPath = _GetCalibrationFilePath(0);
  cv::FileStorage fs(yamlPath, cv::FileStorage::WRITE);
  if (!fs.isOpened()) {
    ERRPRINTF("[EE] CalibWindow::_handleCalibrationFinished: could not open %s for writing\n",
              yamlPath.c_str());
    _ShowStatus("HATA: Kalibrasyon dosyaya kaydedilemedi.", true);
    return;
  }
  fs << "fx" << result.fx;
  fs << "fy" << result.fy;
  fs << "cx" << result.cx;
  fs << "cy" << result.cy;
  fs << "distortion_coeffs" << std::vector<float>(dc.begin(), dc.end());
  fs << "per_image_reprojection_errors" << result.perViewErrors;
  fs.release();
  _ShowStatus("Kalibrasyon başarılı. Kaydedildi: " + yamlPath);

  _undistortBtn->setEnabled(_LoadCalibrationForUndistort());
}

// Deletes all saved frames in the frames directory after a user confirmation dialog.
void CalibWindow::_handleClearFrames() {
  std::string path = _GetFramesDirectory();

  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Kareleri Temizle",
    QString::fromStdString(
      "'" + path + "' klasöründeki tüm kayıtlı kalibrasyon kareleri silinecek. "
      "Emin misiniz?"),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) return;

  int removed = _ClearSavedFrames();
  _ShowStatus(QString("Silinen kare sayısı: %1").arg(removed).toStdString());
}

int CalibWindow::_ClearSavedFrames() {
  std::string path = _GetFramesDirectory();
  std::vector<cv::String> filenames;
  GlobFrameFiles(path, &filenames);
  int removed = 0;
  for (const cv::String &fn : filenames) {
    if (QFile::remove(QString::fromStdString(fn))) ++removed;
  }

  _frameIndex = 0;
  _savedFrameCount = 0;
  _lastRemovedCount = removed;
  _UpdateSaveFrameCounter();
  return removed;
}
