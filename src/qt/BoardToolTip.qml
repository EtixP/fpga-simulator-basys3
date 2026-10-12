import QtQuick
import QtQuick.Controls

// A board part's tooltip. The board is drawn scaled but popups are not, so a
// tooltip placed in the part's own coordinates would sit on the part and take
// the click meant for it. This one keeps a gap of screen pixels between itself
// and the part, centred above it, or below it when the window has no room
// above; the placement is taken each time it opens.
ToolTip {
    id: tip
    required property Item target
    property bool showing: false
    readonly property real gap: 6

    // Screen pixels per unit of the target, through every scaled ancestor.
    function screenScale() {
        const origin = target.mapToItem(null, 0, 0)
        const unit = target.mapToItem(null, 1000, 0)
        return Math.max(0.01, (unit.x - origin.x) / 1000)
    }

    visible: showing
    x: { tip.visible; return (target.width - implicitWidth / screenScale()) / 2 }
    y: {
        tip.visible
        const scale = screenScale()
        const roomAbove = target.mapToItem(null, 0, 0).y - implicitHeight - gap - Math.max(0, margins)
        return roomAbove >= 0 ? -(implicitHeight + gap) / scale : target.height + gap / scale
    }
}
