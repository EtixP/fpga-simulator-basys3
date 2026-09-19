import QtQuick
import QtQuick.Controls
import VirtualBasys.Board

ApplicationWindow {
    id: window
    required property BoardAdapter board
    width: 960
    height: 640
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: qsTr("VirtualBasys")

    Label {
        anchors.centerIn: parent
        text: window.board.connected ? qsTr("Board ready") : qsTr("No board connected")
    }
}
