pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VirtualBasys.Board

// Presentation of the board's VGA monitor. BoardModel reconstructs, times and
// validates every frame; this card only shows the latest completed one. The
// screen comes first so a whole frame fits the default board viewport.
Rectangle {
    id: monitor
    required property VgaFrameModel frame
    property SimulationController controller: null
    // Integer scale: every framebuffer pixel covers an exact block of pixels.
    readonly property int pixelScale: Math.max(1, Math.floor((width - 28) / frame.width))
    // Master cycles per displayed frame: 800 x 525 pixel periods.
    readonly property real cyclesPerFrame: 420000 * Math.max(1, frame.cyclesPerPixel)
    readonly property bool measured: controller !== null && controller.running
                                     && controller.speedAvailable && frame.cyclesPerPixel > 0
    implicitHeight: content.implicitHeight + 20
    radius: 10
    color: palette.base
    border.color: palette.mid

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        // A tight top margin keeps a whole 1x frame in view at the default
        // window size, even when macOS clamps the window to a laptop screen.
        anchors.topMargin: 6
        spacing: 8

        Item {
            Layout.fillWidth: true
            implicitHeight: screen.height
            Rectangle {
                id: screen
                objectName: "vgaScreen"
                x: Math.round((parent.width - width) / 2)
                width: monitor.frame.width * monitor.pixelScale
                height: monitor.frame.height * monitor.pixelScale
                color: "black"

                VgaDisplay {
                    objectName: "vgaDisplay"
                    anchors.fill: parent
                    frame: monitor.frame
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 440)
                    visible: !monitor.frame.hasFrame
                    spacing: 6
                    Label {
                        objectName: "vgaWaitingTitle"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No complete frame yet")
                        color: "#dce7f0"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Label {
                        objectName: "vgaWaitingDetail"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("A frame is shown once two vertical syncs delimit it. Run or Step to advance virtual time.")
                        color: "#8d9fb1"
                    }
                }

                // The monitor's diagnosis of the latest frame, over that frame.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: statusText.implicitHeight + 12
                    visible: monitor.frame.hasFrame && !monitor.frame.ok
                    color: "#d9101720"
                    Label {
                        id: statusText
                        objectName: "vgaStatus"
                        anchors.fill: parent
                        anchors.margins: 6
                        text: qsTr("Signal problem in the latest frame: %1").arg(monitor.frame.status)
                        color: "#f0b190"
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("VGA MONITOR")
                font.pixelSize: 10
                font.letterSpacing: 1
                color: monitor.palette.placeholderText
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("%1 × %2 · shown at %3×").arg(monitor.frame.width)
                          .arg(monitor.frame.height).arg(monitor.pixelScale)
                font.pixelSize: 11
                color: monitor.palette.placeholderText
            }
        }

        Label {
            objectName: "vgaFrameInfo"
            Layout.fillWidth: true
            text: monitor.frame.hasFrame
                ? qsTr("Frame %1 · completed at cycle %2 (%3) · %4 cycles/pixel")
                    .arg(monitor.frame.completedFrames - 1).arg(monitor.frame.frameCycleText)
                    .arg(monitor.frame.frameTimeText).arg(monitor.frame.cyclesPerPixel)
                : qsTr("Waiting for the first complete frame")
            color: monitor.palette.text
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Label {
            objectName: "vgaFrameRate"
            Layout.fillWidth: true
            text: monitor.measured
                ? qsTr("≈ %1 simulated frames/s at the measured speed; real time would be %2.")
                    .arg((monitor.controller.cyclesPerSecond / monitor.cyclesPerFrame).toFixed(1))
                    .arg((100000000 / monitor.cyclesPerFrame).toFixed(1))
                : qsTr("Every completed frame is exact; frames advance with virtual time, not wall time.")
            color: monitor.palette.placeholderText
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
}
