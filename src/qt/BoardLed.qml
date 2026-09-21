import QtQuick
import QtQuick.Controls

Control {
    id: control
    required property string resource
    required property bool available
    required property bool active

    implicitWidth: 40
    implicitHeight: 42
    padding: 0
    hoverEnabled: true
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%1: %2").arg(resource).arg(!available
        ? qsTr("not connected") : active ? qsTr("on") : qsTr("off"))
    ToolTip.visible: hovered
    ToolTip.text: !available ? qsTr("%1 is not connected").arg(resource)
        : qsTr("%1 · %2").arg(resource).arg(active ? qsTr("On") : qsTr("Off"))

    contentItem: Item {
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 2
            width: 22
            height: 22
            radius: 11
            color: control.available && control.active ? "#264c36" : "#15231c"
            Rectangle {
                anchors.centerIn: parent
                width: 13
                height: 13
                radius: 6.5
                color: !control.available ? "#34413b"
                    : control.active ? "#92ef79" : "#3c6144"
                border.color: control.available && control.active ? "#c8ffb9" : "#496351"
            }
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 29
            text: control.resource
            color: control.available ? "#cad8df" : "#718492"
            font.pixelSize: 9
        }
    }
}
