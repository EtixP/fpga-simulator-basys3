#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <vector>

namespace vb::qt {

class BoardAdapter;

// Read-only snapshot of design signals: every supported top-level port, in
// the engine's name order, followed by user watches in the order added. All
// values come from one staging pass at one virtual cycle; a refresh reports
// only the rows whose value changed, grouped into contiguous ranges. Only
// BoardAdapter writes it, and QML never sees a signal handle.
class SignalInspectorModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by BoardAdapter")
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int watchCount READ watchCount NOTIFY countChanged FINAL)
    Q_PROPERTY(int maximumWatches READ maximumWatches CONSTANT FINAL)
    Q_PROPERTY(QString snapshotCycleText READ snapshotCycleText NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(QString snapshotTimeText READ snapshotTimeText NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(QString watchError READ watchError NOTIFY watchErrorChanged FINAL)

public:
    static constexpr int MaximumWatches = 32;

    enum Kind { Input, Output, Clock, Internal };
    Q_ENUM(Kind)

    enum Role {
        NameRole = Qt::UserRole + 1,
        KindRole,
        WidthRole,
        RangeRole,      // declared HDL range, e.g. "[15:0]"; empty for scalars
        ValueRole,      // "0"/"1" for one bit, otherwise zero-padded hex
        DecimalRole,    // unsigned decimal of the packed value
        BindingRole,    // board resources on this signal, e.g. "LED0–LED15"
        ChangedRole,    // differs from the previous snapshot
        WatchRole,      // user-added hierarchical watch
    };
    Q_ENUM(Role)

    struct Row {
        QString name;
        Kind kind = Output;
        int width = 1;
        QString range;
        QString binding;
        bool watch = false;
        uint64_t value = 0;
        bool changed = false;
    };

    explicit SignalInspectorModel(bool available, QObject* parent = nullptr);

    bool available() const { return available_; }
    int count() const { return static_cast<int>(rows_.size()); }
    int watchCount() const;
    int maximumWatches() const { return MaximumWatches; }
    QString snapshotCycleText() const;
    QString snapshotTimeText() const;
    QString watchError() const { return watchError_; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    static QString valueText(uint64_t value, int width);

signals:
    void countChanged();
    void snapshotChanged();
    void watchErrorChanged();

private:
    friend class BoardAdapter;

    struct Changes {
        std::vector<std::pair<int, int>> ranges;  // inclusive changed row ranges
        bool snapshotMoved = false;
    };

    void resetRows(std::vector<Row> rows);
    // Values in row order, all read at `cycle`.
    Changes stageValues(const std::vector<uint64_t>& values, uint64_t cycle);
    void publishValues(const Changes& changes);
    void appendWatch(Row row);
    void removeRow(int row);
    void setWatchError(const QString& message);

    const bool available_;
    std::vector<Row> rows_;
    bool haveSnapshot_ = false;
    uint64_t snapshotCycle_ = 0;
    QString watchError_;
};

// Case-insensitive name filter over the inspector; filtering stays in C++.
class SignalFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by BoardAdapter")
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged FINAL)

public:
    explicit SignalFilterModel(SignalInspectorModel* source, QObject* parent = nullptr);
    QString filterText() const { return filterText_; }
    void setFilterText(const QString& text);

signals:
    void filterTextChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString filterText_;
};

} // namespace vb::qt
