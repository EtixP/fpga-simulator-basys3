import QtQuick
import QtQuick.Controls

Button {
    id: control
    required property string resource
    required property bool available
    required property bool active
    required property var writeInput

    // Retain the accepted gesture's writer, even if the parent changes its
    // binding. A control must never release a pre-existing external assertion.
    property var pressWriter: null

    function releaseInput() {
        const writer = pressWriter
        pressWriter = null
        if (typeof writer === "function")
            writer(false)
    }

    function pressInput() {
        if (pressWriter !== null || active || !enabled || !visible || !Window.active)
            return
        const writer = writeInput
        const hadFocus = activeFocus
        if (typeof writer !== "function" || !writer(true))
            return
        pressWriter = writer
        // A writer may synchronously replace the target or hide its view.
        if (writeInput !== writer || !enabled || !visible || !Window.active
            || (hadFocus && !activeFocus))
            releaseInput()
    }

    implicitWidth: 55
    implicitHeight: 55
    enabled: available
    checkable: false
    autoRepeat: false
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: 0
    text: resource
    down: active

    onPressed: pressInput()
    onReleased: releaseInput()
    onCanceled: releaseInput()
    onActiveChanged: {
        // An external release ends this gesture's ownership. A later external
        // assertion must survive the old mouse/key release or view teardown.
        if (!active)
            pressWriter = null
    }
    onActiveFocusChanged: {
        if (!activeFocus)
            releaseInput()
    }
    onVisibleChanged: {
        if (!visible)
            releaseInput()
    }
    onEnabledChanged: {
        if (!enabled)
            releaseInput()
    }
    onWriteInputChanged: releaseInput()
    Window.onActiveChanged: {
        if (!Window.active)
            releaseInput()
    }
    Window.onVisibilityChanged: {
        if (Window.visibility === Window.Hidden || Window.visibility === Window.Minimized)
            releaseInput()
    }
    Window.onWindowChanged: releaseInput()
    Component.onDestruction: releaseInput()

    // Native Button handling supplies Space activation and accessibility.
    // Ignore OS repeat events so a canceled hold cannot reassert the input.
    Keys.onPressed: event => {
        if (event.isAutoRepeat)
            event.accepted = true
    }
    Keys.onReleased: event => {
        if (event.isAutoRepeat)
            event.accepted = true
    }

    Accessible.name: resource
    Accessible.description: !available ? qsTr("Not connected")
        : qsTr("Momentary button. Hold Space or the mouse button to press.")
    BoardToolTip {
        target: control
        showing: control.hovered
        text: !control.available ? qsTr("%1 is not connected").arg(control.resource)
            : qsTr("Hold %1 to press").arg(control.resource)
    }

    // A push button on the rendered board: while held, its sprite (a render of
    // the plunger pressed in) covers the board image, and a ring shows the press,
    // which a real button barely does from above. Unbound buttons are veiled.
    property var spriteRect: [0, 0, width, height]
    property url spriteSource
    background: Item {
        Image {
            objectName: "sprite"
            x: control.spriteRect[0]
            y: control.spriteRect[1]
            width: control.spriteRect[2]
            height: control.spriteRect[3]
            source: control.spriteSource
            visible: control.active
            smooth: true
            mipmap: true
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.52
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 3
            border.color: "#7fe0d0"
            opacity: control.active ? 0.9 : 0
        }
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: "#0a1a33"
            opacity: control.available ? 0 : 0.6
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: 6
            color: "transparent"
            border.width: 3
            border.color: "#9fe8dc"
            opacity: control.hovered && control.available && !control.visualFocus ? 0.75 : 0
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            radius: 8
            color: "transparent"
            border.width: 4
            border.color: "#7fe0d0"
            visible: control.visualFocus
        }
    }
    contentItem: Item {}
}
