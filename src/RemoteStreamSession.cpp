#include "RemoteStreamSession.h"

#include <QDateTime>
#include <QTimer>

#include <gst/video/video.h>

#include "SshProcess.h"
#include "config.h"
#undef DEBUG_LOCAL
#include "debuglog.h"

// How long without a received frame before we consider the stream unreachable.
static const qint64 kLivenessTimeoutMs = 4000;

// Bus-message poll interval
static const int kBusPollIntervalMs = 100;
static const int kLivenessCheckIntervalMs = 1000;

RemoteStreamSession::RemoteStreamSession(const RemoteCamera &camera, QObject *parent)
  : QObject(parent), _camera(camera) {
}

RemoteStreamSession::~RemoteStreamSession() {
  _tearingDown = true;
  _StopStreamProcess();
  _StopReceivePipeline();
}

void RemoteStreamSession::start() {
  _StartReceivePipeline();

  // No GLib main loop is running, so pump the GStreamer bus by polling it instead.
  _busTimer = new QTimer(this);
  connect(_busTimer, &QTimer::timeout, this, &RemoteStreamSession::_handleGstBusTimer);
  _busTimer->start(kBusPollIntervalMs);

  // udpsrc gives no bus event when the remote stream goes silent, so detect that
  // ourselves by watching for how long it's been since the last frame arrived.
  _livenessTimer = new QTimer(this);
  connect(_livenessTimer, &QTimer::timeout, this, &RemoteStreamSession::_handleLivenessTimer);
  _livenessTimer->start(kLivenessCheckIntervalMs);

  _StartStreamProcess();
}

void RemoteStreamSession::_StartReceivePipeline() {
  // this may need editing later if changes are made to the "server"
  std::string desc =
    "udpsrc port=" + std::to_string(STREAM_PORT) + " "
    "caps=\"application/x-rtp,media=video,clock-rate=90000,encoding-name=RAW,"
    "sampling=YCbCr-4:2:2,depth=(string)8,width=(string)1280,height=(string)720,"
    "colorimetry=SMPTE240M,payload=96\" "
    "! rtpvrawdepay ! videoconvert ! video/x-raw,format=RGB "
    "! appsink name=sink emit-signals=true max-buffers=1 drop=true sync=false";

  GError *error = nullptr;
  _pipeline = gst_parse_launch(desc.c_str(), &error);
  if (!_pipeline || error) {
    ERRPRINTF("[EE] RemoteStreamSession::_StartReceivePipeline: %s\n",
              error ? error->message : "gst_parse_launch returned null");
    if (error) g_error_free(error);
    emit statusMessage("HATA: GStreamer pipeline başlatılamadı.", true);
    return;
  }

  _appsink = gst_bin_get_by_name(GST_BIN(_pipeline), "sink");
  g_signal_connect(_appsink, "new-sample", G_CALLBACK(&RemoteStreamSession::_OnNewSample), this);
  _bus = gst_pipeline_get_bus(GST_PIPELINE(_pipeline));
  gst_element_set_state(_pipeline, GST_STATE_PLAYING);
}

void RemoteStreamSession::_StopReceivePipeline() {
  if (_pipeline) gst_element_set_state(_pipeline, GST_STATE_NULL);
  if (_appsink)  { gst_object_unref(_appsink); _appsink = nullptr; }
  if (_bus)      { gst_object_unref(_bus); _bus = nullptr; }
  if (_pipeline) { gst_object_unref(_pipeline); _pipeline = nullptr; }
}

GstFlowReturn RemoteStreamSession::_OnNewSample(GstElement *sink, gpointer userData) {
  RemoteStreamSession *self = static_cast<RemoteStreamSession *>(userData);

  GstSample *sample = nullptr;
  g_signal_emit_by_name(sink, "pull-sample", &sample);
  if (!sample) return GST_FLOW_ERROR;

  GstBuffer *buffer = gst_sample_get_buffer(sample);
  GstCaps   *caps   = gst_sample_get_caps(sample);
  GstVideoInfo vinfo;
  if (buffer && caps && gst_video_info_from_caps(&vinfo, caps)) {
    GstVideoFrame vframe;
    if (gst_video_frame_map(&vframe, &vinfo, buffer, GST_MAP_READ)) {
      const uchar *data = static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&vframe, 0));
      int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&vframe, 0);
      int w = GST_VIDEO_FRAME_WIDTH(&vframe);
      int h = GST_VIDEO_FRAME_HEIGHT(&vframe);
      // Deep copy now -- vframe/buffer are unmapped/unreffed right after this callback.
      QImage copy = QImage(data, w, h, stride, QImage::Format_RGB888).copy();
      gst_video_frame_unmap(&vframe);
      QMetaObject::invokeMethod(self, "_handleFrameReady", Qt::QueuedConnection,
                                 Q_ARG(QImage, copy));
    }
  }
  gst_sample_unref(sample);
  return GST_FLOW_OK;
}

void RemoteStreamSession::_handleFrameReady(const QImage &frame) {
  if (_tearingDown || frame.isNull()) return;

  bool wasStale = _lastFrameTimeMs == 0 || _stalled;
  _lastFrameTimeMs = QDateTime::currentMSecsSinceEpoch();
  if (wasStale) {
    _stalled = false;
    emit statusMessage("Görüntü alınıyor.", false);
  }
  emit frameReady(frame);
}

void RemoteStreamSession::_handleGstBusTimer() {
  if (_tearingDown || !_bus) return;
  while (gst_bus_have_pending(_bus)) {
    GstMessage *msg = gst_bus_pop(_bus);
    switch (GST_MESSAGE_TYPE(msg)) {
      case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        ERRPRINTF("[EE] RemoteStreamSession: GStreamer error: %s\n", err->message);
        emit statusMessage(QString("HATA: ") + err->message, true);
        g_error_free(err);
        g_free(debug);
        break;
      }
      case GST_MESSAGE_EOS:
        // Not expected on a live UDP source; defensive only.
        emit statusMessage("Akış sonlandı (EOS).", true);
        break;
      default:
        break;
    }
    gst_message_unref(msg);
  }
}

void RemoteStreamSession::_handleLivenessTimer() {
  if (_tearingDown) return;
  if (_lastFrameTimeMs > 0 &&
      QDateTime::currentMSecsSinceEpoch() - _lastFrameTimeMs > kLivenessTimeoutMs) {
    _stalled = true;
    emit statusMessage("Şu anda kameraya ulaşılamıyor!", true);
  }
}

void RemoteStreamSession::_StartStreamProcess() {
  _streamProcess = new QProcess(this);
  ConfigureSshpassProcess(_streamProcess, _camera.sshTarget, _camera.sshPassword,
                          { QString::fromUtf8(STREAMSERVER_PATH), "-d",
                            QString::fromStdString(_camera.device) },
                          /*allocatePty=*/true);
  connect(_streamProcess, &QProcess::errorOccurred, this, &RemoteStreamSession::_handleStreamProcessError);
  connect(_streamProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &RemoteStreamSession::_handleStreamProcessFinished);
  _streamProcess->start();
}

void RemoteStreamSession::_StopStreamProcess() {
  if (!_streamProcess) return;

  // Closing the SSH connection doesn't reliably kill the remote gst-launch-1.0,
  // so ask it to stop via a separate SSH command (stop_stream.sh) and wait for
  // confirmation before tearing down anything locally.
  QProcess killProcess;
  ConfigureSshpassProcess(&killProcess, _camera.sshTarget, _camera.sshPassword,
                          { QString::fromUtf8(STOPSTREAM_PATH),
                            QString::fromStdString(_camera.device) });
  killProcess.start();
  // Remote SIGTERM->SIGKILL escalation (~5s) + SSH connect timeout + slack.
  const int kRemoteKillTimeoutMs = 13000;
  if (!killProcess.waitForFinished(kRemoteKillTimeoutMs)) {
    ERRPRINTF("[WW] RemoteStreamSession::_StopStreamProcess: remote kill command did not "
              "finish in time; proceeding with local teardown anyway.\n");
    killProcess.kill();
    killProcess.waitForFinished(1000);
  } else if (killProcess.exitStatus() != QProcess::NormalExit || killProcess.exitCode() != 0) {
    // A nonzero exit here means the SSH connection itself failed (exit 255),
    // not the remote script -- log the diagnostics for troubleshooting.
    QString killErr = QString::fromUtf8(killProcess.readAllStandardError()).trimmed();
    QString killOut = QString::fromUtf8(killProcess.readAllStandardOutput()).trimmed();
    ERRPRINTF("[WW] RemoteStreamSession::_StopStreamProcess: remote kill command reported "
              "failure (exit %d); the remote process may still be running.\n"
              "  stderr: %s\n  stdout: %s\n",
              killProcess.exitCode(),
              killErr.isEmpty() ? "(empty)" : killErr.toUtf8().constData(),
              killOut.isEmpty() ? "(empty)" : killOut.toUtf8().constData());
  }

  // The remote exit above usually ends this SSH session too; clean up whatever's left.
  if (_streamProcess->state() != QProcess::NotRunning) {
    _streamProcess->terminate();
    if (!_streamProcess->waitForFinished(2000)) {
      _streamProcess->kill();
      _streamProcess->waitForFinished(1000);
    }
  }
  _streamProcess->deleteLater();
  _streamProcess = nullptr;
}

void RemoteStreamSession::_handleStreamProcessError(QProcess::ProcessError error) {
  if (_tearingDown) return;
  if (error == QProcess::FailedToStart) {
    emit statusMessage("HATA: sshpass çalıştırılamadı (sistemde kurulu mu? "
                        "'sudo apt install sshpass').", true);
  }
}

void RemoteStreamSession::_handleStreamProcessFinished(int, QProcess::ExitStatus) {
  if (_tearingDown) return;

  // Read both streams to capture why the remote process exited (e.g. gst-launch
  // failing to negotiate caps against an unsupported camera).
  QString out = QString::fromUtf8(_streamProcess->readAllStandardOutput()).trimmed();
  QString err = QString::fromUtf8(_streamProcess->readAllStandardError()).trimmed();
  QString detail = out;
  if (!err.isEmpty()) detail += (detail.isEmpty() ? QString() : QString("\n")) + err;

  QString msg = "Kamera akışı sonlandı (SSH bağlantısı koptu).";
  if (!detail.isEmpty()) {
    QStringList lines = detail.split('\n', Qt::SkipEmptyParts);
    const int kKeepLines = 6;
    if (lines.size() > kKeepLines) lines = lines.mid(lines.size() - kKeepLines);
    msg += "\n" + lines.join('\n');
  }
  emit statusMessage(msg, true);
}
