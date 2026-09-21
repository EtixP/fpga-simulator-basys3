// Exercise the application shell through the same mouse and keyboard paths as
// a user. No simulation engine is created: layout must work before a board exists.
#include "qt/BoardAdapter.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QStyleHints>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

class ShellTest final : public QObject {
    Q_OBJECT

private:
    // Destruction order is QML -> adapter, matching the application launcher.
    std::unique_ptr<vb::qt::BoardAdapter> adapter_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QQuickWindow* window_ = nullptr;
    QStringList warnings_;

    bool loadShell(bool missingAdapter = false) {
        window_ = nullptr;
        engine_.reset();
        adapter_ = std::make_unique<vb::qt::BoardAdapter>();
        QQmlEngine::setObjectOwnership(adapter_.get(), QQmlEngine::CppOwnership);
        engine_ = std::make_unique<QQmlApplicationEngine>();
        connect(engine_.get(), &QQmlEngine::warnings, this,
                [this](const QList<QQmlError>& errors) {
                    for (const auto& error : errors) warnings_.append(error.toString());
                });
        auto* injected = missingAdapter ? nullptr : adapter_.get();
        engine_->setInitialProperties({{QStringLiteral("board"), QVariant::fromValue(injected)}});
        engine_->load(QUrl::fromLocalFile(QStringLiteral(VB_QT_QML_DIR "/Main.qml")));
        if (engine_->rootObjects().size() != 1) return false;
        window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().first());
        if (!window_) return false;
        window_->requestActivate();
        return QTest::qWaitForWindowExposed(window_);
    }

    QQuickItem* item(const char* name) const {
        return window_ ? window_->findChild<QQuickItem*>(QString::fromLatin1(name)) : nullptr;
    }

    QString label(const char* name) const {
        auto* found = item(name);
        return found ? found->property("text").toString() : QString();
    }

    static QRectF bounds(QQuickItem* target) {
        return target->mapRectToScene(QRectF(0, 0, target->width(), target->height()));
    }

    bool click(const char* name) {
        auto* target = item(name);
        if (!target || !target->isVisible() || !target->isEnabled()) return false;
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
                          bounds(target).center().toPoint());
        return true;
    }

    void shortcut(Qt::Key key) {
        QTest::keyClick(window_, key, Qt::ControlModifier | Qt::ShiftModifier);
    }

    bool layoutIsContained() const {
        if (!window_) return false;
        const QRectF viewport(-1, -1, window_->width() + 2, window_->height() + 2);
        for (const auto* name : {"projectPane", "workspacePane", "inspectorPane", "outputPane"}) {
            auto* pane = item(name);
            if (!pane) return false;
            if (pane->isVisible() &&
                (pane->width() <= 0 || pane->height() <= 0 || !viewport.contains(bounds(pane))))
                return false;
        }
        auto* workspace = item("workspacePane");
        if (!workspace->isVisible()) return false;
        const auto center = bounds(workspace);
        // M3's board keeps usable control sizes in a scrollable viewport. The
        // viewport must fit the shell; its content may intentionally extend past it.
        auto* boardViewport = item("boardFlickable");
        if (boardViewport && boardViewport->isVisible() &&
            !center.adjusted(-1, -1, 1, 1).contains(bounds(boardViewport))) return false;
        if (item("projectPane")->isVisible() &&
            bounds(item("projectPane")).right() > center.left() + 1) return false;
        if (item("inspectorPane")->isVisible() &&
            bounds(item("inspectorPane")).left() < center.right() - 1) return false;
        if (item("outputPane")->isVisible() &&
            bounds(item("outputPane")).top() < center.bottom() - 1) return false;
        return true;
    }

    bool focusIsWithin(const char* name) const {
        auto* ancestor = item(name);
        for (auto* focus = window_->activeFocusItem(); focus; focus = focus->parentItem())
            if (focus == ancestor) return true;
        return false;
    }

    QString textOverflow(const char* paneName) const {
        auto* pane = item(paneName);
        if (!pane || !pane->isVisible()) return QStringLiteral("Pane is missing or hidden");
        for (auto* child : pane->findChildren<QQuickItem*>()) {
            const QString text = child->property("text").toString();
            if (!child->isVisible() || text.isEmpty()) continue;
            const auto rectangle = bounds(child);
            for (auto* ancestor = child->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
                if (ancestor == item("boardFlickable")) break;
                if ((ancestor == pane || ancestor->clip()) &&
                    !bounds(ancestor).adjusted(-1, -1, 1, 1).contains(rectangle))
                    return QStringLiteral("Clipped text/control: %1").arg(text);
            }
            const QVariant contentHeight = child->property("contentHeight");
            if (contentHeight.isValid() && contentHeight.toReal() > child->height() + 1)
                return QStringLiteral("Text height exceeds its allocated space: %1").arg(text);
        }
        return {};
    }

    bool capture(const char* name) const {
        const QString destination = qEnvironmentVariable("VB_QT_SCREENSHOT_DIR");
        if (destination.isEmpty()) return true;
        if (!QDir().mkpath(destination)) return false;
        QImage image;
        for (int attempt = 0; attempt < 30 && image.isNull(); ++attempt) {
            window_->requestUpdate();
            QTest::qWait(10);
            image = window_->grabWindow();
        }
        return !image.isNull() && image.save(QDir(destination).filePath(
            QString::fromLatin1(name) + QStringLiteral(".png")));
    }

private slots:
    void init() {
        warnings_.clear();
        QVERIFY2(loadShell(), qPrintable(warnings_.join('\n')));
        QTRY_VERIFY(layoutIsContained());
    }

    void cleanup() {
        window_ = nullptr;
        engine_.reset();
        adapter_.reset();
        QVERIFY2(warnings_.isEmpty(), qPrintable(warnings_.join('\n')));
    }

    void initialLayoutAndDisconnectedState() {
        QCOMPARE(window_->objectName(), QStringLiteral("shellWindow"));
        QCOMPARE(window_->minimumWidth(), 960);
        QCOMPARE(window_->minimumHeight(), 640);
        QCOMPARE(window_->width(), 1280);
        QCOMPARE(window_->height(), 820);
        QCOMPARE(window_->property("workspaceIndex").toInt(), 0);
        QCOMPARE(window_->property("outputIndex").toInt(), 0);
        for (const auto* name : {"projectPane", "workspacePane", "inspectorPane", "outputPane"})
            QVERIFY(item(name)->isVisible());
        QVERIFY(!label("workspaceTitle").isEmpty());
        QVERIFY(!label("workspaceDetail").isEmpty());
        QVERIFY(!label("outputTitle").isEmpty());
        QVERIFY(!label("outputDetail").isEmpty());
        QVERIFY(!label("workspaceTitle").contains(QStringLiteral("ready"), Qt::CaseInsensitive));
        QVERIFY(item("horizontalSplit"));
        QVERIFY(item("verticalSplit"));
        QVERIFY(capture("shell-default"));
    }

    void navigationAndOutputTabs() {
        const QString boardTitle = label("workspaceTitle");
        QVERIFY(click("overviewNav"));
        QTRY_COMPARE(window_->property("workspaceIndex").toInt(), 1);
        QTRY_VERIFY(label("workspaceTitle") != boardTitle);
        QVERIFY(click("boardNav"));
        QTRY_COMPARE(window_->property("workspaceIndex").toInt(), 0);
        QCOMPARE(label("workspaceTitle"), boardTitle);

        QStringList tabTitles;
        int index = 0;
        for (const auto* name : {"terminalTab", "uartTab", "logsTab", "waveformsTab"}) {
            QVERIFY(click(name));
            QTRY_COMPARE(window_->property("outputIndex").toInt(), index);
            QVERIFY(!label("outputTitle").isEmpty());
            QVERIFY(!label("outputDetail").isEmpty());
            QVERIFY(!tabTitles.contains(label("outputTitle")));
            tabTitles.append(label("outputTitle"));
            ++index;
        }
        // Keyboard activation of a focused navigation control follows its action.
        item("overviewNav")->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(focusIsWithin("overviewNav"));
        QTest::keyClick(window_, Qt::Key_Space);
        QTRY_COMPARE(window_->property("workspaceIndex").toInt(), 1);
    }

    void panelControlsAndKeyboardRecovery() {
        const qreal originalWidth = item("workspacePane")->width();
        const qreal originalHeight = item("workspacePane")->height();
        QVERIFY(click("projectToggle"));
        QTRY_VERIFY(!item("projectPane")->isVisible());
        QTRY_VERIFY(item("workspacePane")->width() > originalWidth);
        QVERIFY(click("inspectorToggle"));
        QTRY_VERIFY(!item("inspectorPane")->isVisible());
        QVERIFY(click("outputToggle"));
        QTRY_VERIFY(!item("outputPane")->isVisible());
        QTRY_VERIFY(item("workspacePane")->height() > originalHeight);
        QTRY_VERIFY(layoutIsContained());
        QVERIFY(capture("shell-panels-hidden"));

        shortcut(Qt::Key_1);
        QTRY_VERIFY(item("projectPane")->isVisible());
        shortcut(Qt::Key_2);
        QTRY_VERIFY(item("inspectorPane")->isVisible());
        shortcut(Qt::Key_3);
        QTRY_VERIFY(item("outputPane")->isVisible());

        item("boardNav")->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(focusIsWithin("projectPane"));
        shortcut(Qt::Key_1);
        QTRY_VERIFY(!item("projectPane")->isVisible());
        QTRY_VERIFY(focusIsWithin("projectToggle"));

        item("waveformsTab")->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(focusIsWithin("outputPane"));
        shortcut(Qt::Key_3);
        QTRY_VERIFY(!item("outputPane")->isVisible());
        QTRY_VERIFY(focusIsWithin("outputToggle"));

        shortcut(Qt::Key_0);
        QTRY_VERIFY(item("projectPane")->isVisible());
        QTRY_VERIFY(item("outputPane")->isVisible());
        QTRY_COMPARE(window_->property("workspaceIndex").toInt(), 0);
        QTRY_COMPARE(window_->property("outputIndex").toInt(), 0);
        QTRY_VERIFY(layoutIsContained());
    }

    void keyboardNavigationAndOutputHideButton() {
        item("projectToggle")->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(focusIsWithin("projectToggle"));
        const QList<QByteArray> reachable{
            "projectToggle", "inspectorToggle", "outputToggle", "restoreLayoutButton",
            "boardNav", "overviewNav", "terminalTab", "closeOutputButton"};
        QSet<QByteArray> visited;
        for (int step = 0; step < 32 && visited.size() < reachable.size(); ++step) {
            for (const auto& name : reachable)
                if (focusIsWithin(name.constData())) visited.insert(name);
            QTest::keyClick(window_, Qt::Key_Tab);
            QCoreApplication::processEvents();
        }
        for (const auto& name : reachable)
            QVERIFY2(visited.contains(name), qPrintable(QStringLiteral("Not reachable with Tab: %1")
                                                       .arg(QString::fromLatin1(name))));

        item("terminalTab")->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(focusIsWithin("terminalTab"));
        QTest::keyClick(window_, Qt::Key_Right);
        QTRY_COMPARE(window_->property("outputIndex").toInt(), 1);
        QTRY_VERIFY(focusIsWithin("uartTab"));
        QTest::keyClick(window_, Qt::Key_Right);
        QTRY_COMPARE(window_->property("outputIndex").toInt(), 2);
        QTest::keyClick(window_, Qt::Key_Left);
        QTRY_COMPARE(window_->property("outputIndex").toInt(), 1);

        QVERIFY(click("closeOutputButton"));
        QTRY_VERIFY(!item("outputPane")->isVisible());
        QTRY_VERIFY(focusIsWithin("outputToggle"));
        QVERIFY(click("outputToggle"));
        QTRY_VERIFY(item("outputPane")->isVisible());
        QCOMPARE(window_->property("outputIndex").toInt(), 1);
        QTRY_VERIFY(layoutIsContained());
    }

    void splittersResizeAndRestore() {
        const auto project = bounds(item("projectPane"));
        const auto workspace = bounds(item("workspacePane"));
        const QPoint horizontalHandle(qRound((project.right() + workspace.left()) / 2),
                                      qRound(project.center().y()));
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, horizontalHandle);
        QTest::mouseMove(window_, horizontalHandle + QPoint(85, 0), 20);
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                            horizontalHandle + QPoint(85, 0));
        QTRY_VERIFY(item("projectPane")->width() > project.width() + 40);

        const auto output = bounds(item("outputPane"));
        const auto updatedWorkspace = bounds(item("workspacePane"));
        const QPoint verticalHandle(qRound(output.center().x()),
                                    qRound((updatedWorkspace.bottom() + output.top()) / 2));
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, verticalHandle);
        QTest::mouseMove(window_, verticalHandle - QPoint(0, 70), 20);
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                            verticalHandle - QPoint(0, 70));
        QTRY_VERIFY(item("outputPane")->height() > output.height() + 35);
        QTRY_VERIFY(layoutIsContained());

        QVERIFY(click("overviewNav"));
        QVERIFY(click("waveformsTab"));
        QVERIFY(click("restoreLayoutButton"));
        QTRY_COMPARE(qRound(item("projectPane")->width()), 220);
        QTRY_COMPARE(qRound(item("inspectorPane")->width()), 260);
        QTRY_COMPARE(qRound(item("outputPane")->height()), 170);
        QTRY_COMPARE(window_->property("workspaceIndex").toInt(), 0);
        QTRY_COMPARE(window_->property("outputIndex").toInt(), 0);
        QTRY_VERIFY(layoutIsContained());
    }

    void minimumSizeAndRepeatedVisibilityChanges() {
        window_->resize(960, 640);
        QTRY_COMPARE(window_->size(), QSize(960, 640));
        QTRY_VERIFY(layoutIsContained());
        for (const auto* name : {"projectPane", "workspacePane", "inspectorPane", "outputPane"})
            QTRY_VERIFY2(textOverflow(name).isEmpty(), qPrintable(textOverflow(name)));
        QVERIFY(capture("shell-minimum"));
        for (int repetition = 0; repetition < 5; ++repetition) {
            for (const auto* name : {"projectToggle", "inspectorToggle", "outputToggle"}) {
                QVERIFY(click(name));
                QTRY_VERIFY(layoutIsContained());
            }
        }
        QTRY_VERIFY(!item("projectPane")->isVisible());
        QTRY_VERIFY(!item("inspectorPane")->isVisible());
        QTRY_VERIFY(!item("outputPane")->isVisible());
        window_->resize(1440, 900);
        QTRY_VERIFY(layoutIsContained());
        shortcut(Qt::Key_0);
        QTRY_VERIFY(item("projectPane")->isVisible());
        QTRY_VERIFY(item("inspectorPane")->isVisible());
        QTRY_VERIFY(item("outputPane")->isVisible());
        QTRY_VERIFY(layoutIsContained());
        window_->resize(960, 640);
        QTRY_VERIFY(layoutIsContained());
    }

    void maximumOutputHeightKeepsTopContentAccessible() {
        window_->resize(960, 640);
        QTRY_COMPARE(window_->size(), QSize(960, 640));
        QTRY_VERIFY(layoutIsContained());
        // Narrowing the project pane wraps its explanatory text, exercising
        // the smallest usable height with the tallest version of that content.
        const auto project = bounds(item("projectPane"));
        const auto initialWorkspace = bounds(item("workspacePane"));
        const QPoint sideHandle(qRound((project.right() + initialWorkspace.left()) / 2),
                                qRound(project.center().y()));
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, sideHandle);
        QTest::mouseMove(window_, sideHandle - QPoint(160, 0), 20);
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, sideHandle - QPoint(160, 0));
        QTRY_COMPARE(qRound(item("projectPane")->width()), 180);
        const auto output = bounds(item("outputPane"));
        const auto workspace = bounds(item("workspacePane"));
        const QPoint handle(qRound(output.center().x()),
                            qRound((workspace.bottom() + output.top()) / 2));
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier, handle);
        QTest::mouseMove(window_, handle - QPoint(0, 240), 20);
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, handle - QPoint(0, 240));
        QTRY_VERIFY(layoutIsContained());
        for (const auto* name : {"projectPane", "workspacePane", "inspectorPane", "outputPane"})
            QTRY_VERIFY2(textOverflow(name).isEmpty(), qPrintable(textOverflow(name)));
        QVERIFY(click("overviewNav"));
        QTRY_COMPARE(window_->property("workspaceIndex").toInt(), 1);
        QTRY_VERIFY2(textOverflow("workspacePane").isEmpty(),
                     qPrintable(textOverflow("workspacePane")));
        QVERIFY(click("boardNav"));
        QTRY_COMPARE(window_->property("workspaceIndex").toInt(), 0);
        QVERIFY(capture("shell-output-expanded"));
    }

    void missingAdapterHasRecoverableErrorState() {
        QVERIFY2(loadShell(true), qPrintable(warnings_.join('\n')));
        QTRY_VERIFY(layoutIsContained());
        QTRY_COMPARE(label("workspaceTitle"), QStringLiteral("Board connection unavailable"));
        QVERIFY(!label("workspaceDetail").isEmpty());
        QVERIFY(click("overviewNav"));
        QTRY_COMPARE(window_->property("workspaceIndex").toInt(), 1);
        QVERIFY(click("logsTab"));
        QTRY_COMPARE(window_->property("outputIndex").toInt(), 2);
        QVERIFY(click("projectToggle"));
        QTRY_VERIFY(!item("projectPane")->isVisible());
        shortcut(Qt::Key_0);
        QTRY_VERIFY(item("projectPane")->isVisible());
        QTRY_COMPARE(label("workspaceTitle"), QStringLiteral("Board connection unavailable"));
        QVERIFY(capture("shell-missing-adapter"));
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    // Respect normal tab order without inheriting the host's macOS preference
    // to restrict Tab navigation to text fields and lists.
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    QGuiApplication::setApplicationName(QStringLiteral("VirtualBasys shell tests"));
    ShellTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_shell.moc"
