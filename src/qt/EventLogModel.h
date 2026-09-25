#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace vb::qt {

class BoardAdapter;

// Bounded view of BoardModel's structured log while the Qt log view records.
// Rows keep the log's own emission order and cycle stamps; the adapter drains
// new lines on each refresh, so the rows never depend on refresh cadence.
// Only BoardAdapter writes it.
class EventLogModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by BoardAdapter")
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int maximumEvents READ maximumEvents CONSTANT FINAL)
    Q_PROPERTY(qint64 trimmedEvents READ trimmedEvents NOTIFY countChanged FINAL)
    Q_PROPERTY(qint64 recordedEvents READ recordedEvents NOTIFY countChanged FINAL)

public:
    // 10,000 rows bound the view; older rows are discarded first.
    static constexpr int MaximumEvents = 10'000;

    enum Kind { Display, Leds, Uart, Inputs, Other, Notice };
    Q_ENUM(Kind)

    enum Role {
        KindRole = Qt::UserRole + 1,
        CycleRole,
        TimeRole,
        TextRole,
    };
    Q_ENUM(Role)

    struct Event {
        Kind kind = Other;
        uint64_t cycle = 0;
        QString text;
    };

    explicit EventLogModel(bool available, QObject* parent = nullptr);

    bool available() const { return available_; }
    bool recording() const { return recording_; }
    int count() const { return static_cast<int>(events_.size()); }
    int maximumEvents() const { return MaximumEvents; }
    qint64 trimmedEvents() const { return trimmed_; }
    qint64 recordedEvents() const { return recorded_; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Parses one "[cycle N] body" structured-log line; false for header lines.
    static bool parseLine(const std::string& line, Event& event);
    static Kind kindOf(const QString& body);

signals:
    void recordingChanged();
    void countChanged();

private:
    friend class BoardAdapter;

    void setRecording(bool recording);
    void append(std::vector<Event> events);
    void clearEvents();

    const bool available_;
    bool recording_ = false;
    std::deque<Event> events_;
    qint64 trimmed_ = 0;
    qint64 recorded_ = 0;
};

// Category toggles and a case-insensitive text filter, applied in C++.
// Notices (recording started/stopped) always pass the category filter.
class EventFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by BoardAdapter")
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged FINAL)
    Q_PROPERTY(bool showDisplay READ showDisplay WRITE setShowDisplay NOTIFY filterChanged FINAL)
    Q_PROPERTY(bool showLeds READ showLeds WRITE setShowLeds NOTIFY filterChanged FINAL)
    Q_PROPERTY(bool showUart READ showUart WRITE setShowUart NOTIFY filterChanged FINAL)
    Q_PROPERTY(bool showInputs READ showInputs WRITE setShowInputs NOTIFY filterChanged FINAL)

public:
    explicit EventFilterModel(EventLogModel* source, QObject* parent = nullptr);

    QString filterText() const { return filterText_; }
    bool showDisplay() const { return shown_[EventLogModel::Display]; }
    bool showLeds() const { return shown_[EventLogModel::Leds]; }
    bool showUart() const { return shown_[EventLogModel::Uart]; }
    bool showInputs() const { return shown_[EventLogModel::Inputs]; }
    void setFilterText(const QString& text);
    void setShowDisplay(bool shown) { setShown(EventLogModel::Display, shown); }
    void setShowLeds(bool shown) { setShown(EventLogModel::Leds, shown); }
    void setShowUart(bool shown) { setShown(EventLogModel::Uart, shown); }
    void setShowInputs(bool shown) { setShown(EventLogModel::Inputs, shown); }

signals:
    void filterChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    void setShown(EventLogModel::Kind kind, bool shown);
    void refilter();

    QString filterText_;
    bool shown_[EventLogModel::Notice + 1] = {true, true, true, true, true, true};
};

} // namespace vb::qt
