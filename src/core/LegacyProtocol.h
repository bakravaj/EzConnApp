#pragma once
#include "Connection.h"
namespace ezconn {
struct Packet { quint8 type=80, destination=66, sender=65, command=0, message=87; QByteArray data; };
class LegacyCodec {
public:
    static QByteArray encode(const Packet& packet);
    static QByteArray auth(const QByteArray& challenge);
    static QByteArray decompress(const QByteArray& bytes,int maxBytes=16*1024*1024);
    QList<Packet> feed(const QByteArray& bytes);
    void reset() { buffer_.clear(); }
private:
    QByteArray buffer_;
};
class VersionProbe : public QObject {
    Q_OBJECT
public:
    explicit VersionProbe(MachineSession& session, QObject* parent=nullptr);
    void start(bool serial=false);
signals:
    void completed(const QString& software, int protocol, quint8 options);
    void failed(const QString& reason);
private:
    void request(quint8 command, const QByteArray& data={});
    void receive(const QByteArray& bytes);
    void fail(const QString& reason);
    MachineSession& session_;
    LegacyCodec codec_;
    QTimer timer_;
    int expected_=0;
    QByteArray challenge_, software_;
    quint8 options_=0;
};
}
