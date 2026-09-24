pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VirtualBasys.Board

// Presentation of the adapter's UART scrollback. Decoding, timestamps, line
// grouping and bounds stay in C++; input goes through BoardAdapter.
Control {
    id: terminal
    required property BoardAdapter board
    readonly property UartConsoleModel uart: board !== null ? board.uart : null
    readonly property bool canSend: uart !== null && uart.rxBound
    property bool hexView: false
    readonly property string monoFamily: "Menlo"
    readonly property color txColor: "#62d0c4"
    readonly property color rxColor: "#e9b872"
    padding: 0

    // Appended by the terminal, like a serial monitor; the adapter sends
    // exactly the resulting string.
    readonly property var lineEndings: ["", "\n", "\r", "\r\n"]

    function send() {
        if (!terminal.canSend || input.text.length === 0)
            return
        if (terminal.board.sendUartText(input.text + terminal.lineEndings[lineEnding.currentIndex]))
            input.clear()
    }

    function statusText() {
        const c = terminal.uart
        if (c === null)
            return ""
        const parts = []
        if (c.txBound)
            parts.push(qsTr("TX %1 B").arg(c.txBytes))
        if (c.rxBound)
            parts.push(qsTr("RX %1 B").arg(c.rxBytes))
        if (c.pendingRxBytes > 0)
            parts.push(qsTr("%1 B queued").arg(c.pendingRxBytes))
        if (c.framingErrors > 0)
            parts.push(c.framingErrors === 1 ? qsTr("1 framing error")
                                             : qsTr("%1 framing errors").arg(c.framingErrors))
        return parts.join("  ·  ")
    }

    // Columns stay aligned through 999 virtual seconds, then widen per row.
    TextMetrics { id: timeMetrics; font.family: terminal.monoFamily; font.pixelSize: 12; text: "000.000000000 s" }
    TextMetrics { id: cycleMetrics; font.family: terminal.monoFamily; font.pixelSize: 12; text: "cycle 00000000000" }

    contentItem: ColumnLayout {
        spacing: 0

        // Outside the ListView: a header whose height follows its visibility
        // re-enters the view's layout and reports a binding loop.
        Label {
            objectName: "uartTrimmedNotice"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 2
            Layout.bottomMargin: 2
            visible: terminal.uart !== null && terminal.uart.trimmedLines > 0
            text: terminal.uart === null ? ""
                : qsTr("%1 earlier lines discarded. Scrollback keeps the latest %2 lines.")
                    .arg(terminal.uart.trimmedLines).arg(terminal.uart.maximumLines)
            color: terminal.palette.placeholderText
            font.pixelSize: 11
            font.italic: true
            elide: Text.ElideRight
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: lines
                objectName: "uartLines"
                // Follow new output until the user scrolls away from the end.
                property bool following: true
                anchors.fill: parent
                clip: true
                model: terminal.uart
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                Accessible.name: qsTr("UART scrollback")
                // No top/bottom margins: positionViewAtEnd() ignores them, so
                // following would stop short of atYEnd.

                // Always re-position while following: at the row limit the
                // cached origin and height can be stale when this runs, so a
                // geometry-based skip would stop following (regression-tested).
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
                // At the row limit an insert and a trim leave count and
                // contentHeight unchanged while the origin moves.
                onOriginYChanged: Qt.callLater(lines.followLatest)
                onContentHeightChanged: Qt.callLater(lines.followLatest)
                onHeightChanged: Qt.callLater(lines.followLatest)
                onMovementStarted: following = false
                onMovementEnded: following = atYEnd

                Connections {
                    target: terminal.uart
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
                    objectName: "uartScrollBar"
                    policy: ScrollBar.AsNeeded
                    onPressedChanged: lines.following = !pressed && lines.atYEnd
                }

                delegate: Item {
                    id: row
                    required property int index
                    required property int kind
                    required property string text
                    required property string hex
                    required property int byteCount
                    required property string cycle
                    required property string lastCycle
                    required property string time
                    readonly property bool notice: kind === UartConsoleModel.Notice
                    width: lines.width
                    height: Math.max(20, rowLayout.implicitHeight + 2)

                    HoverHandler { id: rowHover }
                    ToolTip.visible: rowHover.hovered && !row.notice
                    ToolTip.delay: 600
                    ToolTip.text: row.notice ? ""
                        : qsTr("%1 bytes · cycles %2–%3\n%4").arg(row.byteCount)
                            .arg(row.cycle).arg(row.lastCycle).arg(row.hex)

                    RowLayout {
                        id: rowLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 12
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Label {
                            Layout.alignment: Qt.AlignTop
                            Layout.minimumWidth: timeMetrics.advanceWidth
                            horizontalAlignment: Text.AlignRight
                            text: row.time
                            color: terminal.palette.placeholderText
                            font.family: terminal.monoFamily
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.alignment: Qt.AlignTop
                            Layout.minimumWidth: cycleMetrics.advanceWidth
                            text: qsTr("cycle %1").arg(row.cycle)
                            color: terminal.palette.placeholderText
                            font.family: terminal.monoFamily
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 24
                            text: row.kind === UartConsoleModel.Tx ? "TX"
                                : row.kind === UartConsoleModel.Rx ? "RX" : "··"
                            color: row.kind === UartConsoleModel.Tx ? terminal.txColor
                                : row.kind === UartConsoleModel.Rx ? terminal.rxColor
                                : terminal.palette.placeholderText
                            font.family: terminal.monoFamily
                            font.pixelSize: 12
                            font.bold: !row.notice
                        }
                        Label {
                            objectName: "uartLineText" + row.index
                            Layout.fillWidth: true
                            text: row.notice || !terminal.hexView ? row.text : row.hex
                            color: row.notice ? terminal.palette.placeholderText : terminal.palette.text
                            font.family: terminal.monoFamily
                            font.pixelSize: 12
                            font.italic: row.notice
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }

            // Keyboard focus outline; a ListView child would scroll with rows.
            Rectangle {
                anchors.fill: lines
                visible: lines.activeFocus
                color: "transparent"
                border.color: terminal.palette.highlight
                border.width: 1
            }

            ColumnLayout {
                objectName: "uartEmptyState"
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 620)
                visible: lines.count === 0
                spacing: 4
                Label {
                    objectName: "uartEmptyTitle"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: terminal.uart !== null
                          && terminal.uart.txBytes + terminal.uart.rxBytes > 0
                        ? qsTr("Scrollback cleared") : qsTr("No UART traffic yet")
                    font.weight: Font.DemiBold
                }
                Label {
                    objectName: "uartEmptyDetail"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: terminal.palette.placeholderText
                    text: terminal.uart === null ? ""
                        : terminal.uart.txBytes + terminal.uart.rxBytes > 0
                        ? qsTr("New traffic appears here. Session byte totals are unchanged.")
                        : terminal.uart.rxBound && terminal.uart.txBound
                        ? qsTr("Type below and press Send. Bytes transmit as simulation time advances; design output appears with its virtual cycle.")
                        : terminal.uart.txBound
                        ? qsTr("Design output on RsTx (A18) appears here with its virtual cycle. This design does not bind RsRx (B18).")
                        : qsTr("This design does not bind RsTx (A18); only bytes sent to RsRx (B18) appear here.")
                }
            }

            Button {
                objectName: "uartLatestButton"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 22
                anchors.bottomMargin: 6
                visible: !lines.following && lines.count > 0
                text: qsTr("Latest ↓")
                font.pixelSize: 11
                onClicked: lines.showLatest()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Scroll to the newest line and keep following output")
            }
        }

        Label {
            objectName: "uartSendError"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            visible: text.length > 0
            text: terminal.uart !== null ? terminal.uart.sendError : ""
            color: "#f0b190"
            font.pixelSize: 11
            elide: Text.ElideRight
            ToolTip.visible: sendErrorHover.hovered && truncated
            ToolTip.text: text
            HoverHandler { id: sendErrorHover }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: terminal.palette.mid
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 8
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            spacing: 6
            TextField {
                id: input
                objectName: "uartInput"
                Layout.fillWidth: true
                Layout.minimumWidth: 160
                implicitHeight: 30
                enabled: terminal.canSend
                maximumLength: terminal.uart !== null ? terminal.uart.maximumPendingRxBytes : 0
                placeholderText: terminal.canSend
                    ? qsTr("Send UTF-8 text to RsRx")
                    : qsTr("RsRx (B18) is not bound in this design")
                font.family: terminal.monoFamily
                font.pixelSize: 12
                Accessible.name: qsTr("UART input")
                onAccepted: terminal.send()
            }
            ComboBox {
                id: lineEnding
                objectName: "uartLineEnding"
                Layout.preferredWidth: 132
                implicitHeight: 30
                enabled: terminal.canSend
                model: [qsTr("No line ending"), qsTr("LF (\\n)"), qsTr("CR (\\r)"), qsTr("CR+LF")]
                font.pixelSize: 12
                Accessible.name: qsTr("Line ending appended to each send")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Bytes appended after the typed text")
            }
            Button {
                objectName: "uartSendButton"
                implicitHeight: 30
                text: qsTr("Send")
                enabled: terminal.canSend && input.text.length > 0
                onClicked: terminal.send()
            }
            Label {
                objectName: "uartStatus"
                Layout.leftMargin: 6
                Layout.maximumWidth: 330
                text: terminal.statusText()
                color: terminal.palette.placeholderText
                font.pixelSize: 11
                elide: Text.ElideRight
                ToolTip.visible: statusHover.hovered
                ToolTip.text: qsTr("TX: design output decoded from RsTx. RX: host input sent to RsRx. Queued bytes are waiting for, or still occupying, the serial line.")
                HoverHandler { id: statusHover }
            }
            ToolButton {
                objectName: "uartHexToggle"
                text: qsTr("Hex")
                checkable: true
                checked: terminal.hexView
                focusPolicy: Qt.StrongFocus
                onToggled: terminal.hexView = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Show each line's exact bytes in hexadecimal")
            }
            ToolButton {
                objectName: "uartClearButton"
                text: qsTr("Clear")
                enabled: terminal.uart !== null
                         && (terminal.uart.count > 0 || terminal.uart.sendError.length > 0)
                focusPolicy: Qt.StrongFocus
                onClicked: terminal.board.clearUart()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Clear the scrollback. Simulation state and queued input are unchanged.")
            }
        }
    }
}
