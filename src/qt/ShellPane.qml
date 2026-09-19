import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: pane
    required property string title
    property color paneColor: palette.base
    default property alias body: bodyItem.data
    padding: 0
    background: Rectangle { color: pane.paneColor }
    contentItem: ColumnLayout {
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: pane.palette.alternateBase
            Label {
                anchors.fill: parent
                anchors.leftMargin: 16
                verticalAlignment: Text.AlignVCenter
                text: pane.title
                color: pane.palette.placeholderText
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1.2
            }
        }
        Item {
            id: bodyItem
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
        }
    }
}
