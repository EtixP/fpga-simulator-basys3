// Exercise the QML board against independently supplied pins. Only this test's
// explicit BoardModel::tick calls may advance time; window/input events may not.
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "qt/BoardAdapter.h"

#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QJSEngine>
#include <QJSValue>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QStyleHints>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

Q_IMPORT_QML_PLUGIN(VirtualBasys_BoardPlugin)

namespace {
class PinEngine final : public vb::SimEngine {
public:
    enum Port : size_t { Switches, Buttons, Leds, Segments, Anodes, Point };
    std::array<uint64_t, 6> values{0, 0, 0, 127, 15, 1};
    unsigned steps = 0;
    unsigned pokes = 0;

    vb::SignalId lookup(std::string_view name) override {
        for (size_t i = 0; i < metadata_.size(); ++i)
            if (metadata_[i].name == name) return vb::SignalId(i);
        return vb::kNoSignal;
    }
    vb::SignalInfo info(vb::SignalId id) const override {
        return metadata_.at(size_t(id));
    }
    std::vector<vb::SignalInfo> ports() const override {
        auto result = metadata_;
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.name < b.name;
        });
        return result;
    }
    void step(uint64_t cycles) override { ++steps; now_ += cycles; }
    uint64_t now() const override { return now_; }
    uint64_t peek(vb::SignalId id) override { return values.at(size_t(id)); }
    void poke(vb::SignalId id, uint64_t value) override {
        const auto index = size_t(id);
        if (index > Buttons) throw std::invalid_argument("not an input");
        ++pokes;
        values[index] = value & ((uint64_t{1} << metadata_[index].width) - 1);
    }
    void setTraceFile(std::string_view) override {
        throw std::logic_error("the UI must not request tracing");
    }
    void trace(bool) override {
        throw std::logic_error("the UI must not request tracing");
    }

private:
    uint64_t now_ = 0;
    const std::vector<vb::SignalInfo> metadata_{
        {"inputs", 16, true}, {"keys", 5, true}, {"lamps", 16, false},
        {"seg", 7, false}, {"an", 4, false}, {"dp", 1, false}};
};

std::string constraints() {
    constexpr std::array<const char*, 16> switches{
        "V17", "V16", "W16", "W17", "W15", "V15", "W14", "W13",
        "V2", "T3", "T2", "R3", "W2", "U1", "T1", "R2"};
    constexpr std::array<const char*, 16> leds{
        "U16", "E19", "U19", "V19", "W18", "U15", "U14", "V14",
        "V13", "V3", "W3", "U3", "P3", "N3", "P1", "L1"};
    constexpr std::array<const char*, 5> buttons{"U18", "T18", "W19", "T17", "U17"};
    constexpr std::array<const char*, 7> segments{"W7", "W6", "U8", "V8", "U5", "V5", "U7"};
    constexpr std::array<const char*, 4> anodes{"U2", "U4", "V4", "W4"};
    std::string result;
    const auto bind = [&result](const char* pin, const char* port, int bit) {
        result += std::string("set_property PACKAGE_PIN ") + pin + " [get_ports {"
            + port + "[" + std::to_string(bit) + "]}]\n";
    };
    for (int row = 0; row < 16; ++row) {
        // Reversed port bits deliberately prevent a widget index from being
        // substituted for the board's actual XDC resource mapping.
        bind(switches[size_t(row)], "inputs", 15 - row);
        bind(leds[size_t(row)], "lamps", 15 - row);
    }
    for (int row = 0; row < 5; ++row) bind(buttons[size_t(row)], "keys", row);
    for (int row = 0; row < 7; ++row) bind(segments[size_t(row)], "seg", row);
    for (int row = 0; row < 4; ++row) bind(anodes[size_t(row)], "an", row);
    result += "set_property PACKAGE_PIN V7 [get_ports dp]\n";
    return result;
}
} // namespace

class BoardWidgetsTest final : public QObject {
    Q_OBJECT

private:
    // Destruction order: QML, adapter, board, engine.
    std::unique_ptr<PinEngine> pins_;
    std::unique_ptr<vb::BoardModel> board_;
    std::unique_ptr<vb::qt::BoardAdapter> adapter_;
    std::unique_ptr<QQmlApplicationEngine> qml_;
    QQuickWindow* window_ = nullptr;
    QStringList warnings_;

    // Repeaters parent delegates visually beneath their containing item, but
    // the QObject ownership tree need not follow that visual hierarchy.
    static QQuickItem* findItem(QQuickItem* parent, const QString& name) {
        if (!parent) return nullptr;
        if (parent->objectName() == name) return parent;
        for (auto* child : parent->childItems())
            if (auto* found = findItem(child, name)) return found;
        return nullptr;
    }
    QQuickItem* item(const QString& name) const {
        return window_ ? findItem(window_->contentItem(), name) : nullptr;
    }
    QQuickItem* control(const char* prefix, int row) const {
        return item(QString::fromLatin1(prefix) + QString::number(row));
    }
    static QRectF bounds(QQuickItem* target) {
        return target->mapRectToScene(QRectF(0, 0, target->width(), target->height()));
    }
    bool withinViewport(QQuickItem* target) const {
        auto* viewport = item(QStringLiteral("boardFlickable"));
        return target && viewport && target->isVisible()
            && target->width() > 0 && target->height() > 0
            && bounds(viewport).adjusted(-1, -1, 1, 1).contains(bounds(target));
    }
    bool focusControl(QQuickItem* target) {
        if (!target || !target->isEnabled() || !target->isVisible()) return false;
        target->forceActiveFocus(Qt::TabFocusReason);
        for (int attempt = 0; attempt < 30; ++attempt) {
            QCoreApplication::processEvents();
            if (target->hasActiveFocus() && withinViewport(target)) return true;
            QTest::qWait(2);
        }
        return false;
    }
    bool press(QQuickItem* target) {
        if (!focusControl(target)) return false;
        QTest::mousePress(window_, Qt::LeftButton, Qt::NoModifier,
                          bounds(target).center().toPoint());
        return true;
    }
    void release(QQuickItem* target) {
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier,
                            bounds(target).center().toPoint());
    }
    static QQuickItem* segment(QQuickItem* digit, int bit) {
        return findItem(digit, QStringLiteral("segment%1").arg(bit));
    }
    static bool lit(QQuickItem* piece) { return piece->property("lit").toBool(); }
    // BoardLayout.js as QML sees it: its vars become the engine's globals.
    static QJSValue boardLayout(QJSEngine& js) {
        QFile file(QStringLiteral(VB_QT_QML_DIR "/BoardLayout.js"));
        if (!file.open(QIODevice::ReadOnly)) return QJSValue();
        QString code = QString::fromUtf8(file.readAll());
        code.remove(QStringLiteral(".pragma library"));
        return js.evaluate(code).isError() ? QJSValue() : js.globalObject();
    }
    static QRectF layoutRect(const QJSValue& r) {
        return QRectF(r.property(0).toNumber(), r.property(1).toNumber(),
                      r.property(2).toNumber(), r.property(3).toNumber());
    }
    static QRectF geometry(QQuickItem* target) {
        return QRectF(target->x(), target->y(), target->width(), target->height());
    }
    bool held(int index) const { return board_->buttonState(vb::Button(index)); }
    void repeatKey(QEvent::Type type) {
        QKeyEvent repeat(type, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), true, 1);
        QCoreApplication::sendEvent(window_, &repeat);
    }

private slots:
    void init() {
        warnings_.clear();
        pins_ = std::make_unique<PinEngine>();
        const auto doc = vb::parseXdc(constraints());
        QVERIFY(doc.warnings.empty());
        auto binding = vb::PinBinding::bind(doc, *pins_);
        QVERIFY(binding.diagnostics().empty());
        board_ = std::make_unique<vb::BoardModel>(*pins_, std::move(binding));
        board_->setLogEnabled(true);
        adapter_ = std::make_unique<vb::qt::BoardAdapter>(board_.get());
        QQmlEngine::setObjectOwnership(adapter_.get(), QQmlEngine::CppOwnership);
        qml_ = std::make_unique<QQmlApplicationEngine>();
        connect(qml_.get(), &QQmlEngine::warnings, this,
                [this](const QList<QQmlError>& errors) {
                    for (const auto& error : errors) warnings_.append(error.toString());
                });
        qml_->setInitialProperties({{QStringLiteral("board"), QVariant::fromValue(adapter_.get())}});
        qml_->load(QUrl::fromLocalFile(QStringLiteral(VB_QT_QML_DIR "/Main.qml")));
        QVERIFY2(qml_->rootObjects().size() == 1, qPrintable(warnings_.join('\n')));
        window_ = qobject_cast<QQuickWindow*>(qml_->rootObjects().first());
        QVERIFY(window_);
        window_->resize(1600, 1000);
        window_->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
        QVERIFY(QTest::qWaitForWindowActive(window_));
        QTRY_VERIFY(control("sw", 15) && control("button", 4) && control("digit", 3));
        QTRY_VERIFY(withinViewport(control("sw", 0)));
    }

    void cleanup() {
        window_ = nullptr;
        qml_.reset();
        adapter_.reset();
        board_.reset();
        pins_.reset();
        QVERIFY2(warnings_.isEmpty(), qPrintable(warnings_.join('\n')));
    }

    void allResourcesUseBoardIndicesAndModelState() {
        for (int row = 0; row < 16; ++row) {
            auto* sw = control("sw", row);
            auto* led = control("led", row);
            QVERIFY(sw && led);
            QVERIFY(sw->isEnabled());
            QCOMPARE(sw->property("resource").toString(), QStringLiteral("SW%1").arg(row));
            QCOMPARE(led->property("resource").toString(), QStringLiteral("LED%1").arg(row));
            if (row != 0) {
                QVERIFY(bounds(sw).center().x() < bounds(control("sw", row - 1)).center().x());
                QVERIFY(bounds(led).center().x() < bounds(control("led", row - 1)).center().x());
            }
            QVERIFY(press(sw));
            release(sw);
            QCOMPARE(pins_->values[PinEngine::Switches], uint64_t{1} << (15 - row));
            QTRY_VERIFY(sw->property("active").toBool());
            QVERIFY(adapter_->setSwitch(row, false));
            QTRY_VERIFY(!sw->property("active").toBool());
            QCOMPARE(pins_->values[PinEngine::Switches], uint64_t{0});

            pins_->values[PinEngine::Leds] = uint64_t{1} << (15 - row);
            QVERIFY(adapter_->refresh());
            for (int other = 0; other < 16; ++other)
                QCOMPARE(control("led", other)->property("active").toBool(), other == row);
        }
        QVERIFY(focusControl(control("sw", 0)));
        QTest::keyClick(window_, Qt::Key_Space);
        QVERIFY(board_->switchState(0));
        QVERIFY(adapter_->setSwitch(0, false));
        QTest::keyClick(window_, Qt::Key_Space);
        QVERIFY(board_->switchState(0));
        QCOMPARE(pins_->now(), uint64_t{0});
        QCOMPARE(pins_->steps, 0u);
    }

    void everyButtonHoldsUntilActualRelease() {
        for (int row = 0; row < 5; ++row) {
            auto* button = control("button", row);
            QCOMPARE(button->property("resource").toString(), QString::fromLatin1(vb::kButtonNames[size_t(row)]));
            QVERIFY(press(button));
            QVERIFY(held(row));
            QCOMPARE(pins_->values[PinEngine::Buttons], uint64_t{1} << row);
            QTest::qWait(5);
            QVERIFY(held(row));
            QVERIFY(button->property("active").toBool());
            release(button);
            QVERIFY(!held(row));
            QVERIFY(!button->property("active").toBool());
        }
        const auto center = bounds(control("button", 0)).center();
        QVERIFY(bounds(control("button", 1)).center().y() < center.y());
        QVERIFY(bounds(control("button", 2)).center().x() < center.x());
        QVERIFY(bounds(control("button", 3)).center().x() > center.x());
        QVERIFY(bounds(control("button", 4)).center().y() > center.y());
        QCOMPARE(pins_->now(), uint64_t{0});
        QCOMPARE(pins_->steps, 0u);
    }

    void keyboardHoldIgnoresRepeatsAndReleasesOnFocusLoss() {
        auto* button = control("button", 1);
        QVERIFY(focusControl(button));
        QTest::keyPress(window_, Qt::Key_Space);
        QVERIFY(held(1));
        const auto pokes = pins_->pokes;
        repeatKey(QEvent::KeyRelease);
        repeatKey(QEvent::KeyPress);
        QVERIFY(held(1));
        QCOMPARE(pins_->pokes, pokes);
        QTest::keyRelease(window_, Qt::Key_Space);
        QVERIFY(!held(1));

        QTest::keyPress(window_, Qt::Key_Space);
        QVERIFY(held(1));
        QTest::keyClick(window_, Qt::Key_Tab);
        QTRY_VERIFY(!button->hasActiveFocus());
        QVERIFY(!held(1));
        QVERIFY(focusControl(button));
        repeatKey(QEvent::KeyPress);
        QVERIFY(!held(1));
        QTest::keyRelease(window_, Qt::Key_Space);
        QTest::keyPress(window_, Qt::Key_Space);
        QVERIFY(held(1));
        QTest::keyRelease(window_, Qt::Key_Space);
        QVERIFY(!held(1));
        QCOMPARE(pins_->now(), uint64_t{0});
    }

    void pointerCancellationNeverLeavesAnAssertion() {
        auto* button = control("button", 2);
        QVERIFY(press(button));
        QVERIFY(held(2));
        const QPoint outside = bounds(item(QStringLiteral("workspacePane"))).topLeft().toPoint() + QPoint(10, 10);
        QTest::mouseMove(window_, outside);
        QTest::mouseRelease(window_, Qt::LeftButton, Qt::NoModifier, outside);
        QVERIFY(!held(2));

        QVERIFY(press(button));
        QVERIFY(held(2));
        button->ungrabMouse();
        QVERIFY(!held(2));
        QTest::mouseMove(window_, outside);
        QTest::mouseMove(window_, bounds(button).center().toPoint());
        QVERIFY(!held(2));
        release(button);
        QVERIFY(!held(2));
        QVERIFY(press(button));
        QVERIFY(held(2));
        release(button);
        QVERIFY(!held(2));
        QCOMPARE(pins_->now(), uint64_t{0});
    }

    void navigationAndDisableReleaseHeldInputs() {
        auto* button = control("button", 3);
        QVERIFY(focusControl(button));
        QTest::keyPress(window_, Qt::Key_Space);
        QVERIFY(held(3));
        QVERIFY(window_->setProperty("workspaceIndex", 1));
        QTRY_VERIFY(!item(QStringLiteral("boardView"))->isVisible());
        QVERIFY(!held(3));
        QVERIFY(window_->setProperty("workspaceIndex", 0));
        QVERIFY(focusControl(button));
        repeatKey(QEvent::KeyPress);
        QVERIFY(!held(3));
        QTest::keyRelease(window_, Qt::Key_Space);

        QVERIFY(press(button));
        QVERIFY(held(3));
        button->setEnabled(false);
        QVERIFY(!held(3));
        button->setEnabled(true);
        QVERIFY(!held(3));
        release(button);
        QVERIFY(press(button));
        QVERIFY(held(3));
        release(button);
        QVERIFY(!held(3));
        QCOMPARE(pins_->now(), uint64_t{0});
    }

    void windowDeactivationAndHideReleaseHeldInputs() {
        auto* button = control("button", 0);
        QVERIFY(focusControl(button));
        QTest::keyPress(window_, Qt::Key_Space);
        QVERIFY(held(0));
        QQuickWindow other;
        other.resize(100, 100);
        other.show();
        other.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QTRY_VERIFY(!window_->isActive());
        QVERIFY(!held(0));
        other.hide();
        window_->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window_));
        QVERIFY(focusControl(button));
        repeatKey(QEvent::KeyPress);
        QVERIFY(!held(0));
        QTest::keyRelease(window_, Qt::Key_Space);

        QVERIFY(press(button));
        QVERIFY(held(0));
        window_->hide();
        QVERIFY(!held(0));
        window_->show();
        window_->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window_));
        QVERIFY(!held(0));
        release(button);
        QCOMPARE(pins_->now(), uint64_t{0});
    }

    void destroyingTheViewReleasesItsOwnedInput() {
        QVERIFY(press(control("button", 4)));
        QVERIFY(held(4));
        window_ = nullptr;
        qml_.reset();
        QVERIFY(!held(4));
        QCOMPARE(pins_->values[PinEngine::Buttons], uint64_t{0});
        QCOMPARE(pins_->now(), uint64_t{0});
    }

    void externalAssertionsAreNeverReleasedByTheWidget() {
        auto* button = control("button", 0);
        QVERIFY(adapter_->setButton(0, true));
        const auto pokes = pins_->pokes;
        QVERIFY(press(button));
        release(button);
        QVERIFY(held(0));
        QCOMPARE(pins_->pokes, pokes);
        item(QStringLiteral("boardView"))->setVisible(false);
        QVERIFY(held(0));
        window_ = nullptr;
        qml_.reset();
        QVERIFY(held(0));
        QCOMPARE(pins_->pokes, pokes);
        QVERIFY(adapter_->setButton(0, false));
    }

    void externalReleaseEndsTheWidgetsOwnership() {
        auto* button = control("button", 0);
        QVERIFY(press(button));
        QVERIFY(held(0));
        QVERIFY(adapter_->setButton(0, false));
        QVERIFY(!held(0));
        QVERIFY(adapter_->setButton(0, true));
        const auto pokes = pins_->pokes;
        release(button);
        // The old gesture ended when its assertion was cleared externally.
        // A subsequent external assertion is not this widget's to release.
        QVERIFY(held(0));
        QCOMPARE(pins_->pokes, pokes);
        QVERIFY(adapter_->setButton(0, false));
    }

    void writerReplacementReleasesTheOldTargetAndRejectsNewWrites() {
        auto* button = control("button", 1);
        QVERIFY(press(button));
        QVERIFY(held(1));
        const QJSValue reject = qml_->evaluate(QStringLiteral("(function(value) { return false; })"));
        QVERIFY(reject.isCallable());
        QVERIFY(button->setProperty("writeInput", QVariant::fromValue(reject)));
        QVERIFY(!held(1));
        release(button);
        const auto pokes = pins_->pokes;
        QVERIFY(press(button));
        QVERIFY(!held(1));
        QVERIFY(!button->property("active").toBool());
        QVERIFY(!button->property("down").toBool());
        release(button);
        QCOMPARE(pins_->pokes, pokes);
        QCOMPARE(pins_->now(), uint64_t{0});
    }

    void sevenSegmentsUseRawLitBitsAndPhysicalOrder() {
        for (int digit = 0; digit < 4; ++digit) {
            auto* cell = control("digit", digit);
            QVERIFY(cell);
            if (digit != 0)
                QVERIFY(bounds(cell).center().x() < bounds(control("digit", digit - 1)).center().x());
            for (int bit = 0; bit < 7; ++bit) QVERIFY(segment(cell, bit));
            QVERIFY(findItem(cell, QStringLiteral("decimalPoint")));
        }
        auto* cell = control("digit", 0);
        const auto a = bounds(segment(cell, 0));
        const auto b = bounds(segment(cell, 1));
        const auto c = bounds(segment(cell, 2));
        const auto d = bounds(segment(cell, 3));
        const auto e = bounds(segment(cell, 4));
        const auto f = bounds(segment(cell, 5));
        const auto g = bounds(segment(cell, 6));
        QVERIFY(a.width() > a.height() && d.width() > d.height() && g.width() > g.height());
        QVERIFY(b.height() > b.width() && c.height() > c.width()
                && e.height() > e.width() && f.height() > f.width());
        QVERIFY(a.center().y() < g.center().y() && g.center().y() < d.center().y());
        QVERIFY(f.center().x() < g.center().x() && e.center().x() < g.center().x());
        QVERIFY(b.center().x() > g.center().x() && c.center().x() > g.center().x());
        QVERIFY(b.center().y() < g.center().y() && f.center().y() < g.center().y());
        QVERIFY(c.center().y() > g.center().y() && e.center().y() > g.center().y());

        pins_->values[PinEngine::Anodes] = 0;
        for (int selected = 0; selected < 7; ++selected) {
            pins_->values[PinEngine::Segments] = (~(uint64_t{1} << selected)) & 127;
            board_->tick(1000);
            QVERIFY(adapter_->refresh());
            for (int digit = 0; digit < 4; ++digit) {
                auto* target = control("digit", digit);
                for (int bit = 0; bit < 7; ++bit)
                    QCOMPARE(lit(segment(target, bit)), bit == selected);
                QVERIFY(!lit(findItem(target, QStringLiteral("decimalPoint"))));
            }
            // A lone top segment is not a decoded numeral, but is still drawn.
            if (selected == 0) QCOMPARE(cell->property("character").toString(), QStringLiteral("?"));
        }
        pins_->values[PinEngine::Segments] = 127;
        pins_->values[PinEngine::Point] = 0;
        board_->tick(1000);
        QVERIFY(adapter_->refresh());
        for (int digit = 0; digit < 4; ++digit) {
            auto* target = control("digit", digit);
            for (int bit = 0; bit < 7; ++bit) QVERIFY(!lit(segment(target, bit)));
            QVERIFY(lit(findItem(target, QStringLiteral("decimalPoint"))));
            QCOMPARE(target->property("character").toString(), QStringLiteral(" "));
        }
        QCOMPARE(pins_->now(), uint64_t{8000});
    }

    void displayPersistenceUsesInclusiveVirtualTimeOnly() {
        pins_->values[PinEngine::Segments] = (~uint64_t{0x5b}) & 127; // numeral 2
        pins_->values[PinEngine::Anodes] = 14; // only AN0 driven
        pins_->values[PinEngine::Point] = 0;
        board_->tick(1000);
        QVERIFY(adapter_->refresh());
        auto* digit = control("digit", 0);
        QCOMPARE(digit->property("segments").toInt(), 0x5b);
        QCOMPARE(digit->property("character").toString(), QStringLiteral("2"));
        QVERIFY(digit->property("decimalPoint").toBool());
        for (int other = 1; other < 4; ++other)
            QCOMPARE(control("digit", other)->property("segments").toInt(), 0);
        pins_->values[PinEngine::Anodes] = 15;
        QTest::qWait(30); // longer than the display's 20ms virtual persistence
        QCOMPARE(pins_->now(), uint64_t{1000});
        QCOMPARE(digit->property("segments").toInt(), 0x5b);
        board_->tick(vb::SevenSeg::kPersistCycles);
        QVERIFY(adapter_->refresh());
        QCOMPARE(digit->property("segments").toInt(), 0x5b);
        QVERIFY(digit->property("decimalPoint").toBool());
        board_->tick(1);
        QVERIFY(adapter_->refresh());
        QCOMPARE(digit->property("segments").toInt(), 0);
        QVERIFY(!digit->property("decimalPoint").toBool());
        for (int bit = 0; bit < 7; ++bit) QVERIFY(!lit(segment(digit, bit)));
        QVERIFY(!lit(findItem(digit, QStringLiteral("decimalPoint"))));
    }

    // tools/board_render.py writes BoardLayout.js, board.png and the sprites in one
    // run: every part has its sprite, at the size its rectangle gives (the render
    // has 16 px per mm, the layout 10 units per mm), inside the board image. A
    // switch, LED, button or DONE sprite's feathered edge is the board itself, so
    // it must match board.png where the layout places it: a moved rectangle, a
    // flipped offset or a swapped sprite does not (measured 0.7/255 placed right,
    // 11.6 or more for those faults).
    void renderedBoardMatchesItsLayout() {
        QJSEngine js;
        const QJSValue layout = boardLayout(js);
        QVERIFY(layout.isObject());
        constexpr double unitsPerPixel = 10.0 / 16.0;
        const auto size = [](const QString& path) {
            return QImageReader(QStringLiteral(VB_QT_QML_DIR "/board/") + path).size();
        };
        const QSize board = size(QStringLiteral("board.png"));
        const QImage boardImage = QImage(QStringLiteral(VB_QT_QML_DIR "/board/board.png"))
                                      .convertToFormat(QImage::Format_ARGB32);
        QVERIFY(!boardImage.isNull());
        const auto edgeError = [&](const QString& name, const QPointF& at) {
            const QImage spr = QImage(QStringLiteral(VB_QT_QML_DIR "/board/sprites/") + name)
                                   .convertToFormat(QImage::Format_ARGB32);
            const QPoint origin = (at / unitsPerPixel).toPoint();
            double total = 0;
            int count = 0;
            for (int y = 0; y < spr.height(); ++y)
                for (int x = 0; x < spr.width(); ++x) {
                    const QRgb s = spr.pixel(x, y);
                    if (qAlpha(s) == 0 || qAlpha(s) == 255) continue;
                    const QRgb b = boardImage.pixel(origin.x() + x, origin.y() + y);
                    total += (qAbs(qRed(s) - qRed(b)) + qAbs(qGreen(s) - qGreen(b))
                              + qAbs(qBlue(s) - qBlue(b))) / 3.0;
                    ++count;
                }
            return count ? total / count : 255.0;
        };
        const double width = layout.property("width").toNumber();
        const double height = layout.property("height").toNumber();
        QCOMPARE(qRound(board.width() * unitsPerPixel), qRound(width));
        QCOMPARE(qRound(board.height() * unitsPerPixel), qRound(height));
        const auto rect = layoutRect;
        const auto sprite = [&](const QString& name, const QRectF& placed, const QPointF& origin,
                                bool edgeIsBoard = false) {
            const QSize pixels = size(QStringLiteral("sprites/") + name);
            QVERIFY2(pixels.isValid(), qPrintable(name));
            QVERIFY2(qAbs(pixels.width() * unitsPerPixel - placed.width()) < 0.01
                     && qAbs(pixels.height() * unitsPerPixel - placed.height()) < 0.01, qPrintable(name));
            const QRectF onBoard = placed.translated(origin);
            QVERIFY2(QRectF(0, 0, width, height).contains(onBoard), qPrintable(name));
            if (edgeIsBoard) {
                const double error = edgeError(name, onBoard.topLeft());
                QVERIFY2(error < 4.0, qPrintable(QStringLiteral("%1 edge differs from the board by %2/255")
                                                     .arg(name).arg(error, 0, 'f', 1)));
            }
        };
        const std::array<std::tuple<const char*, const char*, int>, 3> parts{
            {{"switches", "switch", 16}, {"leds", "led", 16}, {"buttons", "button", 5}}};
        for (const auto& [group, prefix, count] : parts) {
            const QJSValue list = layout.property(QString::fromLatin1(group));
            QCOMPARE(list.property(QStringLiteral("length")).toInt(), count);
            for (int i = 0; i < count; ++i) {
                const QRectF part = rect(list.property(i).property(QStringLiteral("rect")));
                QVERIFY(part.width() > 0 && part.height() > 0);
                sprite(QStringLiteral("%1-%2.png").arg(QLatin1String(prefix)).arg(i),
                       rect(list.property(i).property(QStringLiteral("sprite"))), part.topLeft(), true);
            }
        }
        sprite(QStringLiteral("done.png"), rect(layout.property(QStringLiteral("done"))
                                                    .property(QStringLiteral("sprite"))), QPointF(), true);
        const QRectF display = rect(layout.property(QStringLiteral("display")).property(QStringLiteral("rect")));
        QVERIFY(!display.isEmpty() && QRectF(0, 0, width, height).contains(display));
        const QJSValue digits = layout.property(QStringLiteral("digits"));
        QCOMPARE(digits.property(QStringLiteral("length")).toInt(), 4);
        const QStringList segments{"a", "b", "c", "d", "e", "f", "g", "dp"};
        for (int d = 0; d < 4; ++d) {
            const QRectF cell = rect(digits.property(d).property(QStringLiteral("rect")));
            const QJSValue list = digits.property(d).property(QStringLiteral("segments"));
            QCOMPARE(list.property(QStringLiteral("length")).toInt(), 8);
            for (int s = 0; s < 8; ++s)
                sprite(QStringLiteral("segment-%1-%2.png").arg(d).arg(segments[s]),
                       rect(list.property(s).property(QStringLiteral("rect"))), cell.topLeft());
        }
    }

    // Each part shows its own sprite, where the layout puts it, exactly while the
    // model has the part in that state. The DONE LED is lit while connected, and
    // the bound display is not veiled.
    void spritesFollowPartState() {
        QJSEngine js;
        const QJSValue layout = boardLayout(js);
        QVERIFY(layout.isObject());
        const auto placed = [&](QQuickItem* part, const char* group, int row, const QString& file) {
            const QJSValue entry = layout.property(QString::fromLatin1(group)).property(row);
            auto* sprite = findItem(part, QStringLiteral("sprite"));
            return sprite && geometry(part) == layoutRect(entry.property(QStringLiteral("rect")))
                && geometry(sprite) == layoutRect(entry.property(QStringLiteral("sprite")))
                && sprite->property("source").toUrl().fileName() == file ? sprite : nullptr;
        };
        for (int row = 0; row < 16; ++row) {
            auto* sw = placed(control("sw", row), "switches", row, QStringLiteral("switch-%1.png").arg(row));
            QVERIFY2(sw, qPrintable(QStringLiteral("switch %1 sprite").arg(row)));
            QVERIFY(!sw->isVisible());
            QVERIFY(adapter_->setSwitch(row, true));
            QTRY_VERIFY(sw->isVisible());
            QVERIFY(adapter_->setSwitch(row, false));
            QTRY_VERIFY(!sw->isVisible());

            auto* led = placed(control("led", row), "leds", row, QStringLiteral("led-%1.png").arg(row));
            QVERIFY2(led, qPrintable(QStringLiteral("LED %1 sprite").arg(row)));
            pins_->values[PinEngine::Leds] = uint64_t{1} << (15 - row);
            QVERIFY(adapter_->refresh());
            for (int other = 0; other < 16; ++other)
                QCOMPARE(findItem(control("led", other), QStringLiteral("sprite"))->isVisible(), other == row);
        }
        pins_->values[PinEngine::Leds] = 0;
        QVERIFY(adapter_->refresh());
        for (int row = 0; row < 5; ++row) {
            auto* button = placed(control("button", row), "buttons", row, QStringLiteral("button-%1.png").arg(row));
            QVERIFY2(button, qPrintable(QStringLiteral("button %1 sprite").arg(row)));
            QVERIFY(!button->isVisible());
            QVERIFY(adapter_->setButton(row, true));
            QTRY_VERIFY(button->isVisible());
            QVERIFY(adapter_->setButton(row, false));
            QTRY_VERIFY(!button->isVisible());
        }

        const QStringList names{"a", "b", "c", "d", "e", "f", "g", "dp"};
        const auto pieces = [&](int digit) {
            auto* cell = control("digit", digit);
            QList<QQuickItem*> list;
            for (int bit = 0; bit < 7; ++bit) list.append(segment(cell, bit));
            list.append(findItem(cell, QStringLiteral("decimalPoint")));
            return list;
        };
        for (int digit = 0; digit < 4; ++digit) {
            const QJSValue cell = layout.property(QStringLiteral("digits")).property(digit);
            QCOMPARE(geometry(control("digit", digit)), layoutRect(cell.property(QStringLiteral("rect"))));
            const auto list = pieces(digit);
            for (int k = 0; k < 8; ++k) {
                QCOMPARE(list[k]->property("source").toUrl().fileName(),
                         QStringLiteral("segment-%1-%2.png").arg(digit).arg(names[k]));
                QCOMPARE(geometry(list[k]), layoutRect(cell.property(QStringLiteral("segments"))
                                                           .property(k).property(QStringLiteral("rect"))));
                QCOMPARE(list[k]->opacity(), 0.0);
            }
        }
        pins_->values[PinEngine::Anodes] = 0;
        for (int selected = 0; selected < 8; ++selected) {  // one piece at a time
            pins_->values[PinEngine::Segments] = selected < 7 ? (~(uint64_t{1} << selected)) & 127 : 127;
            pins_->values[PinEngine::Point] = selected == 7 ? 0 : 1;
            board_->tick(1000);
            QVERIFY(adapter_->refresh());
            for (int digit = 0; digit < 4; ++digit) {
                const auto list = pieces(digit);
                for (int k = 0; k < 8; ++k) QCOMPARE(list[k]->opacity(), k == selected ? 1.0 : 0.0);
            }
        }
        pins_->values[PinEngine::Segments] = 0; // every segment lit (active low)
        pins_->values[PinEngine::Point] = 0;
        board_->tick(1000);
        QVERIFY(adapter_->refresh());
        for (int digit = 0; digit < 4; ++digit)
            for (auto* piece : pieces(digit)) QCOMPARE(piece->opacity(), 1.0);

        QVERIFY(item(QStringLiteral("doneLed"))->isVisible());
        QVERIFY(!item(QStringLiteral("displayVeil"))->isVisible());
    }

    void keyboardFocusRevealsControlsInANarrowViewport() {
        window_->resize(960, 640);
        QTRY_COMPARE(window_->size(), QSize(960, 640));
        auto* viewport = item(QStringLiteral("boardFlickable"));
        QTRY_VERIFY(viewport->width() < viewport->property("contentWidth").toReal());
        QVERIFY(focusControl(control("sw", 0)));
        QVERIFY(viewport->property("contentX").toReal() > 0);
        QVERIFY(viewport->property("contentY").toReal() > 0);
        QVERIFY(focusControl(control("sw", 15)));
        QVERIFY(withinViewport(control("sw", 15)));
        QVERIFY(focusControl(control("button", 1)));
        QVERIFY(withinViewport(control("button", 1)));

        QSet<QString> visited;
        for (int step = 0; step < 80 && visited.size() < 21; ++step) {
            auto* focused = window_->activeFocusItem();
            if (focused && (focused->objectName().startsWith(QStringLiteral("sw"))
                            || focused->objectName().startsWith(QStringLiteral("button")))) {
                QTRY_VERIFY(withinViewport(focused));
                visited.insert(focused->objectName());
            }
            QTest::keyClick(window_, Qt::Key_Tab);
            QCoreApplication::processEvents();
        }
        QCOMPARE(visited.size(), 21);
        QCOMPARE(pins_->now(), uint64_t{0});
        QCOMPARE(pins_->steps, 0u);
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    BoardWidgetsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_board_widgets.moc"
