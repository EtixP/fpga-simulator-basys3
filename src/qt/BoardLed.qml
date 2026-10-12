import QtQuick
import QtQuick.Controls

Control {
    id: control
    required property string resource
    required property bool available
    required property bool active

    implicitWidth: 24
    implicitHeight: 28
    padding: 0
    hoverEnabled: true
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%1: %2").arg(resource).arg(!available
        ? qsTr("not connected") : active ? qsTr("on") : qsTr("off"))
    BoardToolTip {
        target: control
        showing: control.hovered
        text: !control.available ? qsTr("%1 is not connected").arg(control.resource)
            : qsTr("%1 · %2").arg(control.resource).arg(control.active ? qsTr("On") : qsTr("Off"))
    }

    // An LED on the rendered board: when lit, its sprite (a render of this LED
    // lit, with its glow) covers the board image. Unbound LEDs are veiled.
    readonly property bool lit: available && active
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
            visible: control.lit
            smooth: true
            mipmap: true
        }
        Rectangle {
            anchors.fill: parent
            radius: 3
            color: "#0a1a33"
            opacity: control.available ? 0 : 0.6
        }
    }
    contentItem: Item {}
}
