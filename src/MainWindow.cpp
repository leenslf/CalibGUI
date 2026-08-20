#include <algorithm>
#include <string>

#include <QStackedWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QProcess>

#include <opencv2/core.hpp>

#include "MainWindow.h"
#include "CalibWindow.h"

#include "config.h"
#include "Theme.h"
#undef DEBUG_LOCAL
#include "debuglog.h"

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

static const QString kIconButtonStyle =
  "QPushButton {"
  "  background-color: " + kColorSurface + "; color: " + kColorTextMuted + ";"
  "  border: 1px solid " + kColorBorder + ";"
  "  border-radius: 6px; font-size: 16px; padding: 2px;"
  "}"
  "QPushButton:hover { background-color: " + kColorHover + "; }"
  "QPushButton:pressed { background-color: " + kColorBorder + "; }";

static const QString kCameraListStyle =
  "QListWidget {"
  "  background-color: " + kColorSurface + "; border: 1px solid " + kColorBorder + ";"
  "  border-radius: 6px; outline: none;"
  "}"
  "QListWidget::item { padding: 10px 12px; font-size: 13pt; color: " + kColorText + "; }"
  "QListWidget::item:hover:!selected { background-color: " + kColorHover + "; }"
  "QListWidget::item:selected { background-color: " + kColorAccent + "; color: white; }";

static const QString kHeaderLabelStyle =
  "color: " + kColorText + "; font-size: 13pt; font-weight: 600;";

static const QString kStatusLabelStyle =
  "color: " + kColorTextMuted + "; font-size: 11pt; padding: 4px 2px;";

// ===========================================================================================================

// Extracts the device index from a v4l2 device node path, e.g. "/dev/video0" -> "0".
// Used as the folder-naming key (Frames/<name>/, Calibrations/<name>/) since it's
// short and always uniquely identifies which node was picked, unlike the v4l2-ctl
// camera label (which can be long, spacey, or repeated across robots).
static std::string DeviceIndexName(const std::string &device) {
  size_t slash = device.find_last_of('/');
  std::string base = (slash != std::string::npos) ? device.substr(slash + 1) : device;
  static const std::string kPrefix = "video";
  if (base.compare(0, kPrefix.size(), kPrefix) == 0) base = base.substr(kPrefix.size());
  return base;
}

// Parses the "Connected cameras:" block of `streamserver.sh -l`'s stdout (i.e.
// `v4l2-ctl --list-devices`'s own output): groups separated by blank lines, a
// non-indented header line ending in ':' names the physical camera, followed by one
// or more indented "/dev/videoN" lines -- only the first device line per group is
// kept (the capture-capable node).
static std::vector<RemoteCamera> ParseStreamserverCameraList(
    const std::string &stdoutText, const std::string &robotIp) {
  std::vector<RemoteCamera> result;
  QString text = QString::fromStdString(stdoutText);

  int start = text.indexOf("Connected cameras:");
  if (start < 0) return result;
  start += QString("Connected cameras:").length();
  int end = text.indexOf("Supported formats per device:", start);
  QString block = (end < 0) ? text.mid(start) : text.mid(start, end - start);

  QString currentLabel;
  bool haveDeviceForCurrent = false;
  const QStringList lines = block.split('\n');
  for (const QString &rawLine : lines) {
    QString trimmed = rawLine.trimmed();
    if (trimmed.isEmpty()) {
      currentLabel.clear();
      haveDeviceForCurrent = false;
      continue;
    }
    bool indented = rawLine.startsWith(' ') || rawLine.startsWith('\t');
    if (!indented && trimmed.endsWith(':')) {
      currentLabel = trimmed.chopped(1).trimmed();
      haveDeviceForCurrent = false;
    } else if (indented && trimmed.startsWith("/dev/video") &&
               !currentLabel.isEmpty() && !haveDeviceForCurrent) {
      RemoteCamera c;
      c.robotIp = robotIp;
      c.label = currentLabel.toStdString();
      c.device = trimmed.toStdString();
      c.name = DeviceIndexName(c.device);
      result.push_back(c);
      haveDeviceForCurrent = true;
    }
  }
  return result;
}

// ===========================================================================================================

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
  setWindowTitle("CalibGUI");

  _LoadSettings();

  QVBoxLayout *root = new QVBoxLayout(this);
  _stack = new QStackedWidget;
  root->addWidget(_stack);

  _initUI();
  _stack->addWidget(_connectPage);
  _stack->setCurrentWidget(_connectPage);

  resize(900, 700);
}

MainWindow::~MainWindow() {
  if (_calibWindow) {
    delete _calibWindow;
    _calibWindow = nullptr;
  }
  if (_listProcess) {
    _listProcess->kill();
    _listProcess->deleteLater();
    _listProcess = nullptr;
  }
}

void MainWindow::_initUI() {
  _connectPage = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout(_connectPage);

  QLabel *headerLabel = new QLabel("CalibGUI");
  headerLabel->setStyleSheet(kHeaderLabelStyle);
  layout->addWidget(headerLabel);

  _connectSubStack = new QStackedWidget;
  layout->addWidget(_connectSubStack);

  _initIpEntryPage();
  _initCameraPickPage();
  _connectSubStack->addWidget(_ipEntryPage);
  _connectSubStack->addWidget(_cameraPickPage);
  _connectSubStack->setCurrentWidget(_ipEntryPage);
}

void MainWindow::_initIpEntryPage() {
  _ipEntryPage = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout(_ipEntryPage);

  QLabel *label = new QLabel("Robot IP adresi:");
  label->setStyleSheet(kHeaderLabelStyle);
  layout->addWidget(label);

  QHBoxLayout *ipRow = new QHBoxLayout;
  _ipLineEdit = new QLineEdit;
  _ipLineEdit->setPlaceholderText("ör. 192.168.1.50");
  connect(_ipLineEdit, &QLineEdit::returnPressed, this, &MainWindow::_handleIpSubmit);
  ipRow->addWidget(_ipLineEdit);
  _ipSubmitBtn = new QPushButton("Kameraları Listele");
  _ipSubmitBtn->setStyleSheet(kPrimaryButtonStyle);
  connect(_ipSubmitBtn, &QPushButton::released, this, &MainWindow::_handleIpSubmit);
  ipRow->addWidget(_ipSubmitBtn);
  layout->addLayout(ipRow);

  _ipStatusLabel = new QLabel;
  _ipStatusLabel->setWordWrap(true);
  _ipStatusLabel->setStyleSheet(kStatusLabelStyle);
  layout->addWidget(_ipStatusLabel);
  layout->addStretch();

  QHBoxLayout *actionRow = new QHBoxLayout;
  actionRow->addStretch();
  QPushButton *exitBtn = new QPushButton("Çıkış");
  exitBtn->setStyleSheet(kSecondaryButtonStyle);
  exitBtn->setMinimumWidth(90);
  connect(exitBtn, &QPushButton::released, this, &QWidget::close);
  actionRow->addWidget(exitBtn);
  layout->addLayout(actionRow);
}

void MainWindow::_initCameraPickPage() {
  _cameraPickPage = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout(_cameraPickPage);

  QHBoxLayout *headerRow = new QHBoxLayout;
  QLabel *headerLabel = new QLabel("Bulunan kameralar:");
  headerLabel->setStyleSheet(kHeaderLabelStyle);
  headerRow->addWidget(headerLabel);
  headerRow->addStretch();
  _refreshBtn = new QPushButton(QString::fromUtf8("\xE2\x86\xBB"));
  _refreshBtn->setToolTip("Kameraları Yenile");
  _refreshBtn->setFixedSize(36, 36);
  _refreshBtn->setStyleSheet(kIconButtonStyle);
  connect(_refreshBtn, &QPushButton::released, this, &MainWindow::_handleRefreshCameras);
  headerRow->addWidget(_refreshBtn);
  layout->addLayout(headerRow);

  // Sized to content
  _cameraListWidget = new QListWidget;
  _cameraListWidget->setStyleSheet(kCameraListStyle);
  _cameraListWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  connect(_cameraListWidget, &QListWidget::currentRowChanged, this,
          &MainWindow::_handleCameraSelectionChanged);
  layout->addWidget(_cameraListWidget);

  _pickStatusLabel = new QLabel;
  _pickStatusLabel->setWordWrap(true);
  _pickStatusLabel->setStyleSheet(kStatusLabelStyle);
  layout->addWidget(_pickStatusLabel);

  QHBoxLayout *actionRow = new QHBoxLayout;
  _backBtn = new QPushButton("Farklı Robot");
  _backBtn->setStyleSheet(kSecondaryButtonStyle);
  connect(_backBtn, &QPushButton::released, this, &MainWindow::_handleBackToIpEntry);
  actionRow->addWidget(_backBtn);
  actionRow->addStretch();
  _connectBtn = new QPushButton("Bağlan");
  _connectBtn->setEnabled(false);
  _connectBtn->setStyleSheet(kPrimaryButtonStyle);
  _connectBtn->setMinimumWidth(160);
  connect(_connectBtn, &QPushButton::released, this, &MainWindow::_handleConnect);
  actionRow->addWidget(_connectBtn);
  actionRow->addSpacing(12);
  QPushButton *exitBtn = new QPushButton("Çıkış");
  exitBtn->setStyleSheet(kSecondaryButtonStyle);
  exitBtn->setMinimumWidth(90);
  connect(exitBtn, &QPushButton::released, this, &QWidget::close);
  actionRow->addWidget(exitBtn);
  actionRow->addStretch();
  layout->addLayout(actionRow);
}

void MainWindow::_UpdateCameraListHeight() {
  const int kMinRows = 1;
  const int kMaxRows = 6;
  int rows = std::max(kMinRows, std::min(kMaxRows, (int) _cameras.size()));
  int rowHeight = _cameraListWidget->sizeHintForRow(0);
  if (rowHeight <= 0) rowHeight = _cameraListWidget->fontMetrics().height() + 20;
  int frame = 2 * _cameraListWidget->frameWidth();
  int height = rows * rowHeight + frame + 4;
  _cameraListWidget->setMinimumHeight(height);
  _cameraListWidget->setMaximumHeight(height);
}

void MainWindow::_handleCameraSelectionChanged() {
  _connectBtn->setEnabled(_cameraListWidget->currentRow() >= 0);
}

void MainWindow::_handleIpSubmit() {
  std::string ip = _ipLineEdit->text().trimmed().toStdString();
  if (ip.empty() || _listProcess) return;
  _robotIp = ip;
  _StartListProcess();
}

void MainWindow::_handleRefreshCameras() {
  if (_robotIp.empty() || _listProcess) return;
  _StartListProcess();
}

void MainWindow::_StartListProcess() {
  _ipSubmitBtn->setEnabled(false);
  _refreshBtn->setEnabled(false);
  _connectBtn->setEnabled(false);
  _cameras.clear();
  _cameraListWidget->clear();
  _connectSubStack->setCurrentWidget(_cameraPickPage);
  _pickStatusLabel->setText("Kameralar listeleniyor...");
  _UpdateCameraListHeight();

  _listProcess = new QProcess(this);
  QStringList args {
    "-o", "BatchMode=yes",
    "-o", QString("ConnectTimeout=%1").arg(SSH_CONNECT_TIMEOUT_SECS),
    "-o", "StrictHostKeyChecking=accept-new",
    QString("%1@%2").arg(SSH_USER, QString::fromStdString(_robotIp)),
    QString::fromUtf8(STREAMSERVER_PATH), "-l"
  };
  connect(_listProcess, &QProcess::errorOccurred, this, &MainWindow::_handleListProcessError);
  connect(_listProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &MainWindow::_handleListProcessFinished);
  _listProcess->start("ssh", args);
}

void MainWindow::_handleListProcessFinished(int exitCode, QProcess::ExitStatus status) {
  QByteArray out = _listProcess->readAllStandardOutput();
  QByteArray err = _listProcess->readAllStandardError();
  _listProcess->deleteLater();
  _listProcess = nullptr;
  _ipSubmitBtn->setEnabled(true);
  _refreshBtn->setEnabled(true);

  if (status != QProcess::NormalExit || exitCode != 0) {
    _pickStatusLabel->setText(QString("Robota bağlanılamadı: %1").arg(
      err.isEmpty() ? QString("bilinmeyen hata") : QString::fromUtf8(err).trimmed()));
    return;
  }

  _cameras = ParseStreamserverCameraList(out.toStdString(), _robotIp);
  for (const RemoteCamera &c : _cameras) {
    _cameraListWidget->addItem(QString::fromStdString(c.label) + "  ("
      + QString::fromStdString(c.device) + ")");
  }
  _UpdateCameraListHeight();
  _pickStatusLabel->setText(
    _cameras.empty() ? "Bu robotta bağlı kamera bulunamadı." : "");
}

void MainWindow::_handleListProcessError(QProcess::ProcessError error) {
  if (!_listProcess || error != QProcess::FailedToStart) return;
  _pickStatusLabel->setText("ssh çalıştırılamadı (sistemde kurulu mu?).");
  _listProcess->deleteLater();
  _listProcess = nullptr;
  _ipSubmitBtn->setEnabled(true);
  _refreshBtn->setEnabled(true);
}

void MainWindow::_handleBackToIpEntry() {
  if (_listProcess) {
    _listProcess->kill();
    _listProcess->deleteLater();
    _listProcess = nullptr;
  }
  _cameras.clear();
  _cameraListWidget->clear();
  _connectBtn->setEnabled(false);
  _pickStatusLabel->clear();
  _connectSubStack->setCurrentWidget(_ipEntryPage);
}

void MainWindow::_handleConnect() {
  int row = _cameraListWidget->currentRow();
  if (row < 0 || row >= (int) _cameras.size() || _calibWindow) return;
  const RemoteCamera camera = _cameras[row];

  _calibWindow = new CalibWindow(camera, _settings);
  connect(_calibWindow, &CalibWindow::closed, this, &MainWindow::_handleCalibWindowClosed);
  _stack->addWidget(_calibWindow);
  _stack->setCurrentWidget(_calibWindow);

  QTimer::singleShot(0, _calibWindow, &CalibWindow::start);
}

void MainWindow::_handleCalibWindowClosed() {
  if (_calibWindow) {
    _connectSubStack->setCurrentWidget(_cameraPickPage);
    _stack->setCurrentWidget(_connectPage);
    _stack->removeWidget(_calibWindow);
    _calibWindow->deleteLater();
    _calibWindow = nullptr;
  }
}

std::string MainWindow::_ConfigFilePath() {
  QDir dir(QCoreApplication::applicationDirPath());
  dir.cdUp();
  dir.mkpath("config");
  return dir.filePath("config/config.yaml").toStdString();
}

void MainWindow::_LoadSettings() {
  _settings = CalibSettings();

  cv::FileStorage fs(_ConfigFilePath(), cv::FileStorage::READ);
  if (!fs.isOpened()) return;

  auto readInt = [&fs](const char *key, int &field) {
    cv::FileNode n = fs[key];
    if (!n.empty()) field = (int) n;
  };
  auto readFloat = [&fs](const char *key, float &field) {
    cv::FileNode n = fs[key];
    if (!n.empty()) field = (float) n;
  };
  readInt("squares_x", _settings.squaresX);
  readInt("squares_y", _settings.squaresY);
  readFloat("square_length_m", _settings.squareLengthM);
  readFloat("marker_length_m", _settings.markerLengthM);
  readInt("min_valid_frames", _settings.minValidFrames);
  fs.release();
}
