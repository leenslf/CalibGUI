// Ported from RobotGUI/src/CameraWidget.cpp — keep in sync manually if that file changes.
#include <QQuickItem>
#include <QHBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <gst/gst.h>

#include "config.h"
#include "CameraWidget.h"

#undef DEBUG_LOCAL
#include "debuglog.h"

CameraWidget::CameraWidget(bool qtqml, QWidget *parent) : QWidget(parent) {
  DBGPRINT("CameraWidget::CameraWidget qtqml=%d\n", qtqml);

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

  if (!qtqml) {
    _cam = new QLabel;
    SETCLASS(_cam, "cwlabel");
    _cam->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    l->addWidget(_cam, 0, 0, 40, 60);

  } else {
    _camqtqml = new QQuickWidget;
    _camqtqml->setSource(QUrl("qrc:/CameraWidget.qml"));
    _camqtqml->setResizeMode(QQuickWidget::SizeRootObjectToView);
    l->addWidget(_camqtqml, 0, 0, 40, 60);

  }

  _stall = new QLabel("\uf00a");
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
    _stall->setText("\ueb99");
  } else if (netstall) {
    SETCLASS(_stall, "cwstate cwstateorn");
    _stall->setText("\uf00a");
  } else {
    SETCLASS(_stall, "cwstate cwstategrn");
    _stall->setText("\uf00a");
  }
}

bool CameraWidget::SetQmlGlSink( GstElement *qmlglsink ) {
  DBGPRINT("CameraWidget::SetQmlGlSink sink=%p, obj=%p\n",
           qmlglsink, _camqtqml);
  if (!_camqtqml) return false;

  // qmlglsink was passed straight to g_object_set() unchecked. RHexAPI::GetCameraStreamQmlGlSink() 
  // can legitimately return nullptr (RHexAPIImpl.cc:2459, whenever that
  // camera's stream pipeline doesn't exist), most likely to happen right after a
  // reconnect, if a stream hasn't (re)initialized yet. Passing that straight to
  // g_object_set() is a null-GObject dereference.
  if (!qmlglsink) return false;

  QObject *video_item = _camqtqml->rootObject()->findChild<QObject *>("videoItem");
  DBGPRINT("Video item is %p\n", video_item);

  if (!video_item)  return false;

  g_object_set( qmlglsink, "widget", video_item, NULL );
  return true;
}

unsigned long CameraWidget::GetWindow() {
  
  return (_cam)?_cam->winId():0;
}
