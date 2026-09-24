import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VirtualBasys.Board

Control {
    id: controls
    required property SimulationController controller
    readonly property bool ready: controller !== null && controller.connected
    readonly property bool canAdvance: ready && controller.errorString.length === 0
    implicitHeight: contents.implicitHeight + topPadding + bottomPadding
    leftPadding: 18
    rightPadding: 14
    topPadding: 6
    bottomPadding: 6

    contentItem: ColumnLayout {
        id: contents
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                objectName: "runPauseButton"
                Layout.preferredWidth: 88
                text: controls.ready && controls.controller.running ? qsTr("Pause") : qsTr("Run")
                enabled: controls.canAdvance
                highlighted: controls.ready && !controls.controller.running
                onClicked: {
                    if (controls.controller.running) controls.controller.pause()
                    else controls.controller.run()
                }
            }
            Button {
                objectName: "stepButton"
                text: qsTr("Step")
                enabled: controls.canAdvance && !controls.controller.running
                onClicked: controls.controller.step(stepCycles.value)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Advance the selected number of cycles while paused")
            }
            SpinBox {
                id: stepCycles
                objectName: "stepCycles"
                // Wide enough for "1,000,000" between the step indicators.
                Layout.preferredWidth: 164
                from: 1
                to: 1000000
                value: 1
                editable: true
                enabled: controls.canAdvance && !controls.controller.running
                Accessible.name: qsTr("Cycles per step")
            }
            Label { text: qsTr("cycles") }
            Button {
                objectName: "resetButton"
                Layout.leftMargin: 8
                text: qsTr("Reset")
                enabled: controls.ready && controls.controller.canReset
                onClicked: controls.controller.reset()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Pause and apply a 16-cycle reset pulse. Virtual time continues.")
            }
            ToolSeparator {}
            Label { text: qsTr("Pacing") }
            ComboBox {
                id: pacing
                objectName: "pacingMode"
                Layout.preferredWidth: 176
                model: [qsTr("Turbo"), qsTr("1× real-time target")]
                currentIndex: controls.ready && controls.controller.realtime ? 1 : 0
                enabled: controls.ready
                Accessible.name: qsTr("Simulation pacing")
                onActivated: controls.controller.realtime = currentIndex === 1
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Turbo runs as fast as possible. The real-time target does not guarantee 1× speed.")
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            objectName: "simulationError"
            Layout.fillWidth: true
            visible: controls.ready && controls.controller.errorString.length > 0
            text: controls.ready ? controls.controller.errorString : ""
            color: "#f0b190"
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            ToolTip.visible: errorHover.hovered && truncated
            ToolTip.text: text
            HoverHandler { id: errorHover }
        }
    }
}
