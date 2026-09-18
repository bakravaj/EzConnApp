#include "gui/ScreenView.h"
#include <QtTest>
class ScreenViewTest:public QObject {
    Q_OBJECT
private slots:
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
