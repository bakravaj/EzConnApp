#include "gui/ScreenView.h"
#include "gui/RemoteKeyFilter.h"
#include <QtTest>
class ScreenViewTest:public QObject {
    Q_OBJECT
private slots:
    void keysOnlyInActiveStreamWindow() {
        QWidget window; ScreenView view(&window); window.resize(400,300); view.resize(400,300);
        QList<quint8> keys; RemoteKeyFilter filter(&window,[&](quint8 key){keys.append(key);});
        window.show(); window.activateWindow(); view.setFocus(); QTRY_VERIFY(window.isActiveWindow());
        QTest::keyClick(&view,Qt::Key_F1); QVERIFY(keys.isEmpty());
        filter.setActive(true); QTest::keyClick(&view,Qt::Key_F1); QTest::keyClick(&view,Qt::Key_Return);
        QCOMPARE(keys,QList<quint8>({0x70,0x0d}));
        QTest::keyClick(&view,Qt::Key_A); QTest::keyClick(&view,Qt::Key_Delete); QTest::keyClick(&view,Qt::Key_5);
        QCOMPARE(keys,QList<quint8>({0x70,0x0d,0x41,0x2e,0x35}));
        QTest::keyClick(&view,Qt::Key_F2,Qt::ControlModifier);
        QKeyEvent repeat(QEvent::KeyPress,Qt::Key_F1,Qt::NoModifier,QString(),true); QApplication::sendEvent(&view,&repeat);
        QCOMPARE(keys.size(),5);
        QWidget other; other.show(); other.activateWindow(); other.setFocus(); QTRY_VERIFY(other.isActiveWindow());
        QTest::keyClick(&other,Qt::Key_F3); QCOMPARE(keys.size(),5);
    }
    void touchCoordinates() {
        ScreenView view; QVERIFY(!view.framebufferPoint(QPointF(0,0)));
        QImage frame(1000,600,QImage::Format_RGB32); frame.fill(Qt::red); view.setFrame(frame);
        view.resize(1000,600); QCOMPARE(view.framebufferPoint(QPointF(500,300)).value(),QPoint(500,300));
        view.resize(500,300); QCOMPARE(view.framebufferPoint(QPointF(250,150)).value(),QPoint(500,300));
        view.resize(2000,1200); QCOMPARE(view.framebufferPoint(QPointF(1000,600)).value(),QPoint(500,300));
        QCOMPARE(view.framebufferPoint(QPointF(1999.9,1199.9)).value(),QPoint(999,599));
        view.resize(1000,1000); QCOMPARE(view.imageRect(),QRect(0,200,1000,600));
        QCOMPARE(view.framebufferPoint(QPointF(500,500)).value(),QPoint(500,300));
        QVERIFY(!view.framebufferPoint(QPointF(500,199.9))); QVERIFY(!view.framebufferPoint(QPointF(500,800)));
        view.resize(1200,600); QCOMPARE(view.imageRect(),QRect(100,0,1000,600));
        QVERIFY(!view.framebufferPoint(QPointF(99.9,0))); QVERIFY(!view.framebufferPoint(QPointF(1100,0)));
        QCOMPARE(view.framebufferPoint(QPointF(100,0)).value(),QPoint(0,0));
        QList<QPoint> clicks; view.touch=[&](QPoint p){clicks.append(p);};
        QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,QPoint(600,300));
        QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,QPoint(50,300));
        QTest::mouseClick(&view,Qt::RightButton,Qt::NoModifier,QPoint(600,300));
        QCOMPARE(clicks,QList<QPoint>({QPoint(500,300)}));
    }
    void fitAndResize() {
        ScreenView view; QImage frame(1280,1024,QImage::Format_RGB32); frame.fill(Qt::red);
        view.setFrame(frame); view.resize(1000,600);
        QCOMPARE(view.imageRect(),QRect(125,0,750,600));
        view.resize(400,800); QCOMPARE(view.imageRect(),QRect(0,240,400,320));
        view.resize(2560,2048); QCOMPARE(view.imageRect(),QRect(0,0,2560,2048));
        view.resize(1000,600);
        const auto rendered=view.grab().toImage();
        QCOMPARE(rendered.pixelColor(0,0),QColor(Qt::black));
        QCOMPARE(rendered.pixelColor(500,300),QColor(Qt::red));
    }
};
QTEST_MAIN(ScreenViewTest)
#include "ScreenViewTest.moc"
