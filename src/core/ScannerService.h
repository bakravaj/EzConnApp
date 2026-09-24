#pragma once
#include "LegacyProtocol.h"
namespace ezconn {
inline constexpr quint8 CMD_SCANNER_RX_INJECT=9;
class ScannerService : public QObject {
    Q_OBJECT
public:
    static constexpr int MaxPayload=251; // P: 255-byte body minus four header bytes.
    explicit ScannerService(MachineSession& session,QObject* parent=nullptr,int timeoutMs=5000);
    void setReady(bool ready) { ready_=ready; }
    bool busy() const { return busy_; }
    void sendScannerPayload(const QByteArray& payload);
    static QByteArray preparePayload(const QString& text,bool hex,bool appendCRLF);
    static QString printable(const QByteArray& payload);
    static QString preset(int index);
signals:
    void busyChanged(bool busy);
    void logLine(const QString& message);
    void completed();
    void failed(const QString& message);
private:
    void receive(const QByteArray& bytes);
    void finish();
    void fail(const QString& message,bool disconnect=false);
    MachineSession& session_;
    LegacyCodec codec_;
    QTimer timer_;
    bool ready_=false,busy_=false;
};
}
