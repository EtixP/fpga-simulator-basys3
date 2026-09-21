pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VirtualBasys.Board

Control {
    id: view
    required property BoardAdapter board
    property string headingObjectName: "workspaceTitle"
    property string detailObjectName: "workspaceDetail"
    readonly property bool connected: board !== null && board.connected

    function revealFocusedControl() {
        const host = view.Window.window
        if (!host || !view.visible || !host.activeFocusItem)
            return
        const item = host.activeFocusItem
        let ancestor = item.parent
        while (ancestor && ancestor !== view)
            ancestor = ancestor.parent
        if (ancestor !== view)
            return
        const position = item.mapToItem(viewport.contentItem, 0, 0)
        let left = viewport.contentX
        let top = viewport.contentY
        if (position.x < left)
            left = position.x - 8
        else if (position.x + item.width > left + viewport.width)
            left = position.x + item.width - viewport.width + 8
        if (position.y < top)
            top = position.y - 8
        else if (position.y + item.height > top + viewport.height)
            top = position.y + item.height - viewport.height + 8
        viewport.contentX = Math.max(0, Math.min(left, viewport.contentWidth - viewport.width))
        viewport.contentY = Math.max(0, Math.min(top, viewport.contentHeight - viewport.height))
    }

    Connections {
        target: view.Window.window
        function onActiveFocusItemChanged() { view.revealFocusedControl() }
    }

    // Preserve usable physical control sizes at narrow pane widths. Scrolling
    // changes only the viewport; model rows always retain their board indices.
    contentItem: Flickable {
        id: viewport
        objectName: "boardFlickable"
        clip: true
        contentWidth: Math.max(width, 744)
        contentHeight: contents.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds
        onWidthChanged: Qt.callLater(view.revealFocusedControl)
        onHeightChanged: Qt.callLater(view.revealFocusedControl)
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: contents
            x: 16
            y: 16
            width: viewport.contentWidth - 32
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("BASYS 3")
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        objectName: view.headingObjectName
                        text: view.connected ? qsTr("Board connected") : qsTr("No design loaded")
                        color: view.palette.placeholderText
                    }
                }
                Label {
                    objectName: view.detailObjectName
                    Layout.fillWidth: true
                    text: view.connected
                        ? qsTr("Only bound resources are enabled. Click a switch; hold a button to press it.")
                        : qsTr("Board preview. Inputs are unavailable until a design is connected.")
                    color: view.palette.placeholderText
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 226
                    radius: 10
                    color: view.palette.base
                    border.color: view.palette.mid
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8
                        Label {
                            text: qsTr("SEVEN-SEGMENT DISPLAY")
                            font.pixelSize: 10
                            font.letterSpacing: 1
                            color: view.palette.placeholderText
                        }
                        Item { Layout.fillHeight: true }
                        Row {
                            Layout.alignment: Qt.AlignHCenter
                            layoutDirection: Qt.RightToLeft
                            spacing: 10
                            Repeater {
                                model: view.board ? view.board.digits : null
                                SevenSegmentDigit {
                                    objectName: "digit" + digit
                                    available: view.board !== null && view.board.hasDisplay
                                }
                            }
                        }
                        Item { Layout.fillHeight: true }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: view.board !== null && view.board.hasDisplay
                                ? qsTr("AN3  →  AN0") : qsTr("Display not bound")
                            color: view.palette.placeholderText
                            font.pixelSize: 11
                        }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 230
                    Layout.preferredHeight: 226
                    radius: 10
                    color: view.palette.base
                    border.color: view.palette.mid
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8
                        Label {
                            text: qsTr("PUSH BUTTONS")
                            font.pixelSize: 10
                            font.letterSpacing: 1
                            color: view.palette.placeholderText
                        }
                        GridLayout {
                            Layout.alignment: Qt.AlignHCenter
                            columns: 3
                            rowSpacing: 5
                            columnSpacing: 5
                            Repeater {
                                model: view.board ? view.board.buttons : null
                                BoardButton {
                                    required property int index
                                    objectName: "button" + index
                                    Layout.row: [1, 0, 1, 1, 2][index]
                                    Layout.column: [1, 1, 0, 2, 1][index]
                                    // Capture the target, so a held press can be
                                    // released even when this view is rebound.
                                    writeInput: {
                                        const target = view.board
                                        const row = index
                                        return function(held) {
                                            return target !== null && target.setButton(row, held)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: ioLayout.implicitHeight + 28
                radius: 10
                color: view.palette.base
                border.color: view.palette.mid
                ColumnLayout {
                    id: ioLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 14
                    spacing: 8
                    Label {
                        text: qsTr("LEDS / SWITCHES")
                        font.pixelSize: 10
                        font.letterSpacing: 1
                        color: view.palette.placeholderText
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        layoutDirection: Qt.RightToLeft
                        spacing: 2
                        Repeater {
                            model: view.board ? view.board.leds : null
                            BoardLed {
                                required property int index
                                objectName: "led" + index
                                Layout.fillWidth: true
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        layoutDirection: Qt.RightToLeft
                        spacing: 2
                        Repeater {
                            model: view.board ? view.board.switches : null
                            BoardSwitch {
                                required property int index
                                objectName: "sw" + index
                                Layout.fillWidth: true
                                onToggleRequested: function(value) {
                                    if (view.board) view.board.setSwitch(index, value)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
