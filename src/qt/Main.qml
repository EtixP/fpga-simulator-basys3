pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VirtualBasys.Board

ApplicationWindow {
    id: window
    objectName: "shellWindow"
    required property BoardAdapter board
    property int workspaceIndex: 0
    property int outputIndex: 0
    readonly property bool boardConnected: board !== null && board.connected
    readonly property bool boardUnavailable: board === null
    width: 1280
    height: 820
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: qsTr("VirtualBasys")
    color: palette.window
    font.family: "Helvetica Neue"
    font.pixelSize: 13
    palette.window: "#101720"
    palette.base: "#121c27"
    palette.alternateBase: "#192633"
    palette.text: "#dce7f0"
    palette.windowText: "#dce7f0"
    palette.button: "#233445"
    palette.buttonText: "#dce7f0"
    palette.highlight: "#248b85"
    palette.highlightedText: "#ffffff"
    palette.light: "#536477"
    palette.mid: "#304254"
    palette.dark: "#0b121a"
    palette.placeholderText: "#8d9fb1"

    // Actions own visibility. A hidden pane never retains keyboard focus.
    Action {
        id: projectAction
        objectName: "projectAction"
        text: qsTr("Project")
        checkable: true
        checked: true
        shortcut: "Ctrl+Shift+1"
        onTriggered: projectToggle.forceActiveFocus()
    }
    Action {
        id: inspectorAction
        objectName: "inspectorAction"
        text: qsTr("Inspector")
        checkable: true
        checked: true
        shortcut: "Ctrl+Shift+2"
        onTriggered: inspectorToggle.forceActiveFocus()
    }
    Action {
        id: outputAction
        objectName: "outputAction"
        text: qsTr("Output")
        checkable: true
        checked: true
        shortcut: "Ctrl+Shift+3"
        onTriggered: outputToggle.forceActiveFocus()
    }
    Action {
        id: restoreAction
        text: qsTr("Restore layout")
        shortcut: "Ctrl+Shift+0"
        onTriggered: {
            projectAction.checked = true
            inspectorAction.checked = true
            outputAction.checked = true
            projectPane.SplitView.preferredWidth = 220
            inspectorPane.SplitView.preferredWidth = 260
            outputPane.SplitView.preferredHeight = 232
            window.workspaceIndex = 0
            window.outputIndex = 0
            restoreButton.forceActiveFocus()
        }
    }

    header: ToolBar {
        height: 56
        background: Rectangle { color: window.palette.alternateBase }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 12
            spacing: 8
            Rectangle {
                implicitWidth: 28
                implicitHeight: 28
                radius: 6
                color: window.palette.highlight
                Label { anchors.centerIn: parent; text: "V"; font.bold: true; font.pixelSize: 17 }
            }
            Label { text: qsTr("VirtualBasys"); font.pixelSize: 17; font.weight: Font.DemiBold }
            Label {
                Layout.leftMargin: 12
                text: qsTr("FPGA workspace")
                color: window.palette.placeholderText
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                id: projectToggle
                objectName: "projectToggle"
                action: projectAction
                focusPolicy: Qt.StrongFocus
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Show or hide the project pane")
            }
            ToolButton {
                id: inspectorToggle
                objectName: "inspectorToggle"
                action: inspectorAction
                focusPolicy: Qt.StrongFocus
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Show or hide the inspector")
            }
            ToolButton {
                id: outputToggle
                objectName: "outputToggle"
                action: outputAction
                focusPolicy: Qt.StrongFocus
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Show or hide the output panel")
            }
            ToolSeparator {}
            ToolButton {
                id: restoreButton
                objectName: "restoreLayoutButton"
                action: restoreAction
                focusPolicy: Qt.StrongFocus
            }
        }
    }

    SplitView {
        id: verticalSplit
        objectName: "verticalSplit"
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 6
            color: SplitHandle.pressed ? window.palette.highlight
                                      : SplitHandle.hovered ? window.palette.mid : window.palette.window
        }

        SplitView {
            id: horizontalSplit
            objectName: "horizontalSplit"
            SplitView.fillHeight: true
            SplitView.minimumHeight: 320
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 6
                color: SplitHandle.pressed ? window.palette.highlight
                                          : SplitHandle.hovered ? window.palette.mid : window.palette.window
            }

            ShellPane {
                id: projectPane
                objectName: "projectPane"
                visible: projectAction.checked
                SplitView.preferredWidth: 220
                SplitView.minimumWidth: 180
                SplitView.maximumWidth: 360
                title: qsTr("PROJECT")
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        text: qsTr("No project open")
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Design files will appear here when project loading is available.")
                        color: window.palette.placeholderText
                        wrapMode: Text.WordWrap
                        lineHeight: 1.25
                    }
                    Label {
                        Layout.topMargin: 24
                        text: qsTr("WORKSPACE")
                        color: window.palette.placeholderText
                        font.pixelSize: 10
                        font.letterSpacing: 1
                    }
                    Button {
                        objectName: "boardNav"
                        Layout.fillWidth: true
                        text: qsTr("Board")
                        highlighted: window.workspaceIndex === 0
                        onClicked: window.workspaceIndex = 0
                    }
                    Button {
                        objectName: "overviewNav"
                        Layout.fillWidth: true
                        text: qsTr("Overview")
                        highlighted: window.workspaceIndex === 1
                        onClicked: window.workspaceIndex = 1
                    }
                    Item { Layout.fillHeight: true }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Basys 3 · Artix-7")
                        color: window.palette.placeholderText
                        font.pixelSize: 11
                    }
                }
            }

            ShellPane {
                objectName: "workspacePane"
                SplitView.fillWidth: true
                SplitView.minimumWidth: 400
                title: window.workspaceIndex === 0 ? qsTr("BOARD") : qsTr("OVERVIEW")
                paneColor: window.palette.window
                EmptyState {
                    anchors.fill: parent
                    badge: window.workspaceIndex === 0 ? "B3" : "VB"
                    headingObjectName: "workspaceTitle"
                    detailObjectName: "workspaceDetail"
                    heading: window.boardUnavailable ? qsTr("Board connection unavailable")
                        : window.workspaceIndex === 1 ? qsTr("Your FPGA workspace")
                        : window.boardConnected ? qsTr("Board connected") : qsTr("No design loaded")
                    detail: window.boardUnavailable
                        ? qsTr("The board connection could not be initialized. Restart VirtualBasys to try again.")
                        : window.workspaceIndex === 1
                          ? qsTr("Arrange the project, board, inspector and output panes to suit your workspace. Use Restore layout to return to the default arrangement.")
                          : window.boardConnected
                            ? qsTr("The board is connected. Interactive board controls are not available in this version.")
                            : qsTr("The virtual board will appear here. Design loading and interactive board controls are not available in this version.")
                }
            }

            ShellPane {
                id: inspectorPane
                objectName: "inspectorPane"
                visible: inspectorAction.checked
                SplitView.preferredWidth: 260
                SplitView.minimumWidth: 220
                SplitView.maximumWidth: 420
                title: qsTr("INSPECTOR")
                EmptyState {
                    anchors.fill: parent
                    compact: true
                    heading: qsTr("Nothing to inspect")
                    detail: qsTr("Signal inspection will appear here when a design and inspector are available.")
                }
            }
        }

        Rectangle {
            id: outputPane
            objectName: "outputPane"
            visible: outputAction.checked
            SplitView.preferredHeight: 232
            SplitView.minimumHeight: 170
            SplitView.maximumHeight: 400
            color: window.palette.base
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    TabBar {
                        id: outputTabs
                        Layout.fillWidth: true
                        currentIndex: window.outputIndex
                        onCurrentIndexChanged: window.outputIndex = currentIndex
                        background: Rectangle { color: window.palette.alternateBase }
                        TabButton { objectName: "terminalTab"; text: qsTr("Terminal"); width: implicitWidth + 16 }
                        TabButton { objectName: "uartTab"; text: qsTr("UART"); width: implicitWidth + 16 }
                        TabButton { objectName: "logsTab"; text: qsTr("Logs"); width: implicitWidth + 16 }
                        TabButton { objectName: "waveformsTab"; text: qsTr("Waveforms"); width: implicitWidth + 16 }
                    }
                    ToolButton {
                        objectName: "closeOutputButton"
                        text: qsTr("Hide")
                        focusPolicy: Qt.StrongFocus
                        onClicked: outputAction.trigger()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Hide the output panel")
                    }
                }
                EmptyState {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    compact: true
                    headingObjectName: "outputTitle"
                    detailObjectName: "outputDetail"
                    heading: [qsTr("No terminal session"), qsTr("No UART connection"),
                              qsTr("No simulation log"), qsTr("No waveform open")][window.outputIndex]
                    detail: [qsTr("Command execution is not available in this version."),
                             qsTr("UART traffic and send controls will appear here when a simulation is connected."),
                             qsTr("Simulation events will appear here when log collection is available."),
                             qsTr("Waveform viewing is not available in this version.")][window.outputIndex]
                }
            }
        }
    }

    footer: ToolBar {
        height: 28
        background: Rectangle { color: window.palette.alternateBase }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 8
            Rectangle {
                implicitWidth: 6
                implicitHeight: 6
                radius: 3
                color: window.boardUnavailable ? "#e7a06c" : window.boardConnected
                       ? window.palette.highlight : window.palette.placeholderText
            }
            Label {
                text: window.boardUnavailable ? qsTr("Board unavailable") : window.boardConnected
                      ? qsTr("Board connected") : qsTr("No design loaded")
                font.pixelSize: 11
                color: window.palette.placeholderText
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("VirtualBasys"); font.pixelSize: 11; color: window.palette.placeholderText }
        }
    }
}
