#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <deque>
#include <span>

namespace vb::qt {

class BoardAdapter;

// Bounded, read-only UART scrollback. BoardModel decodes and schedules every
// byte; this model only groups the board's bytes into lines and caches their
// presentation. Only BoardAdapter writes it. Clearing empties this scrollback,
// never the board's decoded bytes, queued input or structured log.
//
// TX/RX follow the design's (and the structured log's) perspective: TX is
// design output on RsTx (A18), RX is host input queued to RsRx (B18).
class UartConsoleModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by BoardAdapter")
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(bool txBound READ txBound CONSTANT FINAL)
    Q_PROPERTY(bool rxBound READ rxBound CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int maximumLines READ maximumLines CONSTANT FINAL)
    Q_PROPERTY(int maximumPendingRxBytes READ maximumPendingRxBytes CONSTANT FINAL)
    Q_PROPERTY(qint64 trimmedLines READ trimmedLines NOTIFY countChanged FINAL)
    Q_PROPERTY(qint64 txBytes READ txBytes NOTIFY trafficChanged FINAL)
    Q_PROPERTY(qint64 rxBytes READ rxBytes NOTIFY trafficChanged FINAL)
    Q_PROPERTY(qint64 pendingRxBytes READ pendingRxBytes NOTIFY trafficChanged FINAL)
    Q_PROPERTY(qint64 framingErrors READ framingErrors NOTIFY trafficChanged FINAL)
    Q_PROPERTY(QString sendError READ sendError NOTIFY sendErrorChanged FINAL)

public:
    // 1000 lines of at most 128 bytes bound the scrollback's memory. Longer
    // lines continue in a new row stamped with its own first byte.
    static constexpr int MaximumLines = 1000;
    static constexpr int MaximumLineBytes = 128;
    // Host input waiting for, or occupying, the serial line: ~4.3 virtual
    // seconds of back-to-back 9600-baud frames.
    static constexpr int MaximumPendingRxBytes = 4096;

    enum Kind { Tx, Rx, Notice };
    Q_ENUM(Kind)

    enum Role {
        KindRole = Qt::UserRole + 1,
        TextRole,
        HexRole,
        ByteCountRole,
        CycleRole,
        LastCycleRole,
        TimeRole,
    };
    Q_ENUM(Role)

    struct Byte {
        uint8_t value = 0;
        uint64_t cycle = 0;
    };

    UartConsoleModel(bool txBound, bool rxBound, QObject* parent = nullptr);

    bool available() const { return txBound_ || rxBound_; }
    bool txBound() const { return txBound_; }
    bool rxBound() const { return rxBound_; }
    int count() const { return static_cast<int>(lines_.size()); }
    int maximumLines() const { return MaximumLines; }
    int maximumPendingRxBytes() const { return MaximumPendingRxBytes; }
    qint64 trimmedLines() const { return trimmedLines_; }
    qint64 txBytes() const { return txBytes_; }
    qint64 rxBytes() const { return rxBytes_; }
    qint64 pendingRxBytes() const { return pendingRxBytes_; }
    qint64 framingErrors() const { return framingErrors_; }
    QString sendError() const { return sendError_; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Printable ASCII is literal; other bytes use \t, \r or \xHH escapes.
    static QString displayText(const QByteArray& bytes);
    static QString hexText(const QByteArray& bytes);

signals:
    void countChanged();
    void trafficChanged();
    void sendErrorChanged();

private:
    friend class BoardAdapter;

    struct Line {
        Kind kind = Notice;
        QByteArray bytes;          // raw payload, including a terminating '\n'
        bool terminated = false;   // ended by '\n'; later bytes start a new row
        uint64_t firstCycle = 0;
        uint64_t lastCycle = 0;
        QString text;
        QString hex;
    };

    // Counters are staged with the other board state; rows publish last.
    bool stageCounters(qint64 txBytes, qint64 rxBytes, qint64 pendingRxBytes,
                       qint64 framingErrors);
    void publishCounters(bool changed);
    // Tx or Rx bytes, in board order. A different kind, a notice or endLine()
    // ends the open line; '\n' ends it after the newline byte.
    void appendTraffic(Kind kind, std::span<const Byte> bytes);
    void endLine() { openLine_ = false; }
    void appendNotice(const QString& text, uint64_t cycle);
    void setSendError(const QString& message);
    void clearLines();

    void insertLines(std::deque<Line> added);

    const bool txBound_;
    const bool rxBound_;
    std::deque<Line> lines_;
    bool openLine_ = false;  // the last row may still receive bytes
    qint64 trimmedLines_ = 0;
    qint64 txBytes_ = 0;
    qint64 rxBytes_ = 0;
    qint64 pendingRxBytes_ = 0;
    qint64 framingErrors_ = 0;
    QString sendError_;
};

} // namespace vb::qt
