#ifndef CAMERAWIDGET_H
#define CAMERAWIDGET_H

#include <QLabel>
#include <QImage>

class CameraWidget : public QWidget {
  Q_OBJECT

public:
  CameraWidget(QWidget *parent = nullptr);

  void setState( bool on );
  bool getState() { return !_off->isVisible(); }
  void setStall( bool hwstall, bool netstall );

  // Displays the given frame and remembers it as the full-resolution frame
  // returned by grabFrame().
  void setFrame(const QImage &img);

  // Returns the last frame passed to setFrame(), at full resolution (not the
  // possibly-scaled pixmap shown on screen). Null image if none received yet.
  QImage grabFrame() { return _lastFrame; }

 protected:
  // Keeps the widget's height matched to its width at a natural 16:9 aspect ratio,
  // so the video stream fills the box edge-to-edge instead of being letterboxed.
  void resizeEvent(QResizeEvent *event) override;

 private:
  QLabel * _cam = nullptr;
  QLabel * _off = nullptr;
  QLabel * _stall = nullptr;
  QImage   _lastFrame;
};

#endif /* CAMERAWIDGET_H */
