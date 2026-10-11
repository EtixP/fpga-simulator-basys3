pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VirtualBasys.Board
import "BoardLayout.js" as BoardLayout

Control {
    id: view
    required property BoardAdapter board
    property SimulationController controller: null
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
        // The board is scaled: map the whole item, not just its corner.
        const position = item.mapToItem(viewport.contentItem, 0, 0, item.width, item.height)
        let left = viewport.contentX
        let top = viewport.contentY
        if (position.x < left)
            left = position.x - 8
        else if (position.x + position.width > left + viewport.width)
            left = position.x + position.width - viewport.width + 8
        if (position.y < top)
            top = position.y - 8
        else if (position.y + position.height > top + viewport.height)
            top = position.y + position.height - viewport.height + 8
        viewport.contentX = Math.max(0, Math.min(left, viewport.contentWidth - viewport.width))
        viewport.contentY = Math.max(0, Math.min(top, viewport.contentHeight - viewport.height))
    }

    Connections {
        target: view.Window.window
        function onActiveFocusItemChanged() { view.revealFocusedControl() }
    }

    // The rendered board scales to fit the pane (its width only below the VGA
    // monitor), up to 10 px per mm and not below minimumScale, so its
    // controls keep a usable size (a switch stays about 24 x 48 px); smaller
    // panes scroll instead. At 0.42 the whole board fits the default window.
    // Model rows always retain their board indices.
    readonly property real minimumScale: 0.42
    readonly property real boardScale: {
        const widthFit = (viewport.width - 32) / BoardLayout.width
        const heightFit = (viewport.height - 32 - header.implicitHeight - contents.spacing)
            / BoardLayout.height
        const fit = vgaLoader.active ? widthFit : Math.min(widthFit, heightFit)
        return Math.max(minimumScale, Math.min(1, fit))
    }
    contentItem: Flickable {
        id: viewport
        objectName: "boardFlickable"
        clip: true
        // Whole-pixel scrolling keeps the VGA monitor's pixels crisp.
        pixelAligned: true
        contentWidth: Math.max(width, BoardLayout.width * view.boardScale + 32)
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

            // Designs that drive all VGA pins get a monitor first, above the board.
            Loader {
                id: vgaLoader
                objectName: "vgaMonitorLoader"
                Layout.fillWidth: true
                active: view.board !== null && view.board.vga.available
                visible: active
                sourceComponent: VgaMonitor {
                    objectName: "vgaMonitor"
                    frame: view.board.vga
                    controller: view.controller
                }
            }

            ColumnLayout {
                id: header
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

            // The rendered board (board/board.png, made by tools/board_render.py
            // from a 3D model) with each live part placed where BoardLayout.js
            // says, in board units of 0.1 mm. A part in its other state (switch
            // on, LED lit, button pressed, segment lit) shows a sprite rendered
            // in that state over the board image.
            Item {
                id: boardFrame
                objectName: "boardFrame"
                Layout.fillWidth: true
                implicitHeight: BoardLayout.height * view.boardScale

                Item {
                    id: boardItem
                    // Centred when the pane is wider than the scaled board
                    x: Math.max(0, (boardFrame.width - BoardLayout.width * view.boardScale) / 2)
                    width: BoardLayout.width
                    height: BoardLayout.height
                    scale: view.boardScale
                    transformOrigin: Item.TopLeft

                    Image {
                        objectName: "boardImage"
                        anchors.fill: parent
                        source: "board/board.png"
                        smooth: true
                        mipmap: true
                    }
                    // The DONE LED lights once the FPGA is configured, that is,
                    // while a design is loaded.
                    Image {
                        objectName: "doneLed"
                        x: BoardLayout.done.sprite[0]
                        y: BoardLayout.done.sprite[1]
                        width: BoardLayout.done.sprite[2]
                        height: BoardLayout.done.sprite[3]
                        source: "board/sprites/done.png"
                        visible: view.connected
                        smooth: true
                        mipmap: true
                    }

                    Repeater {
                        model: view.board ? view.board.digits : null
                        SevenSegmentDigit {
                            objectName: "digit" + digit
                            available: view.board !== null && view.board.hasDisplay
                            cell: BoardLayout.digits[digit]
                            x: BoardLayout.digits[digit].rect[0]
                            y: BoardLayout.digits[digit].rect[1]
                            width: BoardLayout.digits[digit].rect[2]
                            height: BoardLayout.digits[digit].rect[3]
                        }
                    }
                    // A connected design's unbound display is veiled like any
                    // other unbound part. Without a design no part is placed.
                    Rectangle {
                        objectName: "displayVeil"
                        x: BoardLayout.display.rect[0]
                        y: BoardLayout.display.rect[1]
                        width: BoardLayout.display.rect[2]
                        height: BoardLayout.display.rect[3]
                        radius: 4
                        color: "#0a1a33"
                        opacity: 0.6
                        visible: view.connected && !view.board.hasDisplay
                    }
                    Repeater {
                        model: view.board ? view.board.buttons : null
                        BoardButton {
                            required property int index
                            objectName: "button" + index
                            x: BoardLayout.buttons[index].rect[0]
                            y: BoardLayout.buttons[index].rect[1]
                            width: BoardLayout.buttons[index].rect[2]
                            height: BoardLayout.buttons[index].rect[3]
                            spriteRect: BoardLayout.buttons[index].sprite
                            spriteSource: "board/sprites/button-%1.png".arg(index)
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
                    Repeater {
                        model: view.board ? view.board.leds : null
                        BoardLed {
                            required property int index
                            objectName: "led" + index
                            x: BoardLayout.leds[index].rect[0]
                            y: BoardLayout.leds[index].rect[1]
                            width: BoardLayout.leds[index].rect[2]
                            height: BoardLayout.leds[index].rect[3]
                            spriteRect: BoardLayout.leds[index].sprite
                            spriteSource: "board/sprites/led-%1.png".arg(index)
                        }
                    }
                    Repeater {
                        model: view.board ? view.board.switches : null
                        BoardSwitch {
                            required property int index
                            objectName: "sw" + index
                            x: BoardLayout.switches[index].rect[0]
                            y: BoardLayout.switches[index].rect[1]
                            width: BoardLayout.switches[index].rect[2]
                            height: BoardLayout.switches[index].rect[3]
                            spriteRect: BoardLayout.switches[index].sprite
                            spriteSource: "board/sprites/switch-%1.png".arg(index)
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
