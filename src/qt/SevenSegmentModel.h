#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <array>

namespace vb::qt {

class BoardAdapter;

// Display persistence and decoding remain in BoardModel; this stores its view.
class SevenSegmentModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by BoardAdapter")

public:
    static constexpr int DigitCount = 4;

    struct State {
        int segments = 0;
        bool decimalPoint = false;
        QString character = QStringLiteral(" ");
    };

    enum Role {
        DigitRole = Qt::UserRole + 1,
        SegmentsRole,
        DecimalPointRole,
        CharacterRole,
    };
    Q_ENUM(Role)

    using Changes = std::array<QList<int>, DigitCount>;

    explicit SevenSegmentModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    friend class BoardAdapter;

    Changes stageStates(const std::array<State, DigitCount>& states);
    void publishChanges(const Changes& changes);

    std::array<State, DigitCount> states_;
};

} // namespace vb::qt
