#ifndef CAMLISTWIDGET_H
#define CAMLISTWIDGET_H

#include <string>
#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <gst/gst.h>

#include <RHexAPI.h>

class CameraWidget;

class CamListWidget : public QFrame {
  Q_OBJECT

public:
  CamListWidget(CameraWidget *pview, CameraWidget *sview,
                bool qtqml = false, QWidget *parent = nullptr);
  virtual ~CamListWidget() {};

  void setRHexAPI(RHexAPI *rhex);

  void focusOutEvent( QFocusEvent* event );
  void showEvent( QShowEvent* event );

  void setState( int primary, int secondary );
  void getState( int *primary, int *secondary ) {
    *primary = _primary;
    *secondary = _secondary;
  }

  std::string getCamName(int index ) {
    if (index < 0 || index > _numcams) return "";
    else return _camNames[index];
  }
  
  bool swap();
  bool nextPrimary();
  
signals:
  void hide();
  void updated(int pprev, int sprev);
    
public slots:
  void primary( int index );
  void secondary( int index );

private:
  RHexAPI * _rhex;
  bool      _qtqml = false;
  static constexpr int _maxCameras=4;
  int   _numcams = 0;
  
  // UI components
  QPushButton *_primaryBtns[_maxCameras+1];
  QPushButton *_secondaryBtns[_maxCameras+1];
  QPushButton *_newButton( QString name, bool primary, int index );
  // Builds one slot's row: a label followed by a segmented control of
  // (_maxCameras+1) toggle buttons ("Kapalı" + one per camera).
  QHBoxLayout *_buildSlotRow( const QString &label, bool primary );

  // Periodic update, used to call API UpdateCameraStates()
  void _update();
  void _play( int index ); // Starts camera playback
  void _stop( int index ); // Stops camera playback
  
  // Buttons and camera selection
  int _primary = -1, _secondary = -1;
  CameraWidget *_pview = nullptr, *_sview = nullptr;
  std::string _camNames[_maxCameras];
  // Updates button disable/enable states
  void _updateDisabled(); 

};

#endif
