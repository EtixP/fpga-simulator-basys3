#include "qt/VgaDisplay.h"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>

#include <cmath>

namespace vb::qt {

VgaDisplay::VgaDisplay(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setImplicitSize(VgaFrameModel::Width, VgaFrameModel::Height);
    // A parent already in a window delivers ItemSceneChange during the base
    // constructor, before this class's itemChange can observe it.
    watchWindow(window());
}

void VgaDisplay::setFrame(VgaFrameModel* frame) {
    if (frame_ == frame) return;
    disconnect(frameConnection_);
    disconnect(destroyedConnection_);
    frame_ = frame;
    // Another model may reuse serials; always upload its current image.
    uploadedSerial_ = 0;
    if (frame_) {
        frameConnection_ = connect(frame_, &VgaFrameModel::frameChanged, this,
                                   [this] { update(); });
        // Clear the image when the model goes away rather than keep it shown.
        destroyedConnection_ = connect(frame_, &QObject::destroyed, this, [this] { update(); });
    }
    emit frameModelChanged();
    update();
}

// Runs with the GUI thread blocked, so reading the model is safe here.
QSGNode* VgaDisplay::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGImageNode*>(old);
    const bool drawable = frame_ && !frame_->image().isNull() && width() > 0 && height() > 0;
    if (!drawable) {
        delete node;  // owns its texture
        uploadedSerial_ = 0;
        drawn_ = false;
        return nullptr;
    }
    if (!node) {
        node = window()->createImageNode();
        node->setFiltering(QSGTexture::Nearest);
        node->setMipmapFiltering(QSGTexture::None);
        uploadedSerial_ = 0;
    }
    const QImage& image = frame_->image();
    if (uploadedSerial_ != frame_->imageSerial()) {
        // Replace the texture explicitly rather than relying on how
        // setTexture treats an owned predecessor.
        QSGTexture* previous = node->texture();
        node->setOwnsTexture(false);
        node->setTexture(window()->createTextureFromImage(image, QQuickWindow::TextureIsOpaque));
        delete previous;
        node->setOwnsTexture(true);
        uploadedSerial_ = frame_->imageSerial();
    }
    // Offset the quad so its corner lands on a device pixel: a half-pixel
    // offset would put every nearest sample on a texel boundary.
    const QPointF origin = mapToScene(QPointF());
    const qreal ratio = window()->effectiveDevicePixelRatio();
    const QPointF snapped(std::round(origin.x() * ratio) / ratio,
                          std::round(origin.y() * ratio) / ratio);
    node->setRect(QRectF(snapped - origin, size()));
    node->setSourceRect(QRectF(0, 0, image.width(), image.height()));
    drawn_ = true;
    alignedOrigin_ = origin;
    alignedRatio_ = ratio;
    return node;
}

void VgaDisplay::itemChange(ItemChange change, const ItemChangeData& value) {
    if (change == ItemSceneChange)
        watchWindow(value.window);
    else if (change == ItemDevicePixelRatioHasChanged)
        update();  // a snap for one pixel ratio is wrong for another
    QQuickItem::itemChange(change, value);
}

void VgaDisplay::watchWindow(QQuickWindow* window) {
    disconnect(windowConnection_);
    if (window) {
        // Ancestors (scrolling, layouts) move the item without repainting it;
        // re-snap in the same frame, before synchronization.
        windowConnection_ = connect(window, &QQuickWindow::afterAnimating,
                                    this, &VgaDisplay::checkAlignment);
    }
}

void VgaDisplay::checkAlignment() {
    if (drawn_ && window()
        && (mapToScene(QPointF()) != alignedOrigin_
            || window()->effectiveDevicePixelRatio() != alignedRatio_))
        update();
}

} // namespace vb::qt
