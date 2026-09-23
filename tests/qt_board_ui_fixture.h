#pragma once

#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "qt/BoardAdapter.h"
#include "qt/SimulationController.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

inline std::string boardUiReadFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) throw std::runtime_error(std::string("Cannot read ") + path);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

// Repeater delegates are visual children but are not necessarily QObject
// descendants of their visual parent. Follow the scene tree, including when
// looking for individual segment shapes inside a repeated digit.
inline QQuickItem* boardUiFindItem(QQuickItem* root, const QString& name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = boardUiFindItem(child, name)) return found;
    return nullptr;
}

// Tests own the composition root. Without an optional controller, simulated
// time advances exclusively through explicit C++ BoardModel::tick calls.
template <class VModel>
struct BoardUiFixture {
    BoardUiFixture(const char* top, const char* xdcPath)
        : engine(vb::makeVerilatorEngine<VModel>({.topModule = top})),
          board(*engine, vb::PinBinding::bind(vb::parseXdc(boardUiReadFile(xdcPath)), *engine)),
          adapter(&board) {
        board.setLogEnabled(true);
    }

    ~BoardUiFixture() { closeShell(); }

    bool loadShell(vb::qt::SimulationController* controller = nullptr) {
        closeShell();
        QQmlEngine::setObjectOwnership(&adapter, QQmlEngine::CppOwnership);
        qml = std::make_unique<QQmlApplicationEngine>();
        QObject::connect(qml.get(), &QQmlEngine::warnings, qml.get(),
                         [this](const QList<QQmlError>& errors) {
                             for (const auto& error : errors) warnings.append(error.toString());
                         });
        QVariantMap properties{{QStringLiteral("board"), QVariant::fromValue(&adapter)}};
        if (controller) {
            QQmlEngine::setObjectOwnership(controller, QQmlEngine::CppOwnership);
            properties.insert(QStringLiteral("controller"), QVariant::fromValue(controller));
        }
        qml->setInitialProperties(properties);
        qml->load(QUrl::fromLocalFile(QStringLiteral(VB_QT_QML_DIR "/Main.qml")));
        if (qml->rootObjects().size() != 1) return false;
        window = qobject_cast<QQuickWindow*>(qml->rootObjects().first());
        if (!window) return false;
        window->requestActivate();
        return QTest::qWaitForWindowExposed(window) && QTest::qWaitForWindowActive(window);
    }

    void closeShell() {
        window = nullptr;
        qml.reset();
    }

    QQuickItem* item(const char* name) const {
        return window ? boardUiFindItem(window->contentItem(), QString::fromLatin1(name)) : nullptr;
    }

    static QRectF bounds(QQuickItem* target) {
        return target->mapRectToScene(QRectF(0, 0, target->width(), target->height()));
    }

    // The board remains usable in a short/narrow workspace. Scroll to a
    // control before sending actual mouse events instead of bypassing QML.
    bool reveal(QQuickItem* target) const {
        if (!target || !target->isVisible()) return false;
        auto* flick = item("boardFlickable");
        if (flick) {
            bool insideBoard = false;
            for (auto* ancestor = target->parentItem(); ancestor; ancestor = ancestor->parentItem())
                if (ancestor == flick) insideBoard = true;
            if (insideBoard) {
                const auto local = target->mapRectToItem(
                    flick, QRectF(0, 0, target->width(), target->height()));
                qreal x = flick->property("contentX").toReal();
                qreal y = flick->property("contentY").toReal();
                if (local.left() < 0) x += local.left() - 4;
                else if (local.right() > flick->width()) x += local.right() - flick->width() + 4;
                if (local.top() < 0) y += local.top() - 4;
                else if (local.bottom() > flick->height()) y += local.bottom() - flick->height() + 4;
                flick->setProperty("contentX", std::clamp(x, qreal(0),
                    std::max(qreal(0), flick->property("contentWidth").toReal() - flick->width())));
                flick->setProperty("contentY", std::clamp(y, qreal(0),
                    std::max(qreal(0), flick->property("contentHeight").toReal() - flick->height())));
                QCoreApplication::processEvents();
            }
        }
        const QPointF center = bounds(target).center();
        return QRectF(0, 0, window->width(), window->height()).contains(center);
    }

    bool click(const char* name) const {
        auto* target = item(name);
        if (!reveal(target) || !target->isEnabled()) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, bounds(target).center().toPoint());
        return true;
    }

    bool press(const char* name) const {
        auto* target = item(name);
        if (!reveal(target) || !target->isEnabled()) return false;
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, bounds(target).center().toPoint());
        return true;
    }

    bool release(const char* name) const {
        auto* target = item(name);
        if (!reveal(target)) return false;
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, bounds(target).center().toPoint());
        return true;
    }

    bool keyActivate(const char* name) const {
        auto* target = item(name);
        if (!reveal(target) || !target->isEnabled()) return false;
        target->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        return true;
    }

    bool capture(const char* name) const {
        const QString directory = qEnvironmentVariable("VB_QT_SCREENSHOT_DIR");
        if (directory.isEmpty()) return true;
        if (!QDir().mkpath(directory)) return false;
        QImage image;
        for (int attempt = 0; attempt < 30 && image.isNull(); ++attempt) {
            window->requestUpdate();
            QTest::qWait(10);
            image = window->grabWindow();
        }
        return !image.isNull() && image.save(QDir(directory).filePath(
            QString::fromLatin1(name) + QStringLiteral(".png")));
    }

    std::unique_ptr<vb::SimEngine> engine;
    vb::BoardModel board;
    vb::qt::BoardAdapter adapter;
    std::unique_ptr<QQmlApplicationEngine> qml;
    QQuickWindow* window = nullptr;
    QStringList warnings;
};
