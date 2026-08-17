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

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
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

// Resend interval for the UDP "hello" that keeps the stream script's subscription
// alive; must be well under the script's own subscriber timeout (3s).
static const int kHelloIntervalMs = 1000;
// How long without a received frame before we consider the stream unreachable.
static const qint64 kLivenessTimeoutMs = 4000;

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

CalibWindow::CalibWindow(const DiscoveredCamera &camera, const CalibSettings &settings,
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

  _socket = new QUdpSocket(this);
  _socket->bind(LOCAL_PORT);
  connect(_socket, &QUdpSocket::readyRead, this, &CalibWindow::_handleReadyRead);
  _connected = true;

  _helloTimer = new QTimer(this);
  connect(_helloTimer, &QTimer::timeout, this, &CalibWindow::_handleHelloTimer);
  _helloTimer->start(kHelloIntervalMs);
  _handleHelloTimer();
}

CalibWindow::~CalibWindow() {
  // Wait out any background calibration before tearing down.
  if (_calibWatcher && _calibWatcher->isRunning()) _calibWatcher->waitForFinished();
}

void CalibWindow::_initCalibUI(QVBoxLayout *layout) {
  QHBoxLayout *topBar = new QHBoxLayout;
  _disconnectBtn = new QPushButton("Bağlantıyı Kes");
  _disconnectBtn->setStyleSheet(kSecondaryButtonStyle);
  connect(_disconnectBtn, &QPushButton::released, this, &CalibWindow::_handleDisconnect);
  topBar->addWidget(_disconnectBtn);
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

  _calibInstructions = new QLabel(
    "İpucu: Tahtanın tamamı net görünsün; farklı açı, mesafe ve konumlardan "
    "(kenarlar dahil) kamerayı sabit tutarak kaydedin.");
  _calibInstructions->setWordWrap(true);
  _calibInstructions->setStyleSheet(kDetailLabelStyle);
  layout->addWidget(_calibInstructions);

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
  if (_helloTimer) _helloTimer->stop();
  if (_socket) {
    _socket->close();
    delete _socket;
    _socket = nullptr;
  }
  _connected = false;

  emit closed();
}

void CalibWindow::_handleHelloTimer() {
  if (!_connected || !_socket) return;

  static const char kHello[] = "hello";
  _socket->writeDatagram(kHello, sizeof(kHello) - 1,
                          QHostAddress(QString::fromStdString(_camera.ip)), _camera.port);

  if (_lastFrameTimeMs > 0 &&
      QDateTime::currentMSecsSinceEpoch() - _lastFrameTimeMs > kLivenessTimeoutMs) {
    _stalled = true;
    _ShowStatus("Şu anda kameraya ulaşılamıyor!", true);
  }
}

void CalibWindow::_handleReadyRead() {
  while (_socket && _socket->hasPendingDatagrams()) {
    QByteArray datagram;
    datagram.resize((int) _socket->pendingDatagramSize());
    _socket->readDatagram(datagram.data(), datagram.size());

    QImage frame = QImage::fromData(datagram, "JPG");
    if (frame.isNull()) continue;

    bool wasStale = _lastFrameTimeMs == 0 || _stalled;
    _lastFrameTimeMs = QDateTime::currentMSecsSinceEpoch();
    _pview->setFrame(frame);
    if (wasStale) {
      _stalled = false;
      _ShowStatus("Görüntü alınıyor.");
    }
  }
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
  if (!_connected || !_pview) return;

  QImage frame = _pview->grabFrame();
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
