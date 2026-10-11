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
    implicitWidth: 60
    implicitHeight: 90
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

    // One digit of the rendered board's display. Each lit segment shows its
    // sprite (a render of that segment lit, with its glow) over the board image.
    // cell is this digit's entry in BoardLayout.digits: segment rectangles in
    // board units, relative to the digit, in the order A-G then the point.
    property var cell: null
    readonly property var segmentNames: ["a", "b", "c", "d", "e", "f", "g", "dp"]
    function segmentRect(index) {
        return cell ? cell.segments[index].rect : [0, 0, 0, 0]
    }
    contentItem: Item {
        Repeater {
            model: 7
            delegate: Image {
                required property int index
                objectName: "segment" + index
                // A/B/C/D/E/F/G: top, upper right, lower right, bottom, lower
                // left, upper left, middle. The adapter already uses lit bits.
                readonly property bool lit: control.available && (control.segments & (1 << index)) !== 0
                x: control.segmentRect(index)[0]
                y: control.segmentRect(index)[1]
                width: control.segmentRect(index)[2]
                height: control.segmentRect(index)[3]
                source: "board/sprites/segment-%1-%2.png".arg(control.digit).arg(control.segmentNames[index])
                opacity: lit ? 1 : 0
                smooth: true
                mipmap: true
            }
        }
        Image {
            objectName: "decimalPoint"
            readonly property bool lit: control.available && control.decimalPoint
            x: control.segmentRect(7)[0]
            y: control.segmentRect(7)[1]
            width: control.segmentRect(7)[2]
            height: control.segmentRect(7)[3]
            source: "board/sprites/segment-%1-dp.png".arg(control.digit)
            opacity: lit ? 1 : 0
            smooth: true
            mipmap: true
        }
    }
}
