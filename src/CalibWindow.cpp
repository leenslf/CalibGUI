#include <array>
#include <string>

#include <QApplication>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QShortcut>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>

#include "CalibWindow.h"
#include "CalibTools.h"
#include "CameraWidget.h"
#include "RemoteStreamSession.h"
#include "SshProcess.h"

#include "config.h"
#include "Theme.h"
#undef DEBUG_LOCAL
#include "debuglog.h"

CalibWindow::CalibWindow(const RemoteCamera &camera, QWidget *parent)
  : QWidget(parent), _camera(camera) {
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
  bool hasCalib = _LoadCalibrationForUndistort();
  _undistortBtn->setEnabled(hasCalib);
  _pushUndistortBtn->setEnabled(hasCalib);

  _session = new RemoteStreamSession(_camera, this);
  connect(_session, &RemoteStreamSession::frameReady, this, &CalibWindow::_handleFrameReady);
  connect(_session, &RemoteStreamSession::statusMessage, this, &CalibWindow::_handleSessionStatus);
  _session->start();
}

CalibWindow::~CalibWindow() {
  // Wait out any background calibration before tearing down.
  if (_calibWatcher && _calibWatcher->isRunning()) _calibWatcher->waitForFinished();
  delete _session;
  _session = nullptr;
}

void CalibWindow::_initCalibUI(QVBoxLayout *layout) {
  QHBoxLayout *topBar = new QHBoxLayout;
  topBar->setContentsMargins(0, 2, 0, 2);
  topBar->setSpacing(8);

  _disconnectBtn = new QPushButton("Bağlantıyı Kes");
  _disconnectBtn->setStyleSheet(kCompactButtonStyle);
  connect(_disconnectBtn, &QPushButton::released, this, &CalibWindow::_handleDisconnect);
  topBar->addWidget(_disconnectBtn);

  _undistortBtn = new QPushButton("Distorsiyonu Düzelt");
  _undistortBtn->setCheckable(true);
  _undistortBtn->setStyleSheet(kCompactToggleButtonStyle);
  _undistortBtn->setToolTip("Bu kameranın kayıtlı kalibrasyon dosyasını kullanarak "
                             "görüntüyü distorsiyondan arındır.");
  _undistortBtn->setEnabled(false);
  connect(_undistortBtn, &QPushButton::toggled, this, &CalibWindow::_handleUndistortToggled);
  topBar->addWidget(_undistortBtn);

  _pushUndistortBtn = new QPushButton("Robota Gönder");
  _pushUndistortBtn->setStyleSheet(kCompactButtonStyle);
  _pushUndistortBtn->setToolTip("Bu kameranın kalibrasyon sonucunu robotun "
                                 "multimedia_parameters.rc dosyasına yazar.");
  _pushUndistortBtn->setEnabled(false);
  connect(_pushUndistortBtn, &QPushButton::released, this, &CalibWindow::_handlePushUndistort);
  topBar->addWidget(_pushUndistortBtn);
  topBar->addStretch();

  _saveFrameCounter = new QLabel;
  _saveFrameCounter->setStyleSheet(kMutedSmallLabelStyle);
  _saveFrameCounter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  topBar->addWidget(_saveFrameCounter, 0, Qt::AlignVCenter);
  layout->addLayout(topBar);

  QFrame *topBarDivider = new QFrame;
  topBarDivider->setFrameShape(QFrame::HLine);
  topBarDivider->setFixedHeight(1);
  topBarDivider->setStyleSheet("background-color: " + kColorBorder + "; border: none;");
  layout->addWidget(topBarDivider);

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

  _calibInstructions = new QLabel("Daha fazla bilgi için buraya tıklayın.");
  _calibInstructions->setWordWrap(true);
  _calibInstructions->setStyleSheet(kDetailLabelStyle);
  instructionsRow->addWidget(_calibInstructions, 1);
  layout->addLayout(instructionsRow);

  QHBoxLayout *btnRow = new QHBoxLayout;
  btnRow->setSpacing(10);

  static const int kActionButtonHeight = 44;

  _saveFrameBtn = new QPushButton("Kare Kaydet");
  _saveFrameBtn->setStyleSheet(kSecondaryButtonStyle);
  _saveFrameBtn->setFocusPolicy(Qt::NoFocus);
  _saveFrameBtn->setFixedHeight(kActionButtonHeight);
  connect(_saveFrameBtn, &QPushButton::released, this, &CalibWindow::_handleSaveFrame);
  btnRow->addWidget(_saveFrameBtn, 1);

  QShortcut *saveFrameShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  saveFrameShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(saveFrameShortcut, &QShortcut::activated, this, &CalibWindow::_handleSaveFrame);

  _calibrateBtn = new QPushButton("KALİBRE ET");
  _calibrateBtn->setStyleSheet(kPrimaryButtonLargeStyle);
  _calibrateBtn->setFixedHeight(kActionButtonHeight);
  connect(_calibrateBtn, &QPushButton::released, this, &CalibWindow::_handleCalibrate);
  btnRow->addWidget(_calibrateBtn, 1);

  _clearFramesBtn = new QPushButton(QString::fromUtf8("\xF0\x9F\x97\x91"));
  _clearFramesBtn->setStyleSheet(kIconButtonDangerStyle);
  _clearFramesBtn->setFixedSize(36, 36);
  _clearFramesBtn->setToolTip("Kareleri Temizle");
  _clearFramesBtn->setAccessibleName("Kareleri Temizle");
  connect(_clearFramesBtn, &QPushButton::released, this, &CalibWindow::_handleClearFrames);
  btnRow->addWidget(_clearFramesBtn);

  layout->addLayout(btnRow);

  _statusLabel = new QLabel;
  _statusLabel->setWordWrap(true);
  layout->addWidget(_statusLabel);

  layout->addStretch();

  _ShowStatus("Görüntü bekleniyor...");
}

void CalibWindow::_handleDisconnect() {
  if (!_session) return;
  _disconnectBtn->setEnabled(false);
  delete _session;
  _session = nullptr;

  emit closed();
}

void CalibWindow::_handleFrameReady(const QImage &frame) {
  if (!_pview || frame.isNull()) return;
  _lastRawFrame = frame;
  _UpdateDisplayFrame();
}

void CalibWindow::_handleSessionStatus(const QString &msg, bool error) {
  _ShowStatus(msg.toStdString(), error);
}

bool CalibWindow::_LoadCalibrationValues(float &fx, float &fy, float &cx, float &cy,
                                          std::vector<float> &distCoeffs) {
  std::string path = _GetCalibrationFilePath(0);
  if (!QFile::exists(QString::fromStdString(path))) return false;

  cv::FileStorage fs(path, cv::FileStorage::READ);
  if (!fs.isOpened()) return false;

  fx = fy = cx = cy = 0.0f;
  distCoeffs.clear();
  fs["fx"] >> fx;
  fs["fy"] >> fy;
  fs["cx"] >> cx;
  fs["cy"] >> cy;
  fs["distortion_coeffs"] >> distCoeffs;
  fs.release();

  return fx > 0.0f && fy > 0.0f && !distCoeffs.empty();
}

bool CalibWindow::_LoadCalibrationForUndistort() {
  float fx, fy, cx, cy;
  std::vector<float> dc;
  if (!_LoadCalibrationValues(fx, fy, cx, cy, dc)) return false;

  _cameraMatrix = (cv::Mat_<double>(3, 3) << fx, 0, cx, 0, fy, cy, 0, 0, 1);
  _distCoeffs = cv::Mat((int) dc.size(), 1, CV_64F);
  for (size_t i = 0; i < dc.size(); ++i) _distCoeffs.at<double>((int) i) = dc[i];
  return true;
}

void CalibWindow::_handleUndistortToggled(bool checked) {
  _undistortEnabled = checked;
  _UpdateDisplayFrame();
}

void CalibWindow::_handlePushUndistort() {
  float fx, fy, cx, cy;
  std::vector<float> dc;
  if (!_LoadCalibrationValues(fx, fy, cx, cy, dc)) {
    _ShowStatus("Gönderilecek bir kalibrasyon dosyası bulunamadı.", true);
    return;
  }

  QStringList args{
    QString::fromUtf8(COPY_UNDISTORT_PATH), QString::fromStdString(_camera.device),
    QString::number(fx, 'f', 6), QString::number(fy, 'f', 6),
    QString::number(cx, 'f', 6), QString::number(cy, 'f', 6)
  };
  for (float v : dc) args << QString::number(v, 'f', 9);

  _pushUndistortBtn->setEnabled(false);
  QProcess pushProcess;
  ConfigureSshpassProcess(&pushProcess, _camera.sshTarget, _camera.sshPassword, args);
  pushProcess.start();
  const int kPushTimeoutMs = 10000;
  bool finished = pushProcess.waitForFinished(kPushTimeoutMs);
  _pushUndistortBtn->setEnabled(true);

  if (!finished || pushProcess.exitStatus() != QProcess::NormalExit || pushProcess.exitCode() != 0) {
    if (!finished) pushProcess.kill();
    QString err = QString::fromUtf8(pushProcess.readAllStandardError()).trimmed();
    _ShowStatus("Robota gönderilemedi: "
                + (err.isEmpty() ? "bilinmeyen hata" : err.toStdString()), true);
    return;
  }
  _ShowStatus("Kalibrasyon robota gönderildi.");
}

void CalibWindow::_UpdateDisplayFrame() {
  if (!_pview || _lastRawFrame.isNull()) return;

  if (!_undistortEnabled || _cameraMatrix.empty()) {
    _pview->setFrame(_lastRawFrame);
    return;
  }

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
    "4. Kalibrasyon için en az %1 kare gerekir; önerilen aralık kamera başına 10–20 karedir. "
    "Ne kadar çeşitli açı ve mesafe kullanırsanız sonuç o kadar güvenilir olur.\n"
    "5. Yeterli kare biriktikten sonra 'KALİBRE ET' düğmesine basın.\n"
    "6. Kalibrasyon tamamlandığında 'Distorsiyonu Düzelt' düğmesini işaretleyerek "
    "sonucu görsel olarak kontrol edebilirsiniz.\n\n"
    "Kullanılan tahta ayarları: %2 x %3 kare, kare boyutu %4 mm, marker boyutu %5 mm.")
    .arg(CalibTools::kMinFrameCount)
    .arg(_settings.squaresX)
    .arg(_settings.squaresY)
    .arg(_settings.squareLengthM * 1000.0, 0, 'f', 1)
    .arg(_settings.markerLengthM * 1000.0, 0, 'f', 1);

  QMessageBox::information(this, "Kalibrasyon Talimatları", msg);
}

std::string CalibWindow::_ResolveDataDir(const std::string &category) {
  QDir dir(QCoreApplication::applicationDirPath());
  dir.cdUp();
  std::string host = _camera.sshTarget;
  size_t atPos = host.find('@');
  if (atPos != std::string::npos) host = host.substr(atPos + 1);
  QString relPath = QString::fromStdString(category) + "/"
                   + QString::fromStdString(CalibTools::SanitizeFolderName(host)) + "/"
                   + QString::fromStdString(CalibTools::SanitizeFolderName(_camera.name));
  dir.mkpath(relPath);
  return dir.filePath(relPath).toStdString();
}

std::string CalibWindow::_GetFramesDirectory() {
  return _ResolveDataDir("Frames") + "/";
}

std::string CalibWindow::_GetCalibrationFilePath(int cameraIndex) {
  return _ResolveDataDir("Calibrations") + "/" + std::to_string(cameraIndex) + ".yaml";
}

// Syncs _savedFrameCount/_frameIndex with what's actually on disk.
void CalibWindow::_RefreshSavedFrameCount() {
  std::vector<cv::String> filenames;
  CalibTools::GlobFrameFiles(_GetFramesDirectory(), &filenames);

  _savedFrameCount = (int) filenames.size();
  _frameIndex = 0;
  for (const cv::String &fn : filenames) {
    bool isNumeric = false;
    int idx = QFileInfo(QString::fromStdString(fn)).completeBaseName().toInt(&isNumeric);
    if (isNumeric && idx > _frameIndex) _frameIndex = idx;
  }
}

void CalibWindow::_UpdateSaveFrameCounter() {
  _saveFrameCounter->setText(QString("Kare: %1").arg(_savedFrameCount));
}

void CalibWindow::_ShowStatus(const std::string &msg, bool error) {
  _statusLabel->setText(QString::fromStdString(msg));
  _statusLabel->setStyleSheet(error ? "color: #c0392b;" : "color: #2c3e50;");
}

void CalibWindow::_handleSaveFrame() {
  if (!_session) return;

  // Always the raw (distorted) frame, regardless of the undistort preview toggle 
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

  if (_savedFrameCount < CalibTools::kMinFrameCount) {
    _ShowStatus(QString("Kalibrasyon için en az %1 kare gereklidir (şu an: %2).")
                  .arg(CalibTools::kMinFrameCount).arg(_savedFrameCount).toStdString(), true);
    return;
  }

  if (!_calibWatcher) {
    _calibWatcher = new QFutureWatcher<CalibrationResult>(this);
    connect(_calibWatcher, &QFutureWatcher<CalibrationResult>::finished, this, &CalibWindow::_handleCalibrationFinished);
  }

  // image decode, ChArUco detection, bundle adjustment runs on a QtConcurrent worker thread via CalibTools::RunCalibration
  _calibrateBtn->setEnabled(false);
  _saveFrameBtn->setEnabled(false);
  _clearFramesBtn->setEnabled(false);
  _ShowStatus("Kalibrasyon çalışıyor, lütfen bekleyin...");

  _calibWatcher->setFuture(QtConcurrent::run(&CalibTools::RunCalibration, _GetFramesDirectory(), _settings));
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

  bool hasCalib = _LoadCalibrationForUndistort();
  _undistortBtn->setEnabled(hasCalib);
  _pushUndistortBtn->setEnabled(hasCalib);
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
  CalibTools::GlobFrameFiles(path, &filenames);
  int removed = 0;
  for (const cv::String &fn : filenames) {
    if (QFile::remove(QString::fromStdString(fn))) ++removed;
  }

  _frameIndex = 0;
  _savedFrameCount = 0;
  _UpdateSaveFrameCounter();
  return removed;
}
