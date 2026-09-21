import QtQuick
import QtQuick.Controls

Button {
    id: control
    required property string resource
    required property bool available
    required property bool active
    signal toggleRequested(bool value)

    implicitWidth: 40
    implicitHeight: 84
    enabled: available
    checkable: false
    autoRepeat: false
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: 0
    text: resource
    down: active

    // The model owns the value; an interaction only requests its opposite.
    onClicked: toggleRequested(!active)
    Accessible.role: Accessible.CheckBox
    Accessible.name: resource
    Accessible.checkable: true
    Accessible.checked: active
    Accessible.description: !available ? qsTr("Not connected")
        : active ? qsTr("On. Activate to turn off.") : qsTr("Off. Activate to turn on.")
    Accessible.onToggleAction: {
        if (control.enabled)
            control.toggleRequested(!control.active)
    }
    ToolTip.visible: hovered
    ToolTip.text: !available ? qsTr("%1 is not connected").arg(resource)
        : qsTr("%1 · %2").arg(resource).arg(active ? qsTr("On") : qsTr("Off"))

    background: Rectangle {
        radius: 5
        color: control.hovered ? "#203744" : "#172832"
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? "#6bd5c6" : "#304653"
    }
    contentItem: Item {
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
            width: 22
            height: 45
            radius: 4
            color: "#0b151c"
            border.color: control.available ? "#45606c" : "#2a3942"
            Rectangle {
                x: 3
                y: control.active ? 3 : 24
                width: 16
                height: 18
                radius: 2
                color: !control.available ? "#34434d"
                    : control.active ? "#58c9b9" : "#8397a4"
                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 2
                    radius: 1
                    color: "#17333b"
                }
            }
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 62
            text: control.resource
            color: control.available ? "#dce7f0" : "#718492"
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }
}
