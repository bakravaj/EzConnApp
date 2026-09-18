#pragma once
#include "LegacyProtocol.h"
#include <QImage>
#include <QElapsedTimer>
namespace ezconn {
class ScreenService : public QObject {
    Q_OBJECT
public:
    explicit ScreenService(MachineSession& session,QObject* parent=nullptr,int responseWaitMs=30000,int blockIdleMs=15000);
    void start(bool continuous);
    void stop();
    static QImage decodePcx(const QByteArray& bytes,const QImage& previous={});
signals:
    void frameReady(const QImage& image);
    void activeChanged(bool active);
    void failed(const QString& error);
    void status(const QString& message);
    void statistics(const QString& message);
private:
    void request(quint8 command,const QByteArray& data);
    void receive(const QByteArray& bytes);
    void fail(const QString& error);
    void nextBlock();
    MachineSession& session_;
    LegacyCodec codec_;
    QTimer timeout_,poll_;
    QElapsedTimer firstFrameWait_;
    QElapsedTimer requestWait_;
    QElapsedTimer cycle_,transfer_,fpsClock_;
    qint64 captureMs_=0;
    int frameCount_=0;
    static constexpr int blockKiB_=250;
    int responseWaitMs_,blockIdleMs_;
    bool active_=false,continuous_=false,stopping_=false;
    int expected_=0,block_=0,blocks_=0;
    QByteArray frame_;
    QImage image_;
};
}
