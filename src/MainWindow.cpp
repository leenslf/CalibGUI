#include <algorithm>
#include <string>

#include <QStackedWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>

#include <opencv2/core.hpp>

#include <udp_discovery_peer.hpp>
#include <udp_discovery_ip_port.hpp>

#include <retavis/GstManager.hh>

#include "MainWindow.h"
#include "CalibWindow.h"

#include "config.h"
#include "Theme.h"
#undef DEBUG_LOCAL
#include "debuglog.h"

// Ported from RobotGUI/src/MainWindow.cpp:132-164
static const unsigned short kDiscoveryPort = 12021;
static const uint64_t kDiscoveryApplicationId = 7681412;
static const unsigned int kDiscoveryMulticastAddress =
  (224 << 24) + (0 << 16) + (0 << 8) + 123;

static const int kCalibFrameCount = 30;

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

static const QString kRobotListStyle =
  "QListWidget {"
  "  background-color: " + kColorSurface + "; border: 1px solid " + kColorBorder + ";"
  "  border-radius: 6px; outline: none;"
  "}"
  "QListWidget::item { padding: 10px 12px; font-size: 13pt; color: " + kColorText + "; }"
  "QListWidget::item:hover:!selected { background-color: " + kColorHover + "; }"
  "QListWidget::item:selected { background-color: " + kColorAccent + "; color: white; }";

static const QString kHeaderLabelStyle =
  "color: " + kColorText + "; font-size: 13pt; font-weight: 600;";

// ===========================================================================================================

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
  setWindowTitle("CalibGUI");

  _gm = GstManager::getInstance();

  _LoadSettings();

  QVBoxLayout *root = new QVBoxLayout(this);
  _stack = new QStackedWidget;
  root->addWidget(_stack);

  _initUI();
  _stack->addWidget(_connectPage);
  _stack->setCurrentWidget(_connectPage);

  udpdiscovery::PeerParameters parameters;
  _peer = new udpdiscovery::Peer;
  parameters.set_can_use_broadcast(true);
  parameters.set_multicast_group_address(kDiscoveryMulticastAddress);
  parameters.set_can_use_multicast(true);
  parameters.set_can_discover(true);
  parameters.set_can_be_discovered(true);
  parameters.set_port(kDiscoveryPort);
  parameters.set_application_id(kDiscoveryApplicationId);
  if (!_peer->Start(parameters, "discover")) {
    ERRPRINTF("MainWindow: Could not start peer discovery service\n");
    delete _peer;
    _peer = nullptr;
  }

  _discoveryTimer = new QTimer(this);
  connect(_discoveryTimer, &QTimer::timeout, this, &MainWindow::_pollDiscovery);
  _discoveryTimer->start(1000);

  resize(900, 700);
}

MainWindow::~MainWindow() {
  if (_calibWindow) {
    delete _calibWindow;
    _calibWindow = nullptr;
  }
  if (_peer) {
    _peer->Stop();
    delete _peer;
    _peer = nullptr;
  }
  // Release last: see the acquire comment in the constructor.
  if (_gm) {
    _gm->releaseInstance();
    _gm = nullptr;
  }
}

void MainWindow::_initUI() {
  _connectPage = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout(_connectPage);

  QHBoxLayout *headerRow = new QHBoxLayout;
  QLabel *headerLabel = new QLabel("Bulunan robotlar:");
  headerLabel->setStyleSheet(kHeaderLabelStyle);
  headerRow->addWidget(headerLabel);
  headerRow->addStretch();
  _settingsBtn = new QPushButton(QString::fromUtf8("\xE2\x9A\x99"));
  _settingsBtn->setToolTip("Kalibrasyon Ayarları");
  _settingsBtn->setFixedSize(36, 36);
  _settingsBtn->setStyleSheet(kIconButtonStyle);
  connect(_settingsBtn, &QPushButton::released, this, &MainWindow::_handleSettingsClicked);
  headerRow->addWidget(_settingsBtn);
  layout->addLayout(headerRow);

  // Sized to content 
  _robotListWidget = new QListWidget;
  _robotListWidget->setStyleSheet(kRobotListStyle);
  _robotListWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  connect(_robotListWidget, &QListWidget::currentRowChanged, this,
          &MainWindow::_handleRobotSelectionChanged);
  layout->addWidget(_robotListWidget);
  _UpdateRobotListHeight();

  QHBoxLayout *actionRow = new QHBoxLayout;
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

void MainWindow::_UpdateRobotListHeight() {
  const int kMinRows = 1;
  const int kMaxRows = 6;
  int rows = std::max(kMinRows, std::min(kMaxRows, (int) _robots.size()));
  int rowHeight = _robotListWidget->sizeHintForRow(0);
  if (rowHeight <= 0) rowHeight = _robotListWidget->fontMetrics().height() + 20;
  int frame = 2 * _robotListWidget->frameWidth();
  int height = rows * rowHeight + frame + 4;
  _robotListWidget->setMinimumHeight(height);
  _robotListWidget->setMaximumHeight(height);
}

void MainWindow::_pollDiscovery() {
  if (!_peer) return;
  std::list<udpdiscovery::DiscoveredPeer> discovered = _peer->ListDiscovered();

  // CalibGUI only ever shows a short list for a single connection, so it just rebuilds the list on every poll.
  std::vector<DiscoveredRobot> robots;
  for (const auto &peer : discovered) {
    std::string data = peer.user_data();
    size_t pos = data.find(':', 0);
    if (pos == std::string::npos) continue;
    std::string name = data.substr(0, pos);
    size_t pos2 = data.find(':', pos + 1);
    if (pos2 == std::string::npos) continue;
    std::string type = data.substr(pos + 1, pos2 - pos - 1);
    size_t pos3 = data.find(':', pos2 + 1);
    std::string portstr = data.substr(pos2 + 1, pos3 - pos2 - 1);

    DiscoveredRobot r;
    r.name = name;
    r.type = type;
    r.ip = udpdiscovery::IpToString(peer.ip_port().ip());
    try {
      r.port = std::stoi(portstr);
    } catch (const std::exception &) {
      continue;
    }
    robots.push_back(r);
  }

  // Preserve the current selection across a rebuild where possible.
  int selectedRow = _robotListWidget->currentRow();
  std::string selectedId = (selectedRow >= 0 && selectedRow < (int) _robots.size())
    ? _robots[selectedRow].ip + ":" + std::to_string(_robots[selectedRow].port) : "";

  _robots = robots;
  _robotListWidget->clear();
  int newSelectedRow = -1;
  for (size_t i = 0; i < _robots.size(); ++i) {
    const DiscoveredRobot &r = _robots[i];
    QString label = QString::fromStdString(r.name) + "  ("
      + QString::fromStdString(r.ip) + ":" + QString::number(r.port) + ")";
    _robotListWidget->addItem(label);
    std::string id = r.ip + ":" + std::to_string(r.port);
    if (id == selectedId) newSelectedRow = (int) i;
  }
  if (newSelectedRow >= 0) _robotListWidget->setCurrentRow(newSelectedRow);
  _UpdateRobotListHeight();
}

void MainWindow::_handleRobotSelectionChanged() {
  _connectBtn->setEnabled(_robotListWidget->currentRow() >= 0);
}

void MainWindow::_handleConnect() {
  int row = _robotListWidget->currentRow();
  if (row < 0 || row >= (int) _robots.size() || _calibWindow) return;
  const DiscoveredRobot robot = _robots[row];

  // CalibWindow attempts the actual connection itself
  _calibWindow = new CalibWindow(robot, _settings);
  connect(_calibWindow, &CalibWindow::closed, this, &MainWindow::_handleCalibWindowClosed);
  _stack->addWidget(_calibWindow);
  _stack->setCurrentWidget(_calibWindow);

  QTimer::singleShot(0, _calibWindow, &CalibWindow::start);
}

void MainWindow::_handleCalibWindowClosed() {
  if (_calibWindow) {
    _stack->setCurrentWidget(_connectPage);
    _stack->removeWidget(_calibWindow);
    _calibWindow->deleteLater();
    _calibWindow = nullptr;
  }
}

std::string MainWindow::_GetFramesDirectory() {
  QDir dir(QCoreApplication::applicationDirPath());
  dir.cdUp();
  dir.mkpath("Frames");
  return dir.filePath("Frames/").toStdString();
}

int MainWindow::_ClearSavedFrames() {
  std::string path = _GetFramesDirectory();
  std::vector<cv::String> filenames;
  cv::glob(path + "*.png", filenames, true);
  int removed = 0;
  for (const cv::String &fn : filenames) {
    if (QFile::remove(QString::fromStdString(fn))) ++removed;
  }
  return removed;
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

void MainWindow::_SaveSettings() {
  cv::FileStorage fs(_ConfigFilePath(), cv::FileStorage::WRITE);
  if (!fs.isOpened()) {
    ERRPRINTF("[EE] MainWindow::_SaveSettings: could not open %s for writing\n",
              _ConfigFilePath().c_str());
    return;
  }
  fs << "squares_x" << _settings.squaresX;
  fs << "squares_y" << _settings.squaresY;
  fs << "square_length_m" << _settings.squareLengthM;
  fs << "marker_length_m" << _settings.markerLengthM;
  fs << "min_valid_frames" << _settings.minValidFrames;
  fs.release();
}

void MainWindow::_handleSettingsClicked() {
  QDialog dialog(this);
  dialog.setWindowTitle("Kalibrasyon Ayarları");

  QFormLayout *form = new QFormLayout;

  QSpinBox *squaresXSpin = new QSpinBox;
  squaresXSpin->setRange(2, 20);
  squaresXSpin->setValue(_settings.squaresX);
  squaresXSpin->setToolTip("Kalibrasyon tahtasındaki kare sütunu sayısı.");
  form->addRow("Tahta genişliği (kare):", squaresXSpin);

  QSpinBox *squaresYSpin = new QSpinBox;
  squaresYSpin->setRange(2, 20);
  squaresYSpin->setValue(_settings.squaresY);
  squaresYSpin->setToolTip("Kalibrasyon tahtasındaki kare satırı sayısı.");
  form->addRow("Tahta yüksekliği (kare):", squaresYSpin);

  QDoubleSpinBox *squareLenSpin = new QDoubleSpinBox;
  squareLenSpin->setRange(1.0, 500.0);
  squareLenSpin->setDecimals(1);
  squareLenSpin->setSuffix(" mm");
  squareLenSpin->setValue(_settings.squareLengthM * 1000.0);
  squareLenSpin->setToolTip("Tahtadaki her bir satranç karesinin kenar uzunluğu.");
  form->addRow("Kare boyutu:", squareLenSpin);

  QDoubleSpinBox *markerLenSpin = new QDoubleSpinBox;
  markerLenSpin->setRange(1.0, 500.0);
  markerLenSpin->setDecimals(1);
  markerLenSpin->setSuffix(" mm");
  markerLenSpin->setValue(_settings.markerLengthM * 1000.0);
  markerLenSpin->setToolTip("Karelerin içindeki ArUco marker'ların kenar uzunluğu "
                             "(kare boyutundan küçük olmalıdır).");
  form->addRow("Marker boyutu:", markerLenSpin);

  QSpinBox *minValidSpin = new QSpinBox;
  minValidSpin->setRange(1, kCalibFrameCount);
  minValidSpin->setValue(_settings.minValidFrames);
  minValidSpin->setToolTip("Bir karenin kalibrasyonda kullanılması için üzerinde "
                            "tespit edilmesi gereken minimum köşe sayısı.");
  form->addRow("Min. geçerli kare sayısı:", minValidSpin);

  QVBoxLayout *dialogLayout = new QVBoxLayout(&dialog);
  dialogLayout->addLayout(form);

  QPushButton *resetBtn = new QPushButton("Varsayılana Dön");
  QDialogButtonBox *buttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QHBoxLayout *dialogBtnRow = new QHBoxLayout;
  dialogBtnRow->addWidget(resetBtn);
  dialogBtnRow->addStretch();
  dialogBtnRow->addWidget(buttons);
  dialogLayout->addLayout(dialogBtnRow);

  connect(resetBtn, &QPushButton::released, [&]() {
    CalibSettings d;
    squaresXSpin->setValue(d.squaresX);
    squaresYSpin->setValue(d.squaresY);
    squareLenSpin->setValue(d.squareLengthM * 1000.0);
    markerLenSpin->setValue(d.markerLengthM * 1000.0);
    minValidSpin->setValue(d.minValidFrames);
  });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) return;

  if (markerLenSpin->value() >= squareLenSpin->value()) {
    QMessageBox::warning(this, "Geçersiz Ayar",
                          "Marker boyutu, kare boyutundan küçük olmalıdır.");
    return;
  }
  bool boardChanged =
    squaresXSpin->value() != _settings.squaresX ||
    squaresYSpin->value() != _settings.squaresY ||
    !qFuzzyCompare(squareLenSpin->value(), _settings.squareLengthM * 1000.0) ||
    !qFuzzyCompare(markerLenSpin->value(), _settings.markerLengthM * 1000.0);

  if (boardChanged) {
    std::vector<cv::String> existingFrames;
    cv::glob(_GetFramesDirectory() + "*.png", existingFrames, true);
    int savedFrameCount = (int) existingFrames.size();

    if (savedFrameCount > 0) {
      QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Tahta Ayarları Değişti",
        QString("Şu anda %1 kare kaydedilmiş durumda. Tahta ayarlarını değiştirmek "
                "bu kareleri geçersiz kılar.\nKareler silinip yeni ayarlar uygulansın mı?")
          .arg(savedFrameCount),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (reply != QMessageBox::Yes) return;
      _ClearSavedFrames();
    }
  }

  _settings.squaresX = squaresXSpin->value();
  _settings.squaresY = squaresYSpin->value();
  _settings.squareLengthM = (float) (squareLenSpin->value() / 1000.0);
  _settings.markerLengthM = (float) (markerLenSpin->value() / 1000.0);
  _settings.minValidFrames = minValidSpin->value();

  _SaveSettings();
}
