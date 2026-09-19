#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <span>
#include <vector>

namespace vb::qt {

class BoardAdapter;

// A cached view of a fixed group of board resources. Only BoardAdapter writes it.
class BoardIoModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by BoardAdapter")

public:
    struct Row {
        QString resource;
        bool available = false;
        bool active = false;
    };

    enum Role {
        ResourceRole = Qt::UserRole + 1,
        AvailableRole,
        ActiveRole,
    };
    Q_ENUM(Role)

    explicit BoardIoModel(std::vector<Row> rows, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    friend class BoardAdapter;

    QList<int> stageStates(std::span<const bool> states);
    void publishChanges(const QList<int>& rows);

    std::vector<Row> rows_;
};

} // namespace vb::qt
