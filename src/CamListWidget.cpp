#include <math.h>
#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QVariant>

#include "config.h"
#include "Theme.h"
#include "CameraWidget.h"
#include "CamListWidget.h"

#include <gst/gst.h>

#undef DEBUG_LOCAL
#include "debuglog.h"

// Segmented-control styling for the per-slot camera-source rows, built from the
// shared palette in Theme.h. Options sit in a bordered frame with no per-button
// border, so the group reads as one control rather than five separate buttons;
// the selected option is accent-filled via the ":checked" pseudo-state, which
// CamListWidget's existing primary()/secondary() selection logic already drives
// through setChecked() — no new selection-tracking code needed here.
static const QString kSegmentFrameStyle =
  "QFrame {"
  "  background-color: " + kColorSurface + "; border: 1px solid " + kColorBorder + ";"
  "  border-radius: 6px;"
  "}";

static const QString kSegmentButtonStyle =
  "QPushButton {"
  "  background-color: transparent; color: " + kColorTextMuted + ";"
  "  border: none; border-radius: 4px; padding: 5px 12px; font-size: 10.5pt;"
  "}"
  "QPushButton:hover:!checked { background-color: " + kColorHover + "; }"
  "QPushButton:checked { background-color: " + kColorAccent + "; color: white; font-weight: 600; }"
  "QPushButton:disabled { color: " + kColorAccentMuted + "; }";

static const QString kSegmentLabelStyle =
  "color: " + kColorText + "; font-size: 10.5pt; font-weight: 600;";

void CamListWidget::setState( int p, int s ) {
  primary( p );
  secondary( s );
}

void CamListWidget::_updateDisabled( ) {

  for (int i = 0; i < _numcams+1; i++) {
    _primaryBtns[i]->setDisabled(false);
    _secondaryBtns[i]->setDisabled(false);
  }
  
  if (_primary >= 0 ) {
    // Prevent simultaneous operation of back/left/right
    for (int i = 1; i < _numcams; i++) {
      if (_primary == 0 || i == _primary) 
        _secondaryBtns[i+1]->setDisabled(false);
      else
        _secondaryBtns[i+1]->setDisabled(true);
    }
  }
  
  if (_secondary >= 0 ) {
    for (int i = 1; i < _numcams; i++) {
      if (_secondary == 0 || i == _secondary) 
        _primaryBtns[i+1]->setDisabled(false);
      else
        _primaryBtns[i+1]->setDisabled(true);
    }
  }

    // Prevent toggling for the selected
  _primaryBtns[_primary+1]->setDisabled(true);
  _secondaryBtns[_secondary+1]->setDisabled(true);

  // Prevent turning off primary by swapping
  if (_secondary < 0) _secondaryBtns[_primary+1]->setDisabled(true);
}

bool CamListWidget::nextPrimary( ) {
  DBGPRINT("CamListWidget::nextPrimary\n");
  if (!_rhex || !_rhex->IsConnected()) return false;

  if (_numcams == 1) return false;
  
  int newp = _primary;
  do {
    newp = (newp+1) % _numcams;
  } while (!_primaryBtns[newp+1]->isEnabled() || newp == _primary);
  DBGPRINT("CamListWidget::nextPrimary: newp:%d, primary:%d\n", newp, _primary);

  if (newp == _primary) {
    // No other cameras were enabled for some reason, skip
    return false;
  }
  primary(newp+1);
  return true;
}

bool CamListWidget::swap( ) {
  DBGPRINT("CamListWidget::swap\n");
  if (!_rhex || !_rhex->IsConnected()) return false;

  // Do not swap if the secondary camera is turned off
  if (_secondary < 0) return false;

  if (_primary >= 0) {
    if (_qtqml)
      _sview->SetQmlGlSink( _rhex->GetCameraStreamQmlGlSink(_primary));
    else
      _rhex->SetCameraStreamWindow(_primary, _sview->GetWindow());
  }
    
  if (_secondary >= 0) {
    if (_qtqml)
      _pview->SetQmlGlSink( _rhex->GetCameraStreamQmlGlSink(_secondary));
    else
      _rhex->SetCameraStreamWindow(_secondary, _pview->GetWindow());
  }
    
  int pprev = _primary, sprev = _secondary;

  _primary = sprev;
  _secondary = pprev;
  _primaryBtns[pprev+1]->setChecked(false);
  _primaryBtns[_primary+1]->setChecked(true);
  _secondaryBtns[sprev+1]->setChecked(false);
  _secondaryBtns[_secondary+1]->setChecked(true);

  _pview->setState( _primary >= 0 );
  _sview->setState( _secondary >= 0 );
  
  _updateDisabled();
  emit updated(pprev, sprev);
  return true;
}

void CamListWidget::primary( int index ) {
  DBGPRINT("CamListWidget: Primary: %d\n", index);
  if (!_rhex || !_rhex->IsConnected() || _primary == index-1) {
    // Toggle back
    _primaryBtns[index]->setChecked(!_primaryBtns[index]->isChecked());
    return;
  }

  if (index != 0 && _secondary == index-1) {
    if (!swap()) _primaryBtns[index]->setChecked(false);
  } else {
    int pprev = _primary, sprev = _secondary;

    try {
      if (_primary >= 0) _stop( _primary );

       // Maintain consistent state in case of exceptions
      _primary = -1;
      for (int i = 0; i < _numcams+1; i++)
        _primaryBtns[i]->setChecked( false );
      _primaryBtns[0]->setChecked(true);
      _pview->setState( false );

      if (index > 0) {
        if (_qtqml)
          _pview->SetQmlGlSink( _rhex->GetCameraStreamQmlGlSink(index-1));
        else
            _rhex->SetCameraStreamWindow(index-1, _pview->GetWindow());
        _play(index-1);
      } 

      _primaryBtns[0]->setChecked(false);
      _pview->setState( index > 0 );
      _primary = index-1;
      _primaryBtns[index]->setChecked(true);
    } catch (const std::runtime_error &e) {
      ERRPRINTF("[EE] CamListWidget::primary: %s\n", e.what());
    }
    _updateDisabled();
    emit updated(pprev, sprev);
  }
}

void CamListWidget::secondary( int index ) {
  DBGPRINT("CamListWidget: Secondary: %d\n", index);
  if (!_rhex || !_rhex->IsConnected() || _secondary == index-1) {
    _secondaryBtns[index]->setChecked(!_secondaryBtns[index]->isChecked());
    return;
  }

  if (index != 0 && _primary == index-1) {
    if (!swap()) _secondaryBtns[index]->setChecked(false);
  } else {
    int pprev = _primary, sprev = _secondary;

    try {
      if (_secondary >= 0) _stop(_secondary);

       // Maintain consistent state in case of exceptions
      _secondary = -1;
      for (int i = 0; i < _numcams+1; i++)
        _secondaryBtns[i]->setChecked( false );
      _secondaryBtns[0]->setChecked(true);
      _sview->setState( false );

      if (index > 0) {
        if (_qtqml)
          _sview->SetQmlGlSink( _rhex->GetCameraStreamQmlGlSink(index-1));
        else
          _rhex->SetCameraStreamWindow( index-1, _sview->GetWindow() );
        _play( index-1 );
      }

      _secondaryBtns[0]->setChecked(false);
      _sview->setState( index > 0 );
      _secondary = index-1;
      _secondaryBtns[index]->setChecked(true);
    } catch (const std::runtime_error &e) {
      ERRPRINTF("[EE] CamListWidget::secondary: %s\n", e.what());
    }

    _updateDisabled();
    emit updated(pprev, sprev);
  }
}


QPushButton *CamListWidget::_newButton( QString name, bool primary, int index ) {
  QPushButton *btn = new QPushButton(name);
  SETCLASS(btn, "clbtn");
  btn->setStyleSheet(kSegmentButtonStyle);
  btn->setFocusPolicy(Qt::NoFocus);
  btn->setCheckable(true);
  btn->setChecked(false);
  if (primary)
    connect(btn, &QPushButton::released, this,
            [this,index]() {CamListWidget::primary(index);});
  else
    connect(btn, &QPushButton::released, this,
            [this,index]() {CamListWidget::secondary(index);});
  return btn;
}

// Builds one slot's row: a label followed by a segmented control of
// (_maxCameras+1) toggle buttons ("Kapalı" + one per camera), replacing what used
// to be a column of _maxCameras+1 stacked full-height buttons.
QHBoxLayout *CamListWidget::_buildSlotRow( const QString &label, bool primary ) {
  QHBoxLayout *row = new QHBoxLayout;

  QLabel *lbl = new QLabel(label);
  lbl->setStyleSheet(kSegmentLabelStyle);
  lbl->setMinimumWidth(50);
  row->addWidget(lbl);

  QFrame *segment = new QFrame;
  segment->setStyleSheet(kSegmentFrameStyle);
  QHBoxLayout *segLayout = new QHBoxLayout(segment);
  segLayout->setSpacing(2);
  segLayout->setContentsMargins(3, 3, 3, 3);

  QPushButton *offBtn = _newButton("Kapalı", primary, 0);
  offBtn->setChecked(true);
  segLayout->addWidget(offBtn);
  if (primary) _primaryBtns[0] = offBtn; else _secondaryBtns[0] = offBtn;

  for (int i = 0; i < _maxCameras; i++) {
    QPushButton *camBtn = _newButton("kam", primary, i+1);
    segLayout->addWidget(camBtn);
    if (primary) _primaryBtns[i+1] = camBtn; else _secondaryBtns[i+1] = camBtn;
  }

  row->addWidget(segment);
  row->addStretch();
  return row;
}

CamListWidget::CamListWidget(CameraWidget *pview, CameraWidget *sview,
                             bool qtqml, QWidget *parent) : QFrame(parent) {
  _qtqml = qtqml;
  _pview = pview;
  _sview = sview;

  SETCLASS(this,  "clwidget");

  _rhex = nullptr;
  QVBoxLayout *outer = new QVBoxLayout;
  setLayout(outer);
  outer->setSpacing(6);
  outer->setContentsMargins(0, 4, 0, 4);

  outer->addLayout(_buildSlotRow("Birinci", true));
  outer->addLayout(_buildSlotRow("İkinci", false));

  QTimer *_update_timer = new QTimer(this);
  connect(_update_timer,&QTimer::timeout,this, &CamListWidget::_update );
  _update_timer->start( 300 );
}

void CamListWidget::setRHexAPI( RHexAPI *rhex ) {

  if (!rhex) {
    if (_primary >=0) _stop(_primary);
    if (_secondary >=0) _stop(_secondary);
    _primary = -1;
    _secondary = -1;
  }

  _rhex = rhex;

  if (!_rhex) return;
  
  _numcams = _rhex->GetNumCameras();
  DBGPRINT("CamListWidget::setRHexAPI: %d cams\n", _numcams);
  
  if (_numcams > _maxCameras) _numcams = _maxCameras;
  
  // Check available cameras and configure tabs
  int i;
  for (i = 0; i < _numcams; i++) {
    _camNames[i] = _rhex->GetCameraName(i);
    _primaryBtns[i+1]->setText(_camNames[i].c_str());
    _primaryBtns[i+1]->setVisible(true);
    _primaryBtns[i+1]->setChecked(false);
    _secondaryBtns[i+1]->setText(_camNames[i].c_str());
    _secondaryBtns[i+1]->setVisible(true);
    _secondaryBtns[i+1]->setChecked(false);
  }
  for (; i < _maxCameras; i++) {
    _primaryBtns[i+1]->setVisible(false);
    _secondaryBtns[i+1]->setVisible(false);
  }

  _primary = -2;  // Forces full update?
  _secondary = -2;  // Forces full update?
  // First time, so no cameras are turned on
  primary(1);
  secondary(0);

  // _recording = false;
}

void CamListWidget::focusOutEvent( QFocusEvent* event ) {
  QWidget::focusOutEvent( event );
  emit hide();
}

void CamListWidget::showEvent( QShowEvent* event ) {
  QWidget::showEvent( event );
  setFocusPolicy(Qt::ClickFocus);
  setFocus();
} 

void CamListWidget::_play( int camno ) {
  _rhex->PlayCameraStream( camno );
}

void CamListWidget::_stop( int camno ) {
  _rhex->PauseCameraStream( camno );
}


void CamListWidget::_update() {
  if (!_rhex || !_rhex->IsConnected()) return;

  if (_primary >= 0) {
    _rhex->UpdateCameraState( _primary );
    CameraStatus st = _rhex->GetCameraStatus( _primary );
    switch (st) {
    case CameraStatus::HWSTALL:
      _pview->setStall( true, false ); break;
    case CameraStatus::NETSTALL:
      _pview->setStall( false, true ); break;
    default:
      _pview->setStall( false, false ); 
    }
  }
  if (_secondary >= 0) {
    _rhex->UpdateCameraState( _secondary );
    CameraStatus st = _rhex->GetCameraStatus( _secondary );
    switch (st) {
    case CameraStatus::HWSTALL:
      _sview->setStall( true, false ); break;
    case CameraStatus::NETSTALL:
      _sview->setStall( false, true ); break;
    default:
      _sview->setStall( false, false ); 
    }
  }
}


