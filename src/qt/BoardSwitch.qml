import QtQuick
import QtQuick.Controls

Button {
    id: control
    required property string resource
    required property bool available
    required property bool active
    signal toggleRequested(bool value)

    implicitWidth: 57
    implicitHeight: 115
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

    // A slide switch on the rendered board. The board image shows it off; when
    // on, its sprite (a render of this switch on) covers it. Unbound switches
    // are veiled. Geometry is in board units; the sprite overhangs the housing.
    property var spriteRect: [0, 0, width, height]
    property url spriteSource
    background: Item {
        Image {
            objectName: "sprite"
            x: control.spriteRect[0]
            y: control.spriteRect[1]
            width: control.spriteRect[2]
            height: control.spriteRect[3]
            source: control.spriteSource
            visible: control.active
            smooth: true
            mipmap: true
        }
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: "#0a1a33"
            opacity: control.available ? 0 : 0.6
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: 6
            color: "transparent"
            border.width: 3
            border.color: "#9fe8dc"
            opacity: control.hovered && control.available && !control.visualFocus ? 0.75 : 0
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            radius: 8
            color: "transparent"
            border.width: 4
            border.color: "#7fe0d0"
            visible: control.visualFocus
        }
    }
    contentItem: Item {}
}
