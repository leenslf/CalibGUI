#include <algorithm>
#include <set>
#include <string>

#include <QStackedWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QTimer>
#include <QProcess>

#include "MainWindow.h"
#include "CalibWindow.h"

#include "config.h"
#include "SshProcess.h"
#include "Theme.h"
#undef DEBUG_LOCAL
#include "debuglog.h"

// Extracts the node name from a v4l2 device node path, e.g. "/dev/video0" -> "video0".
// Used as the folder-naming key (Frames/<name>/, Calibrations/<name>/) since it always
// uniquely identifies which node was picked, unlike the v4l2-ctl camera label (which
// can be long, spacey, or repeated across robots).
static std::string DeviceIndexName(const std::string &device) {
  size_t slash = device.find_last_of('/');
  return (slash != std::string::npos) ? device.substr(slash + 1) : device;
}

// Parses the "Supported formats per device:" block of `streamserver.sh -l`'s
// stdout -- a per-device `v4l2-ctl --list-formats-ext` dump -- into the set of
// device paths that list at least one supported capture format. Many physical
// cameras expose multiple /dev/videoN nodes (e.g. a real capture node plus a
// metadata-only sibling with zero supported formats); this lets
// ParseStreamserverCameraList tell them apart instead of assuming the first node
// listed under a camera is always the capture-capable one.
//
// `outAllDevices`, if non-null, additionally collects every device header seen
// in this block regardless of capability -- this is the authoritative set of
// device tokens streamserver.sh actually probed (one per real /dev/videoN
// node, but already rewritten to its map_cams.sh name, e.g. "/dev/frontcam",
// when one exists), which ParseStreamserverCameraList uses to recognize device
// lines in "Connected cameras:" that no longer look like "/dev/videoN".
static std::set<std::string> ParseCapableDevices(
    const std::string &stdoutText, std::set<std::string> *outAllDevices = nullptr) {
  std::set<std::string> capable;
  QString text = QString::fromStdString(stdoutText);
  int start = text.indexOf("Supported formats per device:");
  if (start < 0) return capable;

  QString currentDevice;
  const QStringList lines = text.mid(start).split('\n');
  for (const QString &rawLine : lines) {
    QString trimmed = rawLine.trimmed();
    if (trimmed.startsWith("--- ") && trimmed.endsWith(" ---")) {
      currentDevice = trimmed.mid(4, trimmed.length() - 8).trimmed();
      if (outAllDevices && !currentDevice.isEmpty()) outAllDevices->insert(currentDevice.toStdString());
    } else if (!currentDevice.isEmpty() && trimmed.startsWith("[") && trimmed.contains("]:")) {
      capable.insert(currentDevice.toStdString());
    }
  }
  return capable;
}

// Parses the "Connected cameras:" block of `streamserver.sh -l`'s stdout (i.e.
// `v4l2-ctl --list-devices`'s own output): groups separated by blank lines, a
// non-indented header line ending in ':' names the physical camera, followed by one
// or more indented device-node lines -- normally "/dev/videoN", but streamserver.sh
// rewrites these to their map_cams.sh name when one is mapped (e.g. "/dev/frontcam"),
// so lines are recognized against allDevices (from ParseCapableDevices) rather than
// a hardcoded "/dev/video" prefix. Of those, the first one that ParseCapableDevices
// confirms actually supports capture is kept (falling back to the first line listed
// if none are confirmed capable, e.g. if that section failed to parse).
static std::vector<RemoteCamera> ParseStreamserverCameraList(
    const std::string &stdoutText, const std::string &sshTarget,
    const std::string &sshPassword) {
  std::vector<RemoteCamera> result;
  std::set<std::string> allDevices;
  std::set<std::string> capableDevices = ParseCapableDevices(stdoutText, &allDevices);
  QString text = QString::fromStdString(stdoutText);

  int start = text.indexOf("Connected cameras:");
  if (start < 0) return result;
  start += QString("Connected cameras:").length();
  int end = text.indexOf("Supported formats per device:", start);
  QString block = (end < 0) ? text.mid(start) : text.mid(start, end - start);

  QString currentLabel;
  std::vector<std::string> currentDevices;

  auto flushGroup = [&]() {
    if (currentLabel.isEmpty() || currentDevices.empty()) return;
    std::string chosen = currentDevices.front();
    for (const std::string &d : currentDevices) {
      if (capableDevices.count(d)) { chosen = d; break; }
    }
    RemoteCamera c;
    c.sshTarget = sshTarget;
    c.sshPassword = sshPassword;
    c.label = currentLabel.toStdString();
    c.device = chosen;
    c.name = DeviceIndexName(chosen);
    result.push_back(c);
  };

  const QStringList lines = block.split('\n');
  for (const QString &rawLine : lines) {
    QString trimmed = rawLine.trimmed();
    if (trimmed.isEmpty()) {
      flushGroup();
      currentLabel.clear();
      currentDevices.clear();
      continue;
    }
    bool indented = rawLine.startsWith(' ') || rawLine.startsWith('\t');
    if (!indented && trimmed.endsWith(':')) {
      flushGroup();  // defensive: in case a group isn't blank-line-terminated
      currentLabel = trimmed.chopped(1).trimmed();
      currentDevices.clear();
    } else if (indented && !currentLabel.isEmpty() && allDevices.count(trimmed.toStdString())) {
      currentDevices.push_back(trimmed.toStdString());
    }
  }
  flushGroup();  // last group has no trailing blank line before EOF

  return result;
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
  setWindowTitle("CalibGUI");

  QVBoxLayout *root = new QVBoxLayout(this);
  _stack = new QStackedWidget;
  root->addWidget(_stack);

  _initUI();
  _stack->addWidget(_connectPage);
  _stack->setCurrentWidget(_connectPage);

  // Fullscreen/maximize breaks the layout irreversibly, so lock the window size.
  setFixedSize(900, 700);
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

  QLabel *label = new QLabel("SSH ile bağlan:");
  label->setStyleSheet(kHeaderLabelStyle);
  layout->addWidget(label);

  QFormLayout *form = new QFormLayout;
  _sshTargetLineEdit = new QLineEdit;
  _sshTargetLineEdit->setPlaceholderText("ör. kullanici@192.168.1.50");
  connect(_sshTargetLineEdit, &QLineEdit::returnPressed, this, &MainWindow::_handleIpSubmit);
  form->addRow("Kullanıcı@IP:", _sshTargetLineEdit);

  _passwordLineEdit = new QLineEdit;
  _passwordLineEdit->setEchoMode(QLineEdit::Password);
  connect(_passwordLineEdit, &QLineEdit::returnPressed, this, &MainWindow::_handleIpSubmit);
  form->addRow("Şifre:", _passwordLineEdit);
  layout->addLayout(form);

  QHBoxLayout *ipRow = new QHBoxLayout;
  ipRow->addStretch();
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
  // Temporarily disabled -- see MainWindow::_initCameraPickPage for the matching exitBtn.
  // QPushButton *exitBtn = new QPushButton("Çıkış");
  // exitBtn->setStyleSheet(kSecondaryButtonStyle);
  // exitBtn->setMinimumWidth(90);
  // connect(exitBtn, &QPushButton::released, this, &QWidget::close);
  // actionRow->addWidget(exitBtn);
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
  // Temporarily disabled -- see MainWindow::_initIpEntryPage for the matching exitBtn.
  // QPushButton *exitBtn = new QPushButton("Çıkış");
  // exitBtn->setStyleSheet(kSecondaryButtonStyle);
  // exitBtn->setMinimumWidth(90);
  // connect(exitBtn, &QPushButton::released, this, &QWidget::close);
  // actionRow->addWidget(exitBtn);
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
  std::string target = _sshTargetLineEdit->text().trimmed().toStdString();
  if (target.empty() || _listProcess) return;
  _sshTarget = target;
  _sshPassword = _passwordLineEdit->text().toStdString();
  _StartListProcess();
}

void MainWindow::_handleRefreshCameras() {
  if (_sshTarget.empty() || _listProcess) return;
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
  ConfigureSshpassProcess(_listProcess, _sshTarget, _sshPassword,
                          { QString::fromUtf8(STREAMSERVER_PATH), "-l" });
  connect(_listProcess, &QProcess::errorOccurred, this, &MainWindow::_handleListProcessError);
  connect(_listProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &MainWindow::_handleListProcessFinished);
  _listProcess->start();
}

void MainWindow::_handleListProcessFinished(int exitCode, QProcess::ExitStatus status) {
  QByteArray out = _listProcess->readAllStandardOutput();
  QByteArray err = _listProcess->readAllStandardError();
  _listProcess->deleteLater();
  _listProcess = nullptr;
  _ipSubmitBtn->setEnabled(true);
  _refreshBtn->setEnabled(true);

  // streamserver.sh -l can exit non-zero (e.g. v4l2-ctl failing to query a
  // device still held busy by another process) while having already printed a
  // usable "Connected cameras:" block to stdout; only treat this as a hard
  // connection failure if we didn't actually get a camera list out of it.
  bool ok = status == QProcess::NormalExit && exitCode == 0;
  if (!ok && !out.contains("Connected cameras:")) {
    _pickStatusLabel->setText(QString("Bağlanılamadı: %1").arg(
      err.isEmpty() ? QString("bilinmeyen hata") : QString::fromUtf8(err).trimmed()));
    return;
  }

  _cameras = ParseStreamserverCameraList(out.toStdString(), _sshTarget, _sshPassword);
  for (const RemoteCamera &c : _cameras) {
    _cameraListWidget->addItem(QString::fromStdString(c.label) + "  ("
      + QString::fromStdString(c.device) + ")");
  }
  _UpdateCameraListHeight();
  _pickStatusLabel->setText(
    _cameras.empty() ? "Bu makinede bağlı kamera bulunamadı." : "");
}

void MainWindow::_handleListProcessError(QProcess::ProcessError error) {
  if (!_listProcess || error != QProcess::FailedToStart) return;
  _pickStatusLabel->setText("sshpass çalıştırılamadı (sistemde kurulu mu? "
                             "'sudo apt install sshpass').");
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

  _calibWindow = new CalibWindow(camera);
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
