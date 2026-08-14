#ifndef CAMERAWIDGET_H
#define CAMERAWIDGET_H

#include <QQuickWidget>
#include <QLabel>
#include <QImage>
#include <gst/gst.h>

class CameraWidget : public QWidget {
  Q_OBJECT

public:
  CameraWidget(bool qtqml = false, QWidget *parent = nullptr);
  bool SetQmlGlSink(GstElement *qmlglsink);
  unsigned long GetWindow();

  void setState( bool on );
  bool getState() { return !_off->isVisible(); }
  void setStall( bool hwstall, bool netstall );

  // Grabs whatever is currently rendered by the qmlglsink video item, straight
  // off the GPU. Null image if this widget isn't in qtqml mode or nothing has
  // been rendered yet.
  QImage grabFrame();

 protected:
  // Keeps the widget's height matched to its width at a natural 16:9 aspect ratio,
  // so the video stream fills the box edge-to-edge instead of being letterboxed by
  // GstGLVideoItem inside a mismatched rect.
  void resizeEvent(QResizeEvent *event) override;

 private:
  QLabel       * _cam = nullptr;
  QLabel       * _off = nullptr;
  QLabel       * _stall = nullptr;
  QQuickWidget * _camqtqml = nullptr;
};

#endif /* CAMERAWIDGET_H */

