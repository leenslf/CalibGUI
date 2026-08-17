// Ported from RobotGUI/src/CameraWidget.cpp — keep in sync manually if that file changes.
#include <QHBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QResizeEvent>

#include "config.h"
#include "CameraWidget.h"

#undef DEBUG_LOCAL
#include "debuglog.h"

CameraWidget::CameraWidget(QWidget *parent) : QWidget(parent) {
  DBGPRINT("CameraWidget::CameraWidget\n");

  QGridLayout *l = new QGridLayout;
  l->setHorizontalSpacing(0);
  l->setVerticalSpacing(0);
  l->setContentsMargins( 0,0,0,0 );
  this->setLayout(l);

  _off = new QLabel(tr("Kamera kapalı"));
  SETCLASS(_off, "cwlabel");
  _off->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
  l->addWidget(_off, 0, 0, 40, 60);
  _off->setVisible(false);

  _cam = new QLabel;
  SETCLASS(_cam, "cwlabel");
  _cam->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
  l->addWidget(_cam, 0, 0, 40, 60);

  _stall = new QLabel(QString::fromUtf8(""));
  SETCLASS(_stall, "cwstate cwstategrn");
  l->addWidget(_stall, 38, 1, 1, 1);
}

void CameraWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  int width = event->size().width();
  if (width <= 0) return;
  int desiredHeight = width * 9 / 16;
  if (desiredHeight > 0 && desiredHeight != height()) setFixedHeight(desiredHeight);
}

void CameraWidget::setState( bool on ) {
  _off->setVisible(!on);
  _stall->setVisible(on);
}

void CameraWidget::setStall( bool hwstall, bool netstall) {
  if (hwstall) {
    SETCLASS(_stall, "cwstate cwstatered");
    _stall->setText(QString::fromUtf8(""));
  } else if (netstall) {
    SETCLASS(_stall, "cwstate cwstateorn");
    _stall->setText(QString::fromUtf8(""));
  } else {
    SETCLASS(_stall, "cwstate cwstategrn");
    _stall->setText(QString::fromUtf8(""));
  }
}

void CameraWidget::setFrame(const QImage &img) {
  _lastFrame = img;
  _cam->setPixmap(QPixmap::fromImage(img).scaled(
    _cam->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
