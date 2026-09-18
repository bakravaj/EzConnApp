#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QSerialPort>
#include <QTimer>
#include <memory>

namespace ezconn {
inline constexpr auto DefaultHost = "192.168.1.66";
inline constexpr quint16 DefaultPort = 2000;
struct Endpoint {
    QString host = QString::fromLatin1(DefaultHost);
    quint16 port = DefaultPort;
    static bool parse(const QString& text, Endpoint& result);
};
class ITransport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual qint64 write(const QByteArray& data) = 0;
signals:
    void connected();
    void disconnected();
    void received(const QByteArray& data);
    void error(const QString& message);
};
class TcpTransport final : public ITransport {
public:
    explicit TcpTransport(Endpoint endpoint);
    void open() override;
    void close() override;
    qint64 write(const QByteArray& data) override;
private:
    Endpoint endpoint_;
    QTcpSocket socket_;
};
class SerialTransport final : public ITransport {
public:
    SerialTransport(QString port, qint32 baud);
    void open() override;
    void close() override;
    qint64 write(const QByteArray& data) override;
private:
    QSerialPort port_;
    qint32 baud_;
};
class MachineSession final : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected };
    Q_ENUM(State)
    explicit MachineSession(QObject* parent = nullptr);
    ~MachineSession() override;
    void start(std::unique_ptr<ITransport> transport, int timeoutMs = 5000);
    void stop();
    bool send(const QByteArray& data);
    void setWireLogging(bool enabled) { wireLogging_=enabled; }
    State state() const { return state_; }
signals:
    void stateChanged(ezconn::MachineSession::State state);
    void logLine(const QString& line);
    void received(const QByteArray& data);
    void failed(const QString& message);
private:
    void setState(State state);
    void log(const QString& category, const QString& text);
    std::unique_ptr<ITransport> transport_;
    QTimer timer_;
    State state_ = State::Disconnected;
    quint64 generation_ = 0;
    bool wireLogging_=true;
};
}
