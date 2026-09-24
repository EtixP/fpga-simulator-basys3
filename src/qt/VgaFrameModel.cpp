#include "qt/VgaFrameModel.h"

#include "qt/VirtualTime.h"

#include <utility>

namespace vb::qt {

VgaFrameModel::VgaFrameModel(bool available, QObject* parent)
    : QObject(parent), available_(available) {}

QString VgaFrameModel::frameCycleText() const {
    return hasFrame() ? QString::number(frameCycle_) : QString();
}

QString VgaFrameModel::frameTimeText() const {
    return hasFrame() ? formatVirtualTime(frameCycle_) : QString();
}

bool VgaFrameModel::stage(const State& state, QImage image) {
    const bool newFrame = !image.isNull();
    const bool changed = newFrame || completedFrames_ != state.completedFrames
        || frameCycle_ != state.frameCycle || cyclesPerPixel_ != state.cyclesPerPixel
        || ok_ != state.ok || status_ != state.status;
    completedFrames_ = state.completedFrames;
    frameCycle_ = state.frameCycle;
    cyclesPerPixel_ = state.cyclesPerPixel;
    ok_ = state.ok;
    status_ = state.status;
    if (newFrame) {
        image_ = std::move(image);
        ++imageSerial_;
    }
    return changed;
}

void VgaFrameModel::publish(bool changed) {
    if (changed) emit frameChanged();
}

} // namespace vb::qt
