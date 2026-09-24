#pragma once

#include "qt/VgaFrameModel.h"

#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace vb::qt {

// Draws the latest completed VGA frame as a scene-graph texture. The texture
// is uploaded once per new frame, never per refresh or presented frame, and
// sampled with nearest filtering. The quad snaps to device pixels wherever
// layout, scrolling or a screen's pixel ratio places the item, so at an
// integer scale every displayed pixel is an exact framebuffer pixel. Scaling
// belongs to QML; decoding and timing stay in BoardModel.
class VgaDisplay : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(vb::qt::VgaFrameModel* frame READ frame WRITE setFrame NOTIFY frameModelChanged FINAL)

public:
    explicit VgaDisplay(QQuickItem* parent = nullptr);

    VgaFrameModel* frame() const { return frame_; }
    void setFrame(VgaFrameModel* frame);

signals:
    void frameModelChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void watchWindow(QQuickWindow* window);
    void checkAlignment();

    QPointer<VgaFrameModel> frame_;
    QMetaObject::Connection frameConnection_;
    QMetaObject::Connection destroyedConnection_;
    QMetaObject::Connection windowConnection_;
    // Written during synchronization (GUI thread blocked), read on the GUI thread.
    quint64 uploadedSerial_ = 0;  // serials start at 1; 0 = nothing uploaded
    bool drawn_ = false;          // an image node is in the scene
    QPointF alignedOrigin_;       // scene origin and pixel ratio of the last snap
    qreal alignedRatio_ = 0;
};

} // namespace vb::qt
