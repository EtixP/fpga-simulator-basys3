#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>

namespace vb::qt {

class BoardAdapter;

// Read-only view of the board's VGA monitor. BoardModel reconstructs and
// validates every frame; this model keeps a deep copy of the latest completed
// framebuffer, taken only when a new frame completes, plus its exact stamp and
// the monitor's status. Only BoardAdapter writes it.
class VgaFrameModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by BoardAdapter")
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(int width READ width CONSTANT FINAL)
    Q_PROPERTY(int height READ height CONSTANT FINAL)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged FINAL)
    Q_PROPERTY(qint64 completedFrames READ completedFrames NOTIFY frameChanged FINAL)
    Q_PROPERTY(QString frameCycleText READ frameCycleText NOTIFY frameChanged FINAL)
    Q_PROPERTY(QString frameTimeText READ frameTimeText NOTIFY frameChanged FINAL)
    Q_PROPERTY(int cyclesPerPixel READ cyclesPerPixel NOTIFY frameChanged FINAL)
    Q_PROPERTY(bool ok READ ok NOTIFY frameChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY frameChanged FINAL)

public:
    static constexpr int Width = 640;
    static constexpr int Height = 480;

    explicit VgaFrameModel(bool available, QObject* parent = nullptr);

    bool available() const { return available_; }
    int width() const { return Width; }
    int height() const { return Height; }
    bool hasFrame() const { return completedFrames_ > 0; }
    qint64 completedFrames() const { return completedFrames_; }
    // The completion stamp of the latest frame (its closing Vsync fall).
    QString frameCycleText() const;
    QString frameTimeText() const;
    int cyclesPerPixel() const { return cyclesPerPixel_; }
    bool ok() const { return ok_; }
    QString status() const { return status_; }

    // C++ presentation seam for VgaDisplay: the latest completed frame as a
    // top-origin RGB888 Width x Height image (null before the first frame), and
    // a serial that changes whenever a new frame replaces it.
    const QImage& image() const { return image_; }
    quint64 imageSerial() const { return imageSerial_; }

signals:
    void frameChanged();

private:
    friend class BoardAdapter;

    struct State {
        qint64 completedFrames = 0;
        uint64_t frameCycle = 0;
        int cyclesPerPixel = 0;
        bool ok = true;
        QString status;
    };

    // Stages the state; `image` is non-null only when a new frame completed.
    bool stage(const State& state, QImage image);
    void publish(bool changed);

    const bool available_;
    qint64 completedFrames_ = 0;
    uint64_t frameCycle_ = 0;
    int cyclesPerPixel_ = 0;
    bool ok_ = true;
    QString status_;
    QImage image_;
    quint64 imageSerial_ = 0;
};

} // namespace vb::qt
