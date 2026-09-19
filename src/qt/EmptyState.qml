import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: state
    required property string heading
    required property string detail
    property string badge: ""
    property bool compact: false
    property string headingObjectName: ""
    property string detailObjectName: ""
    padding: compact ? 16 : 28
    contentItem: Item {
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width, state.compact ? 580 : 440)
            spacing: state.compact ? 8 : 16
            Rectangle {
                visible: state.badge.length > 0
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 52
                implicitHeight: 52
                radius: 12
                color: state.palette.alternateBase
                border.color: state.palette.mid
                Label {
                    anchors.centerIn: parent
                    text: state.badge
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                    color: state.palette.placeholderText
                }
            }
            Label {
                objectName: state.headingObjectName
                Layout.fillWidth: true
                text: state.heading
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: state.compact ? 14 : 22
                font.weight: Font.DemiBold
            }
            Label {
                objectName: state.detailObjectName
                Layout.fillWidth: true
                text: state.detail
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: state.palette.placeholderText
                lineHeight: 1.3
            }
        }
    }
}
