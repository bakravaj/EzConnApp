#include "Connection.h"
#include <QDateTime>
#include <QHostAddress>

namespace ezconn {
bool Endpoint::parse(const QString& text, Endpoint& result) {
    const auto colon = text.lastIndexOf(':');
    if (colon <= 0) return false;
    QString host = text.left(colon);
    if (host.startsWith('[') && host.endsWith(']')) host = host.mid(1, host.size()-2);
    bool ok = false;
    const uint port = text.mid(colon+1).toUInt(&ok);
    QHostAddress address;
    if (!ok || !port || port > 65535 || !address.setAddress(host)) return false;
    result = {host, static_cast<quint16>(port)};
    return true;
}
TcpTransport::TcpTransport(Endpoint endpoint) : endpoint_(std::move(endpoint)) {
    connect(&socket_, &QTcpSocket::connected, this, &ITransport::connected);
    connect(&socket_, &QTcpSocket::disconnected, this, &ITransport::disconnected);
    connect(&socket_, &QTcpSocket::readyRead, this, [this] { emit received(socket_.readAll()); });
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](auto) { emit error(socket_.errorString()); });
}
void TcpTransport::open() { socket_.connectToHost(endpoint_.host, endpoint_.port); }
void TcpTransport::close() { socket_.abort(); }
qint64 TcpTransport::write(const QByteArray& data) { return socket_.write(data); }

SerialTransport::SerialTransport(QString port, qint32 baud) : baud_(baud) {
    port_.setPortName(port);
    connect(&port_, &QSerialPort::readyRead, this, [this] { emit received(port_.readAll()); });
    connect(&port_, &QSerialPort::errorOccurred, this, [this](auto code) {
        if (code != QSerialPort::NoError) emit error(port_.errorString());
    });
}
void SerialTransport::open() {
    if (baud_ <= 0) { emit error(QStringLiteral("Serial baud rate must be positive")); return; }
    if (!port_.setBaudRate(baud_) || !port_.setDataBits(QSerialPort::Data8)
        || !port_.setParity(QSerialPort::NoParity) || !port_.setStopBits(QSerialPort::OneStop)
        || !port_.setFlowControl(QSerialPort::NoFlowControl)) return;
    if (!port_.open(QIODevice::ReadWrite)) return;
    if (!port_.setDataTerminalReady(true)) { close(); return; }
    emit connected();
}
void SerialTransport::close() {
    if (port_.isOpen()) { port_.close(); emit disconnected(); }
}
qint64 SerialTransport::write(const QByteArray& data) { return port_.write(data); }

MachineSession::MachineSession(QObject* parent) : QObject(parent) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] {
        log("ERROR", "Connection timed out"); stop(); emit failed("Connection timed out");
    });
}
MachineSession::~MachineSession() {
    if (transport_) { transport_->disconnect(this); transport_->close(); }
}
void MachineSession::log(const QString& category, const QString& text) {
    emit logLine(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + " " + category + " " + text);
}
void MachineSession::setState(State state) {
    if (state_ == state) return;
    state_ = state;
    log("STATE", state == State::Connected ? "connected" : state == State::Connecting ? "connecting" : "disconnected");
    emit stateChanged(state);
}
void MachineSession::start(std::unique_ptr<ITransport> transport, int timeoutMs) {
    stop();
    const auto generation = ++generation_;
    if (transport_) transport_->disconnect(this);
    transport_ = std::move(transport);
    if (!transport_) { emit failed("No transport"); return; }
    connect(transport_.get(), &ITransport::connected, this, [this] {
        timer_.stop(); setState(State::Connected);
    });
    connect(transport_.get(), &ITransport::disconnected, this, [this] {
        timer_.stop(); setState(State::Disconnected);
    });
    connect(transport_.get(), &ITransport::received, this, [this](const QByteArray& data) {
        if(!wireLogging_) { emit received(data); return; }
        QString text=QString::fromLatin1(data.left(128).toHex(' ').toUpper());
        if(data.size()>128) text+=QString(" ... [%1 bytes; showing first 128]").arg(data.size());
        log("RX",text); emit received(data);
    });
    // Queued handling avoids closing a serial port inside its open/error callback.
    connect(transport_.get(), &ITransport::error, this, [this, generation](const QString& message) {
        if (generation != generation_ || state_ == State::Disconnected) return;
        log("ERROR", message); stop(); emit failed(message);
    }, Qt::QueuedConnection);
    setState(State::Connecting);
    timer_.start(qMax(1, timeoutMs));
    transport_->open();
}
void MachineSession::stop() {
    timer_.stop();
    if (transport_) transport_->close();
    setState(State::Disconnected);
}
bool MachineSession::send(const QByteArray& data) {
    if (state_ != State::Connected || data.isEmpty()) return false;
    const auto accepted = transport_->write(data);
    if (accepted > 0 && wireLogging_) log("TX-QUEUED", QString::fromLatin1(data.left(accepted).toHex(' ').toUpper()));
    if (accepted != data.size()) {
        log("ERROR", "Incomplete write; closing session"); stop(); emit failed("Incomplete write"); return false;
    }
    return true;
}
}
