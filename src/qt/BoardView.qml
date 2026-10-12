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
    // Where the zoom controls go: the pane's title bar, outside the scrolled
    // board and the VGA monitor. Without one, they float over the view.
    property Item zoomBarParent: null

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
    readonly property real fitScale: {
        const widthFit = (viewport.width - 32) / BoardLayout.width
        const heightFit = (viewport.height - 32 - header.implicitHeight - contents.spacing)
            / BoardLayout.height
        const fit = vgaLoader.active ? widthFit : Math.min(widthFit, heightFit)
        return Math.max(minimumScale, Math.min(1, fit))
    }
    // Zoom magnifies the fitted board, up to the render's own resolution
    // (BoardLayout.pixelsPerMm; a board unit is 0.1 mm). The pane scrolls, and
    // the point under the pointer or pinch stays put.
    property real zoom: 1
    readonly property real maximumScale: BoardLayout.pixelsPerMm / 10
    readonly property real maximumZoom: Math.max(1, maximumScale / fitScale)
    // A wider pane lowers the limit; the zoom in effect never exceeds it.
    readonly property real effectiveZoom: Math.min(zoom, maximumZoom)
    readonly property real boardScale: fitScale * effectiveZoom
    readonly property real zoomStep: 1.25
    function zoomKeys(key) {
        return (Qt.platform.os === "osx" ? "\u2318" : "Ctrl+") + key
    }

    // Sets the zoom, keeping the board point at (x, y) in the pane's visible
    // area where it is; without a point, the middle of the visible area. The
    // board's new place follows from the scale alone, before the layout
    // catches up. A board narrower than the pane is centred and cannot scroll
    // sideways, so it grows about its centre: only its height follows the point.
    function setZoom(value, x, y) {
        const next = Math.max(1, Math.min(value, maximumZoom))
        if (next === effectiveZoom)
            return
        const atX = x === undefined ? viewport.width / 2 : x
        const atY = y === undefined ? viewport.height / 2 : y
        const anchor = boardItem.mapFromItem(viewport, atX, atY)
        zoom = next
        const scale = boardScale
        const left = contents.x
        const top = contents.y + boardFrame.y
        viewport.contentX = Math.max(0, Math.min(left + anchor.x * scale - atX,
                                                 viewport.contentWidth - viewport.width))
        viewport.contentY = Math.max(0, Math.min(top + anchor.y * scale - atY,
                                                 viewport.contentHeight - viewport.height))
    }

    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        enabled: view.visible
        onActivated: view.setZoom(view.effectiveZoom * view.zoomStep)
    }
    Shortcut {
        sequences: [StandardKey.ZoomOut]
        enabled: view.visible
        onActivated: view.setZoom(view.effectiveZoom / view.zoomStep)
    }
    Shortcut {
        sequence: "Ctrl+0"
        enabled: view.visible
        onActivated: view.setZoom(1)
    }
    // The zoom controls sit outside the scrolled content, in the pane's title
    // bar, so they stay in reach however far the board is zoomed and never
    // cover the board or the VGA monitor.
    Pane {
        id: zoomBar
        objectName: "zoomBar"
        parent: view.zoomBarParent !== null ? view.zoomBarParent : view
        visible: view.visible
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: view.zoomBarParent !== null ? parent.verticalCenter : undefined
        anchors.top: view.zoomBarParent !== null ? undefined : parent.top
        anchors.topMargin: 8
        z: 2
        padding: 0
        background: Rectangle {
            radius: 6
            color: view.palette.window
            opacity: view.zoomBarParent !== null ? 0 : 0.94
            border.color: view.palette.mid
            border.width: view.zoomBarParent !== null ? 0 : 1
        }
        contentItem: RowLayout {
            spacing: 2
            ToolButton {
                objectName: "zoomOut"
                implicitHeight: 30
                topPadding: 2
                bottomPadding: 2
                text: "\u2212"
                enabled: view.effectiveZoom > 1
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Zoom out")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Zoom out (%1)").arg(view.zoomKeys("-"))
                onClicked: view.setZoom(view.effectiveZoom / view.zoomStep)
            }
            ToolButton {
                objectName: "zoomFit"
                implicitHeight: 30
                topPadding: 2
                bottomPadding: 2
                text: qsTr("Fit")
                enabled: view.effectiveZoom > 1
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Fit the board to the pane")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fit the board to the pane (%1)").arg(view.zoomKeys("0"))
                onClicked: view.setZoom(1)
            }
            ToolButton {
                objectName: "zoomIn"
                implicitHeight: 30
                topPadding: 2
                bottomPadding: 2
                text: "+"
                enabled: view.boardScale < view.maximumScale - 0.001
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Zoom in")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Zoom in (%1)").arg(view.zoomKeys("+"))
                onClicked: view.setZoom(view.effectiveZoom * view.zoomStep)
            }
        }
    }

    contentItem: Flickable {
        id: viewport
        objectName: "boardFlickable"
        clip: true
        // Whole-pixel scrolling keeps the VGA monitor's pixels crisp.
        pixelAligned: true
        contentWidth: Math.max(width, BoardLayout.width * view.boardScale + 32)
        // The board is the last item: its height follows the zoom at once,
        // where the layout's implicit height would lag until the next polish.
        contentHeight: boardFrame.y + boardFrame.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds
        onWidthChanged: Qt.callLater(view.revealFocusedControl)
        onHeightChanged: Qt.callLater(view.revealFocusedControl)
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        // Command-scroll (Ctrl elsewhere) and trackpad pinch zoom around the pointer.
        WheelHandler {
            objectName: "boardZoomWheel"
            acceptedModifiers: Qt.ControlModifier
            target: null
            onWheel: function(event) {
                const at = viewport.mapFromItem(viewport.contentItem, point.position.x, point.position.y)
                view.setZoom(view.effectiveZoom * Math.pow(view.zoomStep, event.angleDelta.y / 120), at.x, at.y)
            }
        }
        PinchHandler {
            target: null
            property real startZoom: 1
            onActiveChanged: if (active) startZoom = view.effectiveZoom
            onActiveScaleChanged: {
                const at = viewport.mapFromItem(viewport.contentItem, centroid.position.x, centroid.position.y)
                view.setZoom(startZoom * activeScale, at.x, at.y)
            }
        }

        ColumnLayout {
            id: contents
            x: 16
            y: 16
            width: viewport.contentWidth - 32
            spacing: 16

            // Designs that drive all VGA pins get a monitor first, above the board.
            // The monitor and the header keep the visible pane's width: the
            // board's zoom must not magnify the monitor or re-wrap the header,
            // which would move the board out from under the pointer.
            Loader {
                id: vgaLoader
                objectName: "vgaMonitorLoader"
                Layout.preferredWidth: viewport.width - 32
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
                // A nested layout fills its parent's width unless told not to.
                Layout.fillWidth: false
                Layout.preferredWidth: viewport.width - 32
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
                        ? qsTr("Only bound resources are enabled. Click a switch; hold a button to press it. Zoom with %1-scroll or a pinch.")
                        .arg(Qt.platform.os === "osx" ? "\u2318" : "Ctrl")
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
