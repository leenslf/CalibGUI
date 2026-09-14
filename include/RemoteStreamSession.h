#ifndef REMOTESTREAMSESSION_H
#define REMOTESTREAMSESSION_H

#include <QImage>
#include <QObject>
#include <QProcess>
#include <QString>

#include <gst/gst.h>

#include "config.h"

class QTimer;

/** Owns the local GStreamer receive pipeline and the remote SSH `streamserver.sh`
    session for one RemoteCamera, for as long as this object is alive. Constructed by
    CalibWindow (one instance per connected camera); start() begins both (a) a local
    GStreamer pipeline listening on STREAM_PORT and (b) SSHing into the robot to run
    `streamserver.sh -d <device>`, which streams RTP raw video back for as long as
    that SSH session stays alive.

    Delivers frames via frameReady() (already deep-copied off the GStreamer thread)
    and forwards every user-facing status update via statusMessage(). Destroying this object stops
    both the local pipeline and the remote stream. */
class RemoteStreamSession : public QObject {
  Q_OBJECT

public:
  explicit RemoteStreamSession(const RemoteCamera &camera, QObject *parent = nullptr);
  ~RemoteStreamSession();

  void start();

signals:
  void frameReady(const QImage &frame);
  void statusMessage(const QString &msg, bool error);

private slots:
  void _handleGstBusTimer();
  void _handleLivenessTimer();
  void _handleFrameReady(const QImage &frame);
  void _handleStreamProcessError(QProcess::ProcessError error);
  void _handleStreamProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
  void _StartReceivePipeline();
  void _StopReceivePipeline();
  void _StartStreamProcess();
  void _StopStreamProcess();

  static GstFlowReturn _OnNewSample(GstElement *sink, gpointer userData);

  RemoteCamera _camera;

  GstElement *_pipeline = nullptr;
  GstElement *_appsink  = nullptr;
  GstBus     *_bus      = nullptr;

  QTimer   *_busTimer = nullptr;
  QTimer   *_livenessTimer = nullptr;
  QProcess *_streamProcess = nullptr;

  bool   _stalled = false;
  // Set at the top of the destructor, before any teardown call -- guards the slots
  // above against acting on a QProcess/timer signal that fires mid-teardown (e.g.
  // _streamProcess->terminate() below re-triggering _handleStreamProcessFinished).
  bool   _tearingDown = false;
  qint64 _lastFrameTimeMs = 0;
};

#endif
