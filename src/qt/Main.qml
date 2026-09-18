import QtQuick
import QtQuick.Controls

ApplicationWindow {
    width: 960
    height: 640
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: qsTr("VirtualBasys")

    Label {
        anchors.centerIn: parent
        text: qsTr("No board connected")
    }
}
