#include "pages/armedhud.h"
#include "ui/mainwindow.h"
#include "ui/sidenav.h"
#include "ui/theme.h"
#include "ui/widgets/hudcanvas.h"
#include "ui/widgets/keyboardview.h"
#include "ui/widgets/keycodepalette.h"
#include "virtualkeyboard.h"

#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

// test_gui.py and test_hud_canvas.py: the window against a virtual keyboard, and the HUD editor's surface
namespace {

const QByteArray FAKE_KEYBOARD = R"({"name":"Test","matrix":{"rows":2,"cols":2},"layouts":{"keymap":[["0,0","0,1"],["1,0","1,1"]]}})";

QTemporaryDir *configHome()
{
    static QTemporaryDir dir;
    return &dir;
}

hid::DeviceInfo fakeInfo()
{
    hid::DeviceInfo d;
    d.path = "/magic/path/for/tests";
    d.vid = 0xDEAD;
    d.pid = 0xBEEF;
    d.serial = "vial:f64c2b3c";
    d.manufacturer = "Vial Testing Ltd";
    d.product = "Test Keyboard";
    return d;
}

struct Window {
    VirtualKeyboard vk;
    MainWindow mw;

    explicit Window(std::function<void(VirtualKeyboard &)> setup = {})
        : vk(FAKE_KEYBOARD, 2, 2)
    {
        if (setup)
            setup(vk);
        mw.show();
        mw.controller()->setFakeDevices({fakeInfo()}, [this](const hid::DeviceInfo &) { return vk.open(); });
    }
    bool loaded() { return mw.controller()->keyboard() != nullptr; }

    void go(const QString &title)
    {
        auto *nav = mw.findChild<SideNav *>();
        for (int i = 0; i < nav->count(); ++i)
            if (nav->item(i)->text().compare(title, Qt::CaseInsensitive) == 0)
                nav->setCurrentRow(i);
    }
    template <class T>
    T *visible()
    {
        for (T *w : mw.findChildren<T *>())
            if (w->isVisible() && !qobject_cast<SideNav *>(w))
                return w;
        return nullptr;
    }
};

}

class TestGui : public QObject {
    Q_OBJECT

public:
    static void initMain()
    {
        // settings, caches and the agent's lock away from the real ones
        const QByteArray home = configHome()->path().toLocal8Bit();
        qputenv("XDG_CONFIG_HOME", home);
        qputenv("APPDATA", home);
        qputenv("HOME", home);
    }

private:
    // the canvas at 2×, as test_hud_canvas.py sets it up
    struct Canvas {
        hud::Layout hud{{{.type = hud::W_TEXT, .x = 10, .y = 10, .w = 40, .h = 14},
                         {.type = hud::W_BAR, .x = 100, .y = 50, .w = 60, .h = 12}}};
        HudCanvas c;
        QList<std::optional<hud::Box>> areas;
        Canvas()
        {
            c.setScreen(320, 172);
            c.setScale(2);
            QObject::connect(&c, &HudCanvas::edited, &c, [this] { refresh(); });
            refresh();
            c.show();
        }
        void refresh()
        {
            areas.clear();
            for (const auto &w : hud.widgets)
                areas.append(hud::Box{w.x, w.y, w.x + w.w - 1, w.y + w.h - 1});
            QImage img(320, 172, QImage::Format_RGB888);
            img.fill(Qt::black);
            c.setContent(img, &hud, areas);
        }
        static QPoint at(int x, int y) { return {1 + x * 2 + 1, 1 + y * 2 + 1}; }
        // QTest::mouseMove doesn't reach widgets on the offscreen platform
        void move(const QPoint &p, Qt::KeyboardModifiers mods = Qt::NoModifier)
        {
            QMouseEvent e(QEvent::MouseMove, p, c.mapToGlobal(p), Qt::NoButton, Qt::LeftButton, mods);
            QApplication::sendEvent(&c, &e);
        }
        void paint() { c.grab(); }
    };

private slots:
    void initTestCase()
    {
        ui::theme::loadFonts();
        ui::theme::apply();
    }

    void startup()
    {
        Window w;
        QTRY_VERIFY(w.loaded());
        auto *devices = w.mw.findChild<QComboBox *>();
        QCOMPARE(devices->currentText(), QStringLiteral("Vial Testing Ltd Test Keyboard"));
        QCOMPARE(w.mw.controller()->keyboard()->layers, 4);
    }

    void keyChange()
    {
        Window w;
        QTRY_VERIFY(w.loaded());
        w.go("Keymap");
        auto *view = w.visible<KeyboardView>();
        auto *palette = w.visible<KeycodePalette>();
        QVERIFY(view && palette);
        view->setActive(0);
        emit palette->keycodeSelected("KC_A");
        QTRY_COMPARE(w.vk.key(0, 0, 0), 0x04);
        QCOMPARE(w.mw.controller()->keyboard()->key(0, 0, 0), QStringLiteral("KC_A"));
        // the same key again sends nothing
        view->setActive(0);
        emit palette->keycodeSelected("KC_A");
        QTest::qWait(100);
        QCOMPARE(w.vk.writes(), QList<QByteArray>{QByteArray::fromHex("050000000004")});
    }

    void layerSwitch()
    {
        Window w([](VirtualKeyboard &vk) {
            for (int l = 0; l < 4; ++l)
                vk.key(l, 0, 0) = quint16(0x04 + l);
        });
        QTRY_VERIFY(w.loaded());
        w.go("Keymap");
        QPushButton *layer2 = nullptr;
        for (auto *b : w.mw.findChildren<QPushButton *>())
            if (b->isVisible() && b->toolTip().startsWith("Layer 2"))
                layer2 = b;
        QVERIFY(layer2);
        layer2->click();
        w.visible<KeyboardView>()->setActive(0);
        emit w.visible<KeycodePalette>()->keycodeSelected("KC_Z");
        QTRY_COMPARE(w.vk.key(2, 0, 0), 0x1D);
        QCOMPARE(w.vk.key(0, 0, 0), 0x04);
        QCOMPARE(w.vk.key(1, 0, 0), 0x05);
        QCOMPARE(w.vk.key(3, 0, 0), 0x07);
    }

    void combos()
    {
        Window w([](VirtualKeyboard &vk) {
            vk.combos = {{0x04, 0x05, 0, 0, 0x06}, {0, 0, 0, 0, 0}};
        });
        QTRY_VERIFY(w.loaded());
        w.go("Combos");
        QListWidget *list = nullptr;
        QTRY_VERIFY((list = w.visible<QListWidget>()) && list->count() == 2);
        QVERIFY2(list->item(0)->text().contains("A + B") && list->item(0)->text().contains("C"),
                 qPrintable(list->item(0)->text()));
        QVERIFY(list->item(1)->text().endsWith("—"));
    }

    void tapDance()
    {
        Window w([](VirtualKeyboard &vk) {
            vk.tapDances = {{0x04, 0x05, 0x06, 0x07, 200}};
        });
        QTRY_VERIFY(w.loaded());
        w.go("Tap Dance");
        QListWidget *list = nullptr;
        QTRY_VERIFY((list = w.visible<QListWidget>()) && list->count() == 1);
        const QString t = list->item(0)->text();
        QVERIFY2(t.contains("tap A") && t.contains("hold B") && t.contains("2×tap C") && t.contains("tap+hold D"),
                 qPrintable(t));
    }

    void canvasClickEmptyAndBoxSelect()
    {
        Canvas cv;
        QTest::mousePress(&cv.c, Qt::LeftButton, Qt::NoModifier, Canvas::at(200, 150));
        QCOMPARE(cv.c.selection(), QList<int>{});
        cv.paint();
        cv.move(Canvas::at(5, 5));
        cv.paint();
        QList<int> sel = cv.c.selection();
        std::sort(sel.begin(), sel.end());
        QCOMPARE(sel, (QList<int>{0, 1}));
        QTest::mouseRelease(&cv.c, Qt::LeftButton, Qt::NoModifier, Canvas::at(5, 5));
        cv.paint();
    }

    void canvasSelectMoveSnapResize()
    {
        Canvas cv;
        QTest::mouseClick(&cv.c, Qt::LeftButton, Qt::NoModifier, Canvas::at(20, 15));
        QCOMPARE(cv.c.selection(), QList<int>{0});
        QTest::mousePress(&cv.c, Qt::LeftButton, Qt::NoModifier, Canvas::at(20, 15));
        cv.move(Canvas::at(108, 17)); // the left edge lands 2 px off the bar's: snaps
        cv.paint();
        QTest::mouseRelease(&cv.c, Qt::LeftButton, Qt::NoModifier, Canvas::at(108, 17));
        QCOMPARE(cv.hud.widgets[0].x, 100);
        const hud::Box a = *cv.areas[0];
        QTest::mousePress(&cv.c, Qt::LeftButton, Qt::NoModifier, Canvas::at(a.x1, a.y1));
        cv.move(Canvas::at(a.x1 + 20, a.y1 + 5));
        QTest::mouseRelease(&cv.c, Qt::LeftButton, Qt::NoModifier, Canvas::at(a.x1 + 20, a.y1 + 5));
        QVERIFY(cv.hud.widgets[0].w > 40);
        cv.paint();
    }

    void canvasModifiersKeysContext()
    {
        Canvas cv;
        QTest::mouseClick(&cv.c, Qt::LeftButton, Qt::NoModifier, Canvas::at(20, 15));
        QTest::mouseClick(&cv.c, Qt::LeftButton, Qt::ShiftModifier, Canvas::at(110, 55));
        QCOMPARE(cv.c.selection(), (QList<int>{0, 1}));
        QTest::mouseClick(&cv.c, Qt::LeftButton, Qt::ControlModifier, Canvas::at(20, 15));
        QCOMPARE(cv.c.selection(), QList<int>{1});
        QList<QPoint> nudges;
        connect(&cv.c, &HudCanvas::nudge, this, [&](int dx, int dy) { nudges.append({dx, dy}); });
        QTest::keyClick(&cv.c, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(nudges, QList<QPoint>{QPoint(-8, 0)});
        int menus = 0;
        connect(&cv.c, &HudCanvas::contextRequested, this, [&] { ++menus; });
        QTest::mouseClick(&cv.c, Qt::RightButton, Qt::NoModifier, Canvas::at(20, 15));
        QCOMPARE(cv.c.selection(), QList<int>{0});
        QCOMPARE(menus, 1);
        QList<int> hits;
        connect(&cv.c, &HudCanvas::activated, this, [&](int i) { hits.append(i); });
        QTest::mouseDClick(&cv.c, Qt::LeftButton, Qt::NoModifier, Canvas::at(20, 15));
        QCOMPARE(hits, QList<int>{0});
        cv.c.showBounds = true;
        cv.paint();
    }

    void hudListDragReorder()
    {
        Controller ctl;
        HudEditorPage page(&ctl);
        for (int i = 0; i < 3; ++i)
            page.hud_.widgets.append({.type = hud::W_TEXT, .label = QString::number(i)});
        page.refreshList();
        // the list shows the front widget first; a drop takes the item and inserts it elsewhere
        QListWidgetItem *item = page.list_->takeItem(0);
        page.list_->insertItem(2, item);
        QTRY_COMPARE(page.undo_.size(), 1);
        QStringList labels;
        for (const auto &w : page.hud_.widgets)
            labels << *w.label;
        QCOMPARE(labels, (QStringList{"2", "0", "1"}));
    }
};

QTEST_MAIN(TestGui)
#include "tst_gui.moc"
