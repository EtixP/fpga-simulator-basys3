pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VirtualBasys.Board

ApplicationWindow {
    id: window
    objectName: "shellWindow"
    required property BoardAdapter board
    property SimulationController controller: null
    // Example file stem (for example "uart_echo"); defaults to the design name.
    property string designSource: ""
    readonly property string designFileStem: designSource.length > 0 ? designSource
        : controller !== null ? controller.designName.toLowerCase() : ""
    property int workspaceIndex: 0
    readonly property bool boardConnected: board !== null && board.connected
    readonly property bool boardUnavailable: board === null
    readonly property bool liveSimulation: controller !== null && controller.connected
    readonly property bool uartReady: boardConnected && board.uart !== null && board.uart.available
    // Designs that bind the USB-UART pins open on their terminal; other loaded
    // designs on their event log. Without a design, the first tab.
    readonly property int defaultOutputIndex: uartReady ? 1 : boardConnected ? 2 : 0
    property int outputIndex: defaultOutputIndex
    readonly property bool showUartTerminal: outputIndex === 1 && uartReady
    readonly property bool showEventLog: outputIndex === 2 && boardConnected
    readonly property string logUnavailableDetail: boardUnavailable
        ? qsTr("The board connection could not be initialized, so there are no board events.")
        : qsTr("No design is loaded. Cycle-stamped board events appear here once a design runs.")
    readonly property string uartUnavailableDetail: boardUnavailable
        ? qsTr("The board connection could not be initialized, so there is no UART to monitor.")
        : !boardConnected
        ? qsTr("No design is loaded. The terminal opens for designs that bind the USB-UART pins (RsRx B18, RsTx A18).")
        : qsTr("This design does not bind the USB-UART pins (RsRx B18, RsTx A18).")
    width: 1280
    height: liveSimulation ? 880 : 820
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: liveSimulation ? qsTr("VirtualBasys — %1").arg(controller.designName) : qsTr("VirtualBasys")
    color: palette.window
    font.family: "Helvetica Neue"
    font.pixelSize: 13
    palette.window: "#101720"
    palette.base: "#121c27"
    palette.alternateBase: "#192633"
    // Text roles per color group: disabled controls must look disabled.
    palette.active.text: "#dce7f0"
    palette.inactive.text: "#dce7f0"
    palette.disabled.text: "#687a8c"
    palette.active.windowText: "#dce7f0"
    palette.inactive.windowText: "#dce7f0"
    palette.disabled.windowText: "#687a8c"
    palette.button: "#233445"
    palette.active.buttonText: "#dce7f0"
    palette.inactive.buttonText: "#dce7f0"
    palette.disabled.buttonText: "#687a8c"
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
            outputPane.SplitView.preferredHeight = 170
            window.workspaceIndex = 0
            window.outputIndex = window.defaultOutputIndex
            restoreButton.forceActiveFocus()
        }
    }

    header: ToolBar {
        height: 56 + (simulationTools.visible ? simulationTools.implicitHeight : 0)
        background: Rectangle { color: window.palette.alternateBase }
        RowLayout {
            id: applicationTools
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 56
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
        SimulationToolbar {
            id: simulationTools
            objectName: "simulationToolbar"
            anchors.top: applicationTools.bottom
            width: parent.width
            height: implicitHeight
            visible: window.liveSimulation
            controller: window.controller
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
                        objectName: "projectDesignName"
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        text: window.liveSimulation ? window.controller.designName : qsTr("No project open")
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "projectDesignFiles"
                        Layout.fillWidth: true
                        text: window.liveSimulation
                            ? "examples/" + window.designFileStem + ".v\nexamples/"
                                + window.designFileStem + ".xdc"
                            : qsTr("Design files will appear here when project loading is available.")
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
                    visible: window.boardUnavailable || window.workspaceIndex === 1
                    anchors.fill: parent
                    badge: window.workspaceIndex === 0 ? "B3" : "VB"
                    headingObjectName: visible ? "workspaceTitle" : ""
                    detailObjectName: visible ? "workspaceDetail" : ""
                    heading: window.boardUnavailable ? qsTr("Board connection unavailable")
                        : qsTr("Your FPGA workspace")
                    detail: window.boardUnavailable
                        ? qsTr("The board connection could not be initialized. Restart VirtualBasys to try again.")
                        : qsTr("Arrange the project, board, inspector and output panes to suit your workspace. Use Restore layout to return to the default arrangement.")
                }
                BoardView {
                    objectName: "boardView"
                    anchors.fill: parent
                    visible: !window.boardUnavailable && window.workspaceIndex === 0
                    board: window.board
                    controller: window.controller
                    headingObjectName: visible ? "workspaceTitle" : ""
                    detailObjectName: visible ? "workspaceDetail" : ""
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
                    visible: !window.boardConnected
                    compact: true
                    heading: qsTr("Nothing to inspect")
                    detail: window.boardUnavailable
                        ? qsTr("The board connection could not be initialized.")
                        : qsTr("Signal values appear here when a design is loaded.")
                }
                // Created only with a board: the panel binds to its models.
                Loader {
                    objectName: "inspectorLoader"
                    anchors.fill: parent
                    active: window.boardConnected
                    visible: active
                    // Anchored: a hidden Loader gets no layout size, and the
                    // panel must still re-layout once it is shown.
                    sourceComponent: InspectorPanel {
                        objectName: "inspectorPanel"
                        anchors.fill: parent
                        board: window.board
                        designStem: window.designFileStem
                    }
                }
            }
        }

        Rectangle {
            id: outputPane
            objectName: "outputPane"
            visible: outputAction.checked
            SplitView.preferredHeight: 170
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
                // A StackLayout sizes every page, shown or not, so a page shown
                // for the first time is already in place (a plain layout would
                // leave it over the tab bar until its next polish).
                StackLayout {
                    objectName: "outputStack"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: window.showUartTerminal ? 1 : window.showEventLog ? 2 : 0
                    EmptyState {
                        compact: true
                        headingObjectName: visible ? "outputTitle" : ""
                        detailObjectName: visible ? "outputDetail" : ""
                        heading: [qsTr("No terminal session"), qsTr("UART terminal unavailable"),
                                  qsTr("Log view unavailable"), qsTr("No waveform open")][window.outputIndex]
                        detail: [qsTr("Command execution is not available in this version."),
                                 window.uartUnavailableDetail,
                                 window.logUnavailableDetail,
                                 qsTr("Waveform viewing is not available in this version.")][window.outputIndex]
                    }
                    UartTerminal {
                        objectName: "uartTerminal"
                        board: window.board
                    }
                    // Kept alive while a board exists, so filters and rows persist.
                    Loader {
                        objectName: "eventLogLoader"
                        active: window.boardConnected
                        sourceComponent: EventLogView {
                            objectName: "eventLogView"
                            anchors.fill: parent
                            board: window.board
                        }
                    }
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
                objectName: "simulationStatus"
                text: window.liveSimulation
                    ? window.controller.designName + " · " + (window.controller.finished ? qsTr("Finished")
                        : window.controller.running ? qsTr("Running") : qsTr("Paused"))
                    : window.boardUnavailable ? qsTr("Board unavailable") : window.boardConnected
                      ? qsTr("Board connected") : qsTr("No design loaded")
                font.pixelSize: 11
                color: window.palette.placeholderText
            }
            Label {
                objectName: "simulationCycles"
                visible: window.liveSimulation
                Layout.leftMargin: 12
                text: window.liveSimulation ? qsTr("%1 cycles").arg(window.controller.cycleText) : ""
                font.pixelSize: 11
                color: window.palette.text
            }
            Label {
                objectName: "simulationTime"
                visible: window.liveSimulation
                Layout.leftMargin: 8
                text: window.liveSimulation ? window.controller.virtualTimeText : ""
                font.pixelSize: 11
                color: window.palette.text
            }
            Label {
                objectName: "simulationScript"
                visible: window.liveSimulation && window.controller.scripted
                Layout.leftMargin: 12
                text: !visible ? ""
                    : window.controller.finished ? qsTr("Scripted run complete")
                    : window.controller.endCycleText.length > 0
                    ? qsTr("Scripted run · ends at cycle %1").arg(window.controller.endCycleText)
                    : qsTr("Scripted run")
                font.pixelSize: 11
                color: window.palette.placeholderText
            }
            Label {
                objectName: "simulationVgaFrame"
                visible: window.boardConnected && window.board.vga.available
                Layout.leftMargin: 12
                text: !visible ? ""
                    : window.board.vga.hasFrame
                    ? qsTr("VGA frame %1 · cycle %2").arg(window.board.vga.completedFrames - 1)
                        .arg(window.board.vga.frameCycleText)
                    : qsTr("VGA: no complete frame yet")
                font.pixelSize: 11
                color: visible && !window.board.vga.ok ? "#f0b190" : window.palette.placeholderText
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "simulationSpeed"
                text: !window.liveSimulation ? qsTr("VirtualBasys")
                    : !window.controller.running ? qsTr("Speed —")
                    : !window.controller.speedAvailable ? qsTr("Measuring speed…")
                    : qsTr("%1 MHz · %2× real-time")
                        .arg((window.controller.cyclesPerSecond / 1000000).toFixed(2))
                        .arg(window.controller.realtimeMultiplier.toFixed(2))
                font.pixelSize: 11
                color: window.palette.placeholderText
            }
        }
    }
}
