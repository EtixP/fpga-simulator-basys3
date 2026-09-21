pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Control {
    id: control
    required property int digit
    required property int segments
    required property bool decimalPoint
    required property string character
    property bool available: true

    objectName: "digit" + digit
    implicitWidth: 46
    implicitHeight: 78
    padding: 0
    hoverEnabled: true
    Accessible.role: Accessible.StaticText
    Accessible.name: !available ? qsTr("AN%1: not connected").arg(digit)
        : qsTr("AN%1: %2%3").arg(digit)
            .arg(character === " " ? qsTr("blank") : character)
            .arg(decimalPoint ? qsTr(", decimal point on") : "")
    ToolTip.visible: hovered
    ToolTip.text: !available ? qsTr("AN%1 is not connected").arg(digit)
        : Accessible.name

    contentItem: Item {
        id: face
        readonly property real xScale: width / 46
        readonly property real yScale: height / 78
        Repeater {
            model: 7
            delegate: Rectangle {
                required property int index
                objectName: "segment" + index
                readonly property bool lit: control.available && (control.segments & (1 << index)) !== 0
                readonly property bool horizontal: index === 0 || index === 3 || index === 6
                // A/B/C/D/E/F/G: top, upper right, lower right, bottom,
                // lower left, upper left, middle. The adapter already uses lit bits.
                x: (horizontal ? 8 : (index === 1 || index === 2 ? 32 : 4)) * face.xScale
                y: (index === 0 ? 3 : index === 3 ? 59 : index === 6 ? 31
                    : index === 1 || index === 5 ? 7 : 35) * face.yScale
                width: (horizontal ? 24 : 4) * face.xScale
                height: (horizontal ? 4 : 23) * face.yScale
                radius: Math.min(face.xScale, face.yScale)
                color: lit ? "#ff746c" : control.available ? "#462c32" : "#302c32"
            }
        }
        Rectangle {
            objectName: "decimalPoint"
            readonly property bool lit: control.available && control.decimalPoint
            x: 39 * face.xScale
            y: 59 * face.yScale
            width: 5 * face.xScale
            height: 5 * face.yScale
            radius: width / 2
            color: lit ? "#ff746c" : control.available ? "#462c32" : "#302c32"
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 68 * face.yScale
            text: "AN" + control.digit
            color: control.available ? "#a9bac5" : "#718492"
            font.pixelSize: 9 * Math.min(face.xScale, face.yScale)
        }
    }
}
