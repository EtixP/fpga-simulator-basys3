pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VirtualBasys.Board

// Presentation of the adapter's signal snapshot. Values are read in C++ in
// one pass per refresh and arrive as changed-row ranges; filtering is a C++
// proxy. Delegates only bind roles.
Control {
    id: panel
    required property BoardAdapter board
    property string designStem: ""
    readonly property SignalInspectorModel inspector: board.inspector
    readonly property color changedColor: "#3a2f1a"
    readonly property string monoFamily: "Menlo"
    padding: 0

    function addWatch() {
        if (watchInput.text.trim().length > 0 && panel.board.addWatch(watchInput.text))
            watchInput.clear()
    }

    contentItem: ColumnLayout {
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            Label {
                objectName: "inspectorSnapshot"
                Layout.fillWidth: true
                text: panel.inspector.snapshotCycleText.length > 0
                    ? qsTr("Values at cycle %1").arg(panel.inspector.snapshotCycleText) : ""
                color: panel.palette.placeholderText
                font.pixelSize: 11
                elide: Text.ElideRight
                ToolTip.visible: snapshotHover.hovered
                ToolTip.text: qsTr("All values were read together at virtual time %1. Changed rows differ from the previous snapshot.")
                    .arg(panel.inspector.snapshotTimeText)
                HoverHandler { id: snapshotHover }
            }
        }

        TextField {
            id: filter
            objectName: "inspectorFilter"
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            implicitHeight: 28
            placeholderText: qsTr("Filter signals")
            font.pixelSize: 12
            Accessible.name: qsTr("Filter signals by name")
            onTextChanged: panel.board.inspectorView.filterText = text
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Every row exists: at most the design's ports plus 32 watches. A
            // ListView would create only rows near the view, and Tab would then
            // skip the remove buttons of rows not yet created. A Repeater keeps
            // its rows in model order, so Tab and Shift+Tab follow row order.
            Flickable {
                id: signalList
                objectName: "inspectorList"
                readonly property int count: signalRows.count
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: signalColumn.height
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                Accessible.name: qsTr("Design signals")
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                function scrollTo(y) {
                    contentY = Math.max(0, Math.min(Math.max(0, contentHeight - height), y))
                }
                function showRow(item) {
                    if (item.y < contentY)
                        scrollTo(item.y)
                    else if (item.y + item.height > contentY + height)
                        scrollTo(item.y + item.height - height)
                }
                Keys.onPressed: (event) => {
                    switch (event.key) {
                    case Qt.Key_Up: signalList.scrollTo(signalList.contentY - 24); break
                    case Qt.Key_Down: signalList.scrollTo(signalList.contentY + 24); break
                    case Qt.Key_PageUp: signalList.scrollTo(signalList.contentY - signalList.height * 0.9); break
                    case Qt.Key_PageDown: signalList.scrollTo(signalList.contentY + signalList.height * 0.9); break
                    case Qt.Key_Home: signalList.scrollTo(0); break
                    case Qt.Key_End: signalList.scrollTo(signalList.contentHeight); break
                    default: return
                    }
                    event.accepted = true
                }

                Column {
                    id: signalColumn
                    width: signalList.width

                    Repeater {
                        id: signalRows
                        model: panel.board.inspectorView
                        delegate: Rectangle {
                            id: row
                            required property string name
                            required property int kind
                            required property string range
                            required property string value
                            required property string decimal
                            required property string binding
                            required property bool changed
                            required property bool watch
                            required property int bits
                            objectName: "signalRow_" + name
                            width: signalColumn.width
                            height: 44  // one height for port and watch rows
                            color: row.shownChanged ? panel.changedColor : "transparent"

                            // Rows out of view keep their last text and catch up as they
                            // scroll into view, so a refresh lays out visible rows only.
                            // All rows exist, so this matters with many changing watches.
                            readonly property bool inView: y + height > signalList.contentY
                                && y < signalList.contentY + signalList.height
                            property string shownValue
                            property string shownDecimal
                            property bool shownChanged
                            Binding on shownValue { when: row.inView; value: row.value; restoreMode: Binding.RestoreNone }
                            Binding on shownDecimal { when: row.inView; value: row.decimal; restoreMode: Binding.RestoreNone }
                            Binding on shownChanged { when: row.inView; value: row.changed; restoreMode: Binding.RestoreNone }

                            ColumnLayout {
                                id: rowLayout
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 12
                                anchors.rightMargin: 10
                                spacing: 1
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: 20  // the × button's height: one layout for all rows
                                    spacing: 6
                                    Label {
                                        Layout.fillWidth: true
                                        text: row.name + row.range
                                        font.family: panel.monoFamily
                                        font.pixelSize: 12
                                        elide: Text.ElideMiddle
                                    }
                                    Label {
                                        objectName: "signalValue_" + row.name
                                        text: row.shownValue
                                        font.family: panel.monoFamily
                                        font.pixelSize: 12
                                        font.bold: row.shownChanged
                                        color: row.shownChanged ? "#f0c674" : panel.palette.text
                                    }
                                    ToolButton {
                                        objectName: "removeWatch_" + row.name
                                        visible: row.watch
                                        implicitWidth: 22
                                        implicitHeight: 20
                                        text: "×"
                                        focusPolicy: Qt.StrongFocus
                                        Accessible.name: qsTr("Remove watch %1").arg(row.name)
                                        // Keyboard focus may land on a row scrolled out of view.
                                        onActiveFocusChanged: if (activeFocus) signalList.showRow(row)
                                        onClicked: {
                                            // The button goes with its row; keep keyboard focus in the list.
                                            if (visualFocus)
                                                signalList.forceActiveFocus(Qt.TabFocusReason)
                                            panel.board.removeWatch(row.name)
                                        }
                                    }
                                }
                                Label {
                                    objectName: "signalDetail_" + row.name
                                    Layout.fillWidth: true
                                    text: [
                                        [qsTr("input"), qsTr("output"), qsTr("clock"), qsTr("watch")][row.kind],
                                        row.shownValue.startsWith("0x") ? qsTr("= %1").arg(row.shownDecimal) : "",
                                        row.binding
                                    ].filter(part => part.length > 0).join("  ·  ")
                                    color: panel.palette.placeholderText
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                            ToolTip.visible: rowHover.hovered
                            ToolTip.delay: 600
                            // Built only while hovered, not on every refresh of every row.
                            ToolTip.text: !rowHover.hovered ? ""
                                : row.kind === SignalInspectorModel.Clock
                                ? qsTr("The 100 MHz master clock. It is low before the first rising edge and high at every later snapshot, which is taken after a rising edge.")
                                : qsTr("%1%2 · %3 bits · %4 = %5").arg(row.name).arg(row.range).arg(row.bits)
                                    .arg(row.value).arg(row.decimal)
                                  + (row.binding.length > 0 ? qsTr(" · pins %1").arg(row.binding) : "")
                            HoverHandler { id: rowHover }
                        }
                    }
                }
            }

            // Keyboard focus outline; a Flickable child would scroll with rows.
            Rectangle {
                objectName: "inspectorFocusOutline"
                anchors.fill: signalList
                visible: signalList.activeFocus
                color: "transparent"
                border.color: panel.palette.highlight
                border.width: 1
            }

            Label {
                objectName: "inspectorEmptyFilter"
                anchors.centerIn: parent
                width: parent.width - 24
                visible: signalList.count === 0 && panel.inspector.count > 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap  // filter text may have no spaces
                text: qsTr("No signal matches “%1”.").arg(filter.text.trim())
                color: panel.palette.placeholderText
            }
        }

        Label {
            objectName: "watchError"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            visible: text.length > 0
            text: panel.inspector.watchError
            color: "#f0b190"
            font.pixelSize: 11
            // Wrap anywhere as a last resort: hierarchical names have no spaces.
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 8
            spacing: 6
            TextField {
                id: watchInput
                objectName: "watchInput"
                Layout.fillWidth: true
                implicitHeight: 28
                placeholderText: panel.designStem.length > 0
                    ? qsTr("%1.signal").arg(panel.designStem) : qsTr("top.signal")
                font.family: panel.monoFamily
                font.pixelSize: 12
                Accessible.name: qsTr("Hierarchical signal name to watch")
                onAccepted: panel.addWatch()
            }
            Button {
                objectName: "watchButton"
                implicitHeight: 28
                text: qsTr("Watch")
                enabled: watchInput.text.trim().length > 0
                         && panel.inspector.watchCount < panel.inspector.maximumWatches
                onClicked: panel.addWatch()
            }
        }
    }
}
