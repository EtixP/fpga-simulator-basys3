pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VirtualBasys.Board

// Presentation of the adapter's structured-log view. BoardModel produces the
// cycle-stamped events; the adapter drains them in emission order and the C++
// proxy filters them. Delegates only bind roles.
Control {
    id: logView
    required property BoardAdapter board
    readonly property EventLogModel events: board.eventLog
    readonly property EventFilterModel filtered: board.eventLogView
    readonly property string monoFamily: "Menlo"
    readonly property var tagText: ["DISP", "LED", "UART", "IN", "··", "··"]
    readonly property var tagColors: ["#8fb8ff", "#8be28b", "#62d0c4", "#e9b872", "#8d9fb1", "#8d9fb1"]
    padding: 0

    contentItem: ColumnLayout {
        spacing: 0

        Label {
            objectName: "logTrimmedNotice"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 2
            Layout.bottomMargin: 2
            visible: logView.events.trimmedEvents > 0
            text: qsTr("%1 earlier events discarded. The view keeps the latest %2.")
                .arg(logView.events.trimmedEvents).arg(logView.events.maximumEvents)
            color: logView.palette.placeholderText
            font.pixelSize: 11
            font.italic: true
            elide: Text.ElideRight
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: lines
                objectName: "logLines"
                property bool following: true
                anchors.fill: parent
                clip: true
                model: logView.filtered
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                Accessible.name: qsTr("Board event log")
                // No top/bottom margins: positionViewAtEnd() ignores them.

                function followLatest() {
                    if (following)
                        positionViewAtEnd()
                }
                function showLatest() {
                    following = true
                    positionViewAtEnd()
                }
                function scrollBy(delta) {
                    const last = Math.max(originY, originY + contentHeight - height)
                    contentY = Math.max(originY, Math.min(last, contentY + delta))
                    following = contentY >= last - 0.5
                }

                onCountChanged: {
                    if (count === 0)
                        following = true
                    Qt.callLater(lines.followLatest)
                }
                onOriginYChanged: Qt.callLater(lines.followLatest)
                onContentHeightChanged: Qt.callLater(lines.followLatest)
                onHeightChanged: Qt.callLater(lines.followLatest)
                onMovementStarted: following = false
                onMovementEnded: following = atYEnd

                Connections {
                    target: logView.events
                    // Emitted for every insert, including those that trim.
                    function onCountChanged() { Qt.callLater(lines.followLatest) }
                }

                Keys.onPressed: (event) => {
                    switch (event.key) {
                    case Qt.Key_Up: lines.scrollBy(-20); break
                    case Qt.Key_Down: lines.scrollBy(20); break
                    case Qt.Key_PageUp: lines.scrollBy(-lines.height * 0.9); break
                    case Qt.Key_PageDown: lines.scrollBy(lines.height * 0.9); break
                    case Qt.Key_Home: lines.scrollBy(-lines.contentHeight); break
                    case Qt.Key_End: lines.showLatest(); break
                    default: return
                    }
                    event.accepted = true
                }

                ScrollBar.vertical: ScrollBar {
                    objectName: "logScrollBar"
                    policy: ScrollBar.AsNeeded
                    onPressedChanged: lines.following = !pressed && lines.atYEnd
                }

                delegate: Item {
                    id: row
                    required property int index
                    required property int kind
                    required property string cycle
                    required property string time
                    required property string text
                    readonly property bool notice: kind === EventLogModel.Notice
                    width: lines.width
                    height: 20
                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 12
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Label {
                            Layout.minimumWidth: timeMetrics.advanceWidth
                            horizontalAlignment: Text.AlignRight
                            text: row.time
                            color: logView.palette.placeholderText
                            font.family: logView.monoFamily
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.minimumWidth: cycleMetrics.advanceWidth
                            text: qsTr("cycle %1").arg(row.cycle)
                            color: logView.palette.placeholderText
                            font.family: logView.monoFamily
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.preferredWidth: 34
                            text: logView.tagText[row.kind]
                            color: logView.tagColors[row.kind]
                            font.family: logView.monoFamily
                            font.pixelSize: 12
                            font.bold: !row.notice
                        }
                        Label {
                            objectName: "logLineText" + row.index
                            Layout.fillWidth: true
                            text: row.text
                            color: row.notice ? logView.palette.placeholderText : logView.palette.text
                            font.family: logView.monoFamily
                            font.pixelSize: 12
                            font.italic: row.notice
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // Keyboard focus outline; a ListView child would scroll with rows.
            Rectangle {
                objectName: "logFocusOutline"
                anchors.fill: lines
                visible: lines.activeFocus
                color: "transparent"
                border.color: logView.palette.highlight
                border.width: 1
            }

            ColumnLayout {
                objectName: "logEmptyState"
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 640)
                visible: lines.count === 0
                spacing: 4
                Label {
                    objectName: "logEmptyTitle"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    font.weight: Font.DemiBold
                    text: !logView.events.recording && logView.events.count === 0
                        ? qsTr("Recording is off")
                        : logView.events.count > 0 ? qsTr("No events match the filter")
                        : qsTr("No events yet")
                }
                Label {
                    objectName: "logEmptyDetail"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: logView.palette.placeholderText
                    text: !logView.events.recording && logView.events.count === 0
                        ? qsTr("Record to see cycle-stamped LED, display, UART, switch and button events as the simulation advances.")
                        : logView.events.count > 0 ? qsTr("Change the categories or the text filter to show more events.")
                        : qsTr("Events appear when outputs change or inputs are used. Run or Step to advance virtual time.")
                }
            }

            Button {
                objectName: "logLatestButton"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 22
                anchors.bottomMargin: 6
                visible: !lines.following && lines.count > 0
                text: qsTr("Latest ↓")
                font.pixelSize: 11
                onClicked: lines.showLatest()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: logView.palette.mid
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 8
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            spacing: 4
            Button {
                objectName: "logRecordButton"
                implicitHeight: 30
                text: logView.events.recording ? qsTr("Stop") : qsTr("Record")
                highlighted: !logView.events.recording
                onClicked: logView.board.setLogRecording(!logView.events.recording)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("While recording, the log view collects the board's structured log and keeps its memory bounded.")
            }
            Repeater {
                model: [
                    {name: "logShowDisplay", label: qsTr("Display"), property: "showDisplay"},
                    {name: "logShowLeds", label: qsTr("LEDs"), property: "showLeds"},
                    {name: "logShowUart", label: qsTr("UART"), property: "showUart"},
                    {name: "logShowInputs", label: qsTr("Inputs"), property: "showInputs"}
                ]
                // Check boxes show plainly which categories are hidden.
                CheckBox {
                    required property var modelData
                    objectName: modelData.name
                    text: modelData.label
                    checked: logView.filtered[modelData.property]
                    font.pixelSize: 12
                    onToggled: logView.filtered[modelData.property] = checked
                }
            }
            TextField {
                objectName: "logFilter"
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                implicitHeight: 30
                placeholderText: qsTr("Filter events")
                font.pixelSize: 12
                Accessible.name: qsTr("Filter events by text or cycle")
                onTextChanged: logView.filtered.filterText = text
            }
            Label {
                objectName: "logStatus"
                Layout.leftMargin: 6
                text: logView.events.recording || logView.events.count > 0
                    ? qsTr("%1 of %2 shown").arg(lines.count).arg(logView.events.count) : ""
                color: logView.palette.placeholderText
                font.pixelSize: 11
            }
            ToolButton {
                objectName: "logClearButton"
                text: qsTr("Clear")
                enabled: logView.events.count > 0
                focusPolicy: Qt.StrongFocus
                onClicked: logView.board.clearEventLog()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Clear the view. Recording continues.")
            }
        }
    }

    TextMetrics { id: timeMetrics; font.family: logView.monoFamily; font.pixelSize: 12; text: "000.000000000 s" }
    TextMetrics { id: cycleMetrics; font.family: logView.monoFamily; font.pixelSize: 12; text: "cycle 00000000000" }
}
