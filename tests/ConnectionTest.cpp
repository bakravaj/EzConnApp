#include "core/Connection.h"
#include <QtTest>
#include <QTcpServer>
using namespace ezconn;
class SilentTransport final : public ITransport {
public:
    void open() override {}
    void close() override {}
    qint64 write(const QByteArray&) override { return -1; }
};
class ConnectionTest : public QObject {
    Q_OBJECT
private slots:
    void endpoints() {
        Endpoint e;
        QCOMPARE(e.host, QString("192.168.1.66")); QCOMPARE(e.port, quint16(2000));
        QVERIFY(Endpoint::parse("127.0.0.1:1234", e)); QCOMPARE(e.port, quint16(1234));
        QVERIFY(!Endpoint::parse("127.0.0.1:65536", e));
        QVERIFY(!Endpoint::parse("127.0.0.1:0", e));
        QVERIFY(!Endpoint::parse("bad:2000", e));
        QVERIFY(Endpoint::parse("[::1]:2000", e));
    }
    void tcpRoundtrip() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        MachineSession session;
        QSignalSpy incoming(&session, &MachineSession::received);
        QSignalSpy logs(&session, &MachineSession::logLine);
        QVERIFY(!session.send("offline"));
        session.start(std::make_unique<TcpTransport>(Endpoint{"127.0.0.1", server.serverPort()}));
        QTRY_COMPARE(session.state(), MachineSession::State::Connected);
        QTRY_VERIFY(server.hasPendingConnections());
        auto* peer = server.nextPendingConnection();
        QVERIFY(session.send(QByteArray::fromHex("007f80ff")));
        QTRY_COMPARE(peer->bytesAvailable(), qint64(4));
        QCOMPARE(peer->readAll(), QByteArray::fromHex("007f80ff"));
        peer->write(QByteArray::fromHex("5042")); peer->flush();
        QTRY_COMPARE(incoming.count(), 1);
        QCOMPARE(incoming.at(0).at(0).toByteArray(), QByteArray::fromHex("5042"));
        QVERIFY(logs.count() >= 4);
        session.stop(); QCOMPARE(session.state(), MachineSession::State::Disconnected);
        QVERIFY(!session.send("offline"));
        // Reconnect with a fresh transport, without retaining old callbacks.
        session.start(std::make_unique<TcpTransport>(Endpoint{"127.0.0.1", server.serverPort()}));
        QTRY_COMPARE(session.state(), MachineSession::State::Connected);
        session.stop();
    }
    void connectionRefused() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        const auto port = server.serverPort(); server.close();
        MachineSession session; QSignalSpy errors(&session, &MachineSession::failed);
        session.start(std::make_unique<TcpTransport>(Endpoint{"127.0.0.1", port}), 500);
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(session.state(), MachineSession::State::Disconnected);
    }
    void timeout() {
        MachineSession session; QSignalSpy errors(&session, &MachineSession::failed);
        session.start(std::make_unique<SilentTransport>(), 20);
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(session.state(), MachineSession::State::Disconnected);
    }
    void invalidSerial() {
        MachineSession session; QSignalSpy errors(&session, &MachineSession::failed);
        session.start(std::make_unique<SerialTransport>("EZCONN_NONEXISTENT_PORT", 9600));
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(session.state(), MachineSession::State::Disconnected);
    }
    void staleErrorAfterReconnect() {
        MachineSession session; QSignalSpy errors(&session, &MachineSession::failed);
        auto first = std::make_unique<SilentTransport>();
        auto* previous = first.get();
        session.start(std::move(first));
        emit previous->error("Old connection error");
        session.start(std::make_unique<SilentTransport>());
        QCoreApplication::processEvents();
        QCOMPARE(errors.count(), 0);
        QCOMPARE(session.state(), MachineSession::State::Connecting);
        session.stop();
    }
};
QTEST_GUILESS_MAIN(ConnectionTest)
#include "ConnectionTest.moc"
