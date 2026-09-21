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

    implicitWidth: 54
    implicitHeight: 54
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
    ToolTip.visible: hovered
    ToolTip.text: !available ? qsTr("%1 is not connected").arg(resource)
        : qsTr("Hold %1 to press").arg(resource)

    background: Rectangle {
        radius: 9
        color: !control.available ? "#1a252e"
            : control.active ? "#286861" : control.hovered ? "#304653" : "#243641"
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? "#8ce4d7"
            : control.active ? "#64ccbc" : "#48616e"
        Rectangle {
            anchors.fill: parent
            anchors.margins: 5
            radius: 5
            color: "transparent"
            border.color: control.available && control.active ? "#5cb8a9" : "#3a4d59"
        }
    }
    contentItem: Label {
        text: control.resource
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.available ? "#e0ecef" : "#718492"
        font.pixelSize: 10
        font.weight: Font.DemiBold
    }
}
