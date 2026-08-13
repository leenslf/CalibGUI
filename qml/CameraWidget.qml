// Ported from RobotGUI/qml/CameraWidget.qml — keep in sync manually if that file changes.
import QtQuick 2.4
import QtQuick.Window 2.4
import QtQuick.Controls 2.4

import org.freedesktop.gstreamer.GLVideoItem 1.0

Rectangle {
    // Apparently, QQuickWidget background cannot be transparent for some reason
    color: "black"      
    
    GstGLVideoItem {
        id: video
        objectName: "videoItem"
        anchors.centerIn: parent
        width: parent.width
        height: parent.height
    }
}
