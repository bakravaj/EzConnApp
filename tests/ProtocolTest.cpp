#include "core/LegacyProtocol.h"
#include "core/ReadServices.h"
#include "core/ScreenService.h"
#include "core/RemoteKey.h"
#include "core/ScannerService.h"
#include <QtTest>
#include <QProcess>
#include <QCoreApplication>
#include <QTcpServer>
#include <QFile>
#include <QRandomGenerator>
using namespace ezconn;
static QByteArray tinyPcx(quint8 r=10,quint8 g=20,quint8 b=30) {
    QByteArray data(128,char(0)); data[0]=10; data[2]=1; data[3]=8; data[65]=3; data[66]=1; data[70]=1; data[72]=1;
    for(auto c:{r,g,b}) { if(c>=192) data.append(char(193)); data.append(char(c)); }
    return data;
}
class SimulatedMachine final : public ITransport {
public:
    QList<int> commands;
    QList<Packet> requests;
    LegacyCodec decoder;
    bool wrongAuth=false;
    bool multiScreen=false;
    int screenPendingReplies=0;
    int screenDelayMs=0;
    bool dropScreenReply=false;
    bool slowBlockFragments=false;
    int keyReplyDelayMs=0;
    bool dropKeyReply=false;
    QByteArray keyReplyData=QByteArray(1,char(0));
    quint8 keyReplyMessage=87;
    int scannerError=0;
    bool dropScannerReply=false;
    QByteArray screenFile() const {
        auto source=tinyPcx();
        if(multiScreen) { QRandomGenerator random(123); while(source.size()<260*1024) source.append(char(random.generate()&255)); }
        return qCompress(source).mid(4);
    }
    void open() override { emit connected(); }
    void close() override { emit disconnected(); }
    qint64 write(const QByteArray& bytes) override {
        for(const auto& p:decoder.feed(bytes)) {
            commands.append(p.command);
            requests.append(p);
            Packet reply; reply.destination=65; reply.sender=66; reply.command=p.command; reply.message=82;
            if(p.command==1) reply.data=wrongAuth ? QByteArray("00000000") : LegacyCodec::auth(p.data);
            if(p.command==14) reply.data="Kaparossa simulator";
            if(p.command==38) reply.data=QByteArray(1,char(0x20));
            if(p.command==3) reply.data=QByteArray(1,char(2));
            // Actual Kaparossa reply observed for virtual keyboard command.
            if(p.command==66) { reply.message=keyReplyMessage; reply.data=keyReplyData; }
            if(p.command==9) { reply.message=scannerError ? 69 : 87; if(scannerError) reply.data=QByteArray(1,char(scannerError)); }
            if(p.command==23) reply.data=".\\\n..\\\nConfig\\\nProgram.RTB\n";
            if(p.command==45) reply.data=QByteArray::fromHex("7856341200100000000010000000080000000000");
            if(p.command==67) { auto kib=(screenFile().size()+1023)/1024; reply.data.append(char(kib&255)); reply.data.append(char(kib>>8)); }
            if(p.command==67 && screenPendingReplies>0) { --screenPendingReplies; reply.data=QByteArray(1,char(1)); }
            if(p.command==26) {
                reply.type=88;
                const int blockBytes=quint8(p.data.back())*1024;
                reply.data=screenFile().mid(quint8(p.data[0])*blockBytes,blockBytes);
            }
            auto wire=LegacyCodec::encode(reply);
            if(p.command==9 && dropScannerReply) continue;
            if(p.command==67 && dropScreenReply) continue;
            if(p.command==66 && dropKeyReply) continue;
            if(p.command==26 && slowBlockFragments) {
                QTimer::singleShot(0,this,[this,wire]{emit received(wire.left(2));});
                QTimer::singleShot(70,this,[this,wire]{emit received(wire.mid(2,2));});
                QTimer::singleShot(140,this,[this,wire]{emit received(wire.mid(4));});
                continue;
            }
            QTimer::singleShot(p.command==67 ? screenDelayMs : p.command==66 ? keyReplyDelayMs : 0,this,[this,wire]{ emit received(wire.left(2)); emit received(wire.mid(2)); });
        }
        return bytes.size();
    }
};
class ProtocolTest:public QObject {
    Q_OBJECT
private slots:
    void scannerPayloads() {
        const auto sample=ScannerService::preset(1);
        const auto data=ScannerService::preparePayload(sample,false,true);
        QCOMPARE(data,sample.toLatin1()+QByteArray("\r\n",2));
        QVERIFY(data.endsWith(QByteArray::fromHex("40433735400d0a")));
        QCOMPARE(ScannerService::preparePayload(sample+"\r\n",false,true),data);
        QCOMPARE(ScannerService::preparePayload(sample+"\n",false,true),data);
        QCOMPARE(ScannerService::preparePayload("42 46 32 44 00 FF 0D 0A",true,true),QByteArray::fromHex("4246324400ff0d0a"));
        QCOMPARE(ScannerService::preparePayload("41",true,false),QByteArray("A"));
        QVERIFY_EXCEPTION_THROWN(ScannerService::preparePayload("",false,true),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ScannerService::preparePayload(" ",true,true),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ScannerService::preparePayload("GG",true,false),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ScannerService::preparePayload("A",true,false),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ScannerService::preparePayload(QString(QChar(0x410)),false,false),std::runtime_error);
        QCOMPARE(ScannerService::preparePayload(QString(249,'A'),false,true).size(),251);
        QVERIFY_EXCEPTION_THROWN(ScannerService::preparePayload(QString(250,'A'),false,true),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ScannerService::preparePayload(QString(252,'A'),false,false),std::runtime_error);
        for(int i=0;i<3;++i) QVERIFY(ScannerService::preparePayload(ScannerService::preset(i),false,true).size()<=251);
    }
    void scannerSendAndOldFirmware() {
        MachineSession session; ScannerService scanner(session); ReadServices reads(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); session.start(std::move(machine));
        QSignalSpy done(&scanner,&ScannerService::completed),errors(&scanner,&ScannerService::failed),logs(&scanner,&ScannerService::logLine);
        const auto payload=ScannerService::preparePayload(ScannerService::preset(1),false,true);
        scanner.sendScannerPayload(payload); QVERIFY(raw->commands.isEmpty()); // Auth gate.
        scanner.setReady(true);
        scanner.sendScannerPayload({}); scanner.sendScannerPayload(QByteArray(252,'A')); QVERIFY(raw->commands.isEmpty());
        scanner.sendScannerPayload(payload); scanner.sendScannerPayload(payload); // No duplicate while busy.
        QTRY_COMPARE(done.count(),1);
        QCOMPARE(raw->commands,QList<int>({9})); QCOMPARE(raw->requests[0].command,CMD_SCANNER_RX_INJECT);
        QCOMPARE(raw->requests[0].data,payload); QVERIFY(!scanner.busy()); QVERIFY(logs[0][0].toString().contains("<CR><LF>"));
        raw->scannerError=3; errors.clear(); scanner.sendScannerPayload(payload); QTRY_COMPARE(errors.count(),1);
        QCOMPARE(errors[0][0].toString(),QString("This Rotor108 firmware does not support Scanner RX Inject (command 9)."));
        QCOMPARE(session.state(),MachineSession::State::Connected);
        reads.setReady(true); QSignalSpy disk(&reads,&ReadServices::diskReady); reads.readDisk("D:\\"); QTRY_COMPARE(disk.count(),1);
        QCOMPARE(raw->commands,QList<int>({9,9,45}));
        session.stop();
    }
    void scannerTimeoutDoesNotRetry() {
        MachineSession session; ScannerService scanner(session,nullptr,50);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); raw->dropScannerReply=true;
        session.start(std::move(machine)); scanner.setReady(true);
        QSignalSpy errors(&scanner,&ScannerService::failed);
        scanner.sendScannerPayload(QByteArray::fromHex("00ff0d0a"));
        QTRY_COMPARE(errors.count(),1);
        QCOMPARE(raw->commands,QList<int>({9})); QCOMPARE(raw->requests[0].data,QByteArray::fromHex("00ff0d0a"));
        QCOMPARE(session.state(),MachineSession::State::Disconnected); QVERIFY(!scanner.busy());
        QVERIFY(errors[0][0].toString().contains("not retried"));
    }
    void profilerReply_data() {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<int>("marker");
        QTest::addColumn<QString>("line");
        QTest::addColumn<int>("warnings");
        QTest::newRow("legacy") << QByteArray::fromHex("00") << 87 << QString() << 0;
        QTest::newRow("short") << QByteArray::fromHex("6402a50200") << 87 << QString() << 0;
        QTest::newRow("valid") << QByteArray::fromHex("040136011103") << 87
            << QString("Profiler PLANES: P0=260 ms | P1=310 ms | P2=785 ms | SUM=1355 ms") << 0;
        QTest::newRow("unsigned") << QByteArray::fromHex("00800180ffff") << 87
            << QString("Profiler PLANES: P0=32768 ms | P1=32769 ms | P2=65535 ms | SUM=131072 ms") << 0;
        QTest::newRow("maximum-sum") << QByteArray::fromHex("ffffffffffff") << 87
            << QString("Profiler PLANES: P0=65535 ms | P1=65535 ms | P2=65535 ms | SUM=196605 ms") << 0;
        QTest::newRow("zero") << QByteArray(6,char(0)) << 87
            << QString("Profiler PLANES: P0=0 ms | P1=0 ms | P2=0 ms | SUM=0 ms") << 0;
        QTest::newRow("descending-independent") << QByteArray::fromHex("0a0005000000") << 87
            << QString("Profiler PLANES: P0=10 ms | P1=5 ms | P2=0 ms | SUM=15 ms") << 0;
        QTest::newRow("mixed-independent") << QByteArray::fromHex("0a0005001400") << 87
            << QString("Profiler PLANES: P0=10 ms | P1=5 ms | P2=20 ms | SUM=35 ms") << 0;
        QTest::newRow("small-copy") << QByteArray::fromHex("6402a5020100") << 87
            << QString("Profiler PLANES: P0=612 ms | P1=677 ms | P2=1 ms | SUM=1290 ms") << 0;
        QTest::newRow("extra") << QByteArray::fromHex("6402a502000000") << 87 << QString() << 1;
        QTest::newRow("other-marker") << QByteArray::fromHex("6402a5020000") << 82 << QString() << 1;
    }
    void profilerReply() {
        QFETCH(QByteArray,data); QFETCH(int,marker); QFETCH(QString,line); QFETCH(int,warnings);
        MachineSession session; ScreenService screen(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get();
        raw->keyReplyData=data; raw->keyReplyMessage=quint8(marker);
        session.start(std::move(machine));
        QSignalSpy diagnostics(&screen,&ScreenService::profilerLog),frames(&screen,&ScreenService::frameReady),errors(&screen,&ScreenService::failed);
        screen.start(true); QVERIFY(screen.queueKey(0x70));
        QTRY_VERIFY(frames.count()>=2);
        QCOMPARE(errors.count(),0);
        QCOMPARE(diagnostics.count(),warnings+(line.isEmpty()?0:1));
        if(warnings) QVERIFY(diagnostics[0][0].toString().contains("debug warning"));
        if(!line.isEmpty()) QCOMPARE(diagnostics.last()[0].toString(),line);
        QCOMPARE(raw->commands.mid(0,5),QList<int>({67,26,66,67,26}));
        QCOMPARE(raw->commands.count(66),1); // No profiler polling.
        QCOMPARE(raw->requests[2].data,QByteArray::fromHex("000000007000"));
        screen.stop(); session.stop();
    }
    void remoteKeyPayloads() {
        for(int i=0;i<10;++i) QCOMPARE(RemoteKey::fromQt(Qt::Key_0+i).value(),quint8(0x30+i));
        for(int i=0;i<26;++i) QCOMPARE(RemoteKey::fromQt(Qt::Key_A+i).value(),quint8(0x41+i));
        QCOMPARE(RemoteKey::fromQt(Qt::Key_Delete).value(),quint8(0x2e));
        QCOMPARE(RemoteKey::keyboardPacket(0x2e).data.toHex(),QByteArray("000000002e00"));
        QCOMPARE(RemoteKey::keyboardPacket(0x41).data.toHex(),QByteArray("000000004100"));
        for(int i=0;i<10;++i) {
            const auto packet=RemoteKey::keyboardPacket(quint8(0x30+i));
            QCOMPARE(packet.command,quint8(66));
            QCOMPARE(packet.data.left(5),QByteArray(5,char(0)));
            QCOMPARE(quint8(packet.data[5]),quint8(0x30+i));
        }
        QCOMPARE(RemoteKey::keyboardPacket(0x35).data.toHex(),QByteArray("000000000035"));
        QCOMPARE(RemoteKey::touchPacket(500,300).data.toHex(),QByteArray("f4012c010000"));
        QCOMPARE(RemoteKey::touchPacket(65535,65535).data.toHex(),QByteArray("ffffffff0000"));
        QCOMPARE(RemoteKey::touchPacket(0,0).command,quint8(66));
        for(int i=0;i<12;++i) QCOMPARE(RemoteKey::fromQt(Qt::Key_F1+i),quint8(0x70+i));
        QCOMPARE(RemoteKey::fromQt(Qt::Key_Home),quint8(0x24));
        QCOMPARE(RemoteKey::fromQt(Qt::Key_Left),quint8(0x25));
        QCOMPARE(RemoteKey::fromQt(Qt::Key_Up),quint8(0x26));
        QCOMPARE(RemoteKey::fromQt(Qt::Key_Right),quint8(0x27));
        QCOMPARE(RemoteKey::fromQt(Qt::Key_Down),quint8(0x28));
        QCOMPARE(RemoteKey::fromQt(Qt::Key_Enter),quint8(0x0d));
        QCOMPARE(RemoteKey::fromQt(Qt::Key_Return),quint8(0x0d));
        QVERIFY(!RemoteKey::fromQt(Qt::Key_Escape));
        const auto packet=RemoteKey::keyboardPacket(0x70);
        QCOMPARE(packet.command,quint8(66)); QCOMPARE(packet.data,QByteArray::fromHex("000000007000"));
        QCOMPARE(LegacyCodec::encode(packet).toHex(),QByteArray("500a424142570000000070006c"));
    }
    void keysWaitForFrameAndAcknowledgement() {
        MachineSession session; ScreenService screen(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); raw->multiScreen=true; raw->keyReplyDelayMs=200;
        session.start(std::move(machine)); QSignalSpy logs(&screen,&ScreenService::keyLog);
        QVERIFY(!screen.queueKey(0x70)); screen.start(true);
        QVERIFY(screen.queueKey(0x70)); QVERIFY(screen.queueKey(0x24));
        QTRY_VERIFY(raw->commands.contains(66));
        QCOMPARE(raw->commands,QList<int>({67,26,26,66}));
        QCOMPARE(raw->requests.last().data,QByteArray::fromHex("000000007000"));
        QTest::qWait(50); QCOMPARE(raw->commands.last(),66);
        QTRY_COMPARE(raw->commands.count(66),2);
        QCOMPARE(raw->commands,QList<int>({67,26,26,66,67,26,26,66}));
        QCOMPARE(raw->requests.last().data,QByteArray::fromHex("000000002400"));
        QVERIFY(logs.count()>=3);
        QVERIFY(logs[1][0].toString().contains("message 0x57 payload 00"));
        screen.stop(); QTest::qWait(250); session.stop();
    }
    void stoppedStreamDiscardsKeys() {
        MachineSession session; ScreenService screen(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); session.start(std::move(machine));
        screen.start(true); QVERIFY(screen.queueKey(0x0d)); screen.stop(); QTest::qWait(100);
        QCOMPARE(raw->commands,QList<int>({67})); session.stop();
    }
    void touchAndKeyboardShareFrameQueue() {
        MachineSession session; ScreenService screen(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); raw->keyReplyDelayMs=150;
        session.start(std::move(machine));
        QSignalSpy frames(&screen,&ScreenService::frameReady),logs(&screen,&ScreenService::keyLog);
        QVERIFY(!screen.queueTouch(0,0));
        connect(&screen,&ScreenService::frameReady,&screen,[&](const QImage&){
            if(frames.count()==1) { QVERIFY(screen.queueTouch(0,0)); QVERIFY(screen.queueKey(0x41)); QVERIFY(!screen.queueTouch(1,0)); }
        });
        screen.start(true);
        QTRY_VERIFY(raw->commands.contains(66));
        QCOMPARE(raw->commands,QList<int>({67,26,66}));
        QCOMPARE(raw->requests.last().data,QByteArray(6,char(0)));
        QTest::qWait(30); QCOMPARE(raw->commands.last(),66);
        QTRY_COMPARE(raw->commands.count(66),2);
        QCOMPARE(raw->commands,QList<int>({67,26,66,67,26,66}));
        QCOMPARE(raw->requests.last().data,RemoteKey::keyboardPacket(0x41).data);
        QVERIFY(logs[0][0].toString().contains("TOUCH TX command 66 x=0 y=0"));
        QVERIFY(logs[1][0].toString().contains("TOUCH RX command 66"));
        QVERIFY(logs[2][0].toString().contains("KEY TX command 66 A"));
        screen.stop(); QTest::qWait(200); session.stop();
        QVERIFY(!screen.queueTouch(0,0));
    }
    void keyTimeoutDoesNotRetry() {
        MachineSession session; ScreenService screen(session,nullptr,100,100);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); raw->dropKeyReply=true; session.start(std::move(machine));
        QSignalSpy errors(&screen,&ScreenService::failed),logs(&screen,&ScreenService::keyLog);
        screen.start(true); QVERIFY(screen.queueKey(0x70)); QTRY_COMPARE(errors.count(),1);
        QCOMPARE(raw->commands,QList<int>({67,26,66})); QVERIFY(logs.last()[0].toString().contains("not retried"));
        QCOMPARE(session.state(),MachineSession::State::Disconnected);
    }
    void oracle() {
        QProcess host; host.start(QCoreApplication::applicationDirPath()+"/legacy/EasyConn.LegacyHost.exe",QStringList{});
        QVERIFY(host.waitForStarted());
        QByteArray requests="hello\n";
        QList<QByteArray> expected{"OK legacyhost-1 Schnell.TeleAssistenza.Protocollo.formatoComando"};
        for(quint8 key:{0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x24,0x25,0x26,0x27,0x28,0x0d}) {
            const auto packet=RemoteKey::keyboardPacket(key);
            requests += "encode 80 66 87 "+packet.data.toHex()+"\n";
            expected.append("OK "+LegacyCodec::encode(packet).toHex().toUpper());
        }
        for(quint8 type:{80,67,66}) {
            Packet p; p.type=type; p.command=3; p.data=QByteArray::fromHex("007f80ff");
            auto bytes=LegacyCodec::encode(p);
            requests += "encode "+QByteArray::number(type)+" 3 87 007F80FF\n";
            expected.append("OK "+bytes.toHex().toUpper());
            requests += "parse "+bytes.toHex()+"\n";
            expected.append("OK "+QByteArray::number(type)+" 66 65 3 87 007F80FF");
        }
        for(const auto& challenge:{QByteArray("00000000"),QByteArray("12345678"),QByteArray("FFFFFFFF")}) {
            requests += "auth "+challenge+"\n";
            expected.append("OK "+LegacyCodec::auth(challenge));
        }
        requests += "parse 50044241035700\n";
        expected.append("ERR Invalid LRC");
        host.write(requests); host.closeWriteChannel();
        QVERIFY(host.waitForFinished(10000)); QCOMPARE(host.exitCode(),0);
        const auto lines=host.readAllStandardOutput().trimmed().split('\n');
        QCOMPARE(lines.size(),expected.size());
        for(qsizetype i=0;i<lines.size();++i) QCOMPARE(lines[i].trimmed(),expected[i]);
    }
    void framing() {
        Packet p; p.command=3;
        const auto wire=LegacyCodec::encode(p); QCOMPARE(wire.toHex(),QByteArray("50044241035753"));
        LegacyCodec codec;
        for(qsizetype i=0;i<wire.size()-1;++i) QVERIFY(codec.feed(wire.mid(i,1)).isEmpty());
        QCOMPARE(codec.feed(wire.right(1)).size(),1);
        QCOMPARE(codec.feed(wire+wire).size(),2);
        auto bad=wire; bad[bad.size()-1]=char(0);
        QVERIFY_EXCEPTION_THROWN(codec.feed(bad),std::runtime_error);
        codec.reset(); QVERIFY_EXCEPTION_THROWN(codec.feed(QByteArray::fromHex("5000")),std::runtime_error);
        codec.reset(); QVERIFY_EXCEPTION_THROWN(codec.feed(QByteArray::fromHex("42ffffffff")),std::runtime_error);
        p.data=QByteArray(252,'x'); QVERIFY_EXCEPTION_THROWN(LegacyCodec::encode(p),std::runtime_error);
    }
    void compressionCodec() {
        const QByteArray plain(20000,'A'); auto packed=qCompress(plain).mid(4);
        // Generated by the original Tool.dll BytesManager.Compress (SharpZipLib).
        const auto original=QByteArray::fromHex("789CEDC13101000000C2A06CF62F658D1DC0000000000000000000000000000000000000004838E54DD73E");
        QCOMPARE(LegacyCodec::decompress(original),plain);
        QCOMPARE(LegacyCodec::decompress(packed),plain);
        QVERIFY_EXCEPTION_THROWN(LegacyCodec::decompress(packed,19999),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(LegacyCodec::decompress(packed.left(packed.size()-2)),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(LegacyCodec::decompress(packed+"x"),std::runtime_error);
        for(quint8 type:{72,90}) {
            Packet p; p.type=type; p.data=packed; LegacyCodec decoder;
            QCOMPARE(decoder.feed(LegacyCodec::encode(p)).front().data,plain);
        }
    }
    void largeScreenBlocks() {
        MachineSession session; ScreenService screen(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get();
        session.start(std::move(machine)); QSignalSpy frames(&screen,&ScreenService::frameReady),stats(&screen,&ScreenService::statistics);
        screen.start(false); QTRY_COMPARE(frames.count(),1); QCOMPARE(raw->commands,QList<int>({67,26}));
        QCOMPARE(raw->requests.last().data,QByteArray::fromHex("00fa"));
        QCOMPARE(stats.count(),1); QVERIFY(stats[0][0].toString().contains("1 blocks x 250 KiB")); session.stop();
    }
    void compressionNegotiation() {
        MachineSession session; VersionProbe probe(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); session.start(std::move(machine));
        QSignalSpy result(&probe,&VersionProbe::completed); probe.start(); QTRY_COMPARE(result.count(),1);
        bool found=false; for(const auto& p:raw->requests) if(p.command==14) { QCOMPARE(p.data,QByteArray::fromHex("31")); found=true; }
        QVERIFY(found); session.stop();
    }
    void screenPixels() {
        auto image=ScreenService::decodePcx(tinyPcx()); QCOMPARE(image.size(),QSize(1,1)); QCOMPARE(image.pixel(0,0),qRgb(10,20,30));
        auto zeroRun=tinyPcx(); zeroRun.insert(128,QByteArray::fromHex("c07f"));
        QCOMPARE(ScreenService::decodePcx(zeroRun).pixel(0,0),image.pixel(0,0));
        zeroRun=tinyPcx(); zeroRun.insert(129,QByteArray::fromHex("c000c0ff"));
        QCOMPARE(ScreenService::decodePcx(zeroRun).pixel(0,0),image.pixel(0,0));
        auto truncatedRun=tinyPcx(); truncatedRun.truncate(128); truncatedRun.append(char(0xc0));
        QVERIFY_EXCEPTION_THROWN(ScreenService::decodePcx(truncatedRun),std::runtime_error);
        auto delta=ScreenService::decodePcx(tinyPcx(181,183,248),image); QCOMPARE(delta.pixel(0,0),image.pixel(0,0));
        delta=ScreenService::decodePcx(tinyPcx(255,128,0),image); QCOMPARE(delta.pixel(0,0),qRgb(255,128,0));
        auto broken=tinyPcx(); broken.chop(1); QVERIFY_EXCEPTION_THROWN(ScreenService::decodePcx(broken),std::runtime_error);
        broken=tinyPcx(); broken[128]=char(255); QVERIFY_EXCEPTION_THROWN(ScreenService::decodePcx(broken),std::runtime_error);
        // A one-pixel delta at nonzero X on an existing two-pixel screen.
        QImage base(2,1,QImage::Format_RGB32); base.fill(Qt::black);
        auto patch=tinyPcx(1,2,3); patch[4]=1; patch[8]=1; patch[70]=1;
        auto composed=ScreenService::decodePcx(patch,base);
        QCOMPARE(composed.pixel(0,0),qRgb(0,0,0)); QCOMPARE(composed.pixel(1,0),qRgb(1,2,3));
        QVERIFY_EXCEPTION_THROWN(ScreenService::decodePcx(patch),std::runtime_error);
        patch[4]=2; patch[8]=2;
        QVERIFY_EXCEPTION_THROWN(ScreenService::decodePcx(patch,base),std::runtime_error);
    }
    void capturedScreenDelta() {
        const auto path=qEnvironmentVariable("EZCONN_PCX_FIXTURE");
        if(path.isEmpty()) QSKIP("Set EZCONN_PCX_FIXTURE to test a captured device frame");
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QImage base(1280,1024,QImage::Format_RGB32); base.fill(Qt::black);
        auto image=ScreenService::decodePcx(file.readAll(),base);
        QCOMPARE(image.size(),base.size());
        QCOMPARE(image.pixel(0,0),base.pixel(0,0));
        QVERIFY(image!=base);
    }
    void singleFrameWaitsForImage() {
        MachineSession session; ScreenService screen(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); raw->screenPendingReplies=2;
        session.start(std::move(machine));
        QSignalSpy frames(&screen,&ScreenService::frameReady), errors(&screen,&ScreenService::failed);
        screen.start(false); QTRY_COMPARE(frames.count(),1);
        QCOMPARE(errors.count(),0); QCOMPARE(raw->commands,QList<int>({67,67,67,26}));
        QTest::qWait(200); QCOMPARE(raw->commands.size(),4);
        session.stop();
    }
    void slowScreenUpdate() {
        MachineSession session; ScreenService screen(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); raw->screenDelayMs=6000;
        session.start(std::move(machine));
        QSignalSpy frames(&screen,&ScreenService::frameReady),errors(&screen,&ScreenService::failed),status(&screen,&ScreenService::status);
        screen.start(false); QTRY_COMPARE_WITH_TIMEOUT(frames.count(),1,9000);
        QCOMPARE(errors.count(),0); QVERIFY(status.count()>=1);
        QCOMPARE(raw->commands,QList<int>({67,26}));
        QCOMPARE(session.state(),MachineSession::State::Connected); session.stop();
    }
    void lostScreenReplyIsBounded() {
        MachineSession session; ScreenService screen(session,nullptr,100,100);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); raw->dropScreenReply=true;
        session.start(std::move(machine)); QSignalSpy errors(&screen,&ScreenService::failed);
        screen.start(true); QTRY_COMPARE(errors.count(),1);
        QVERIFY(errors[0][0].toString().contains("command 67"));
        QCOMPARE(raw->commands,QList<int>({67})); QCOMPARE(session.state(),MachineSession::State::Disconnected);
    }
    void blockTimeoutTracksProgress() {
        MachineSession session; ScreenService screen(session,nullptr,1000,100);
        auto machine=std::make_unique<SimulatedMachine>(); machine->slowBlockFragments=true;
        session.start(std::move(machine));
        QSignalSpy frames(&screen,&ScreenService::frameReady),errors(&screen,&ScreenService::failed);
        screen.start(false); QTRY_COMPARE(frames.count(),1); QCOMPARE(errors.count(),0); session.stop();
    }
    void screenTransfer() {
        MachineSession session; ScreenService screen(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); session.start(std::move(machine));
        QSignalSpy frames(&screen,&ScreenService::frameReady); QSignalSpy errors(&screen,&ScreenService::failed);
        screen.start(false); QTRY_COMPARE(frames.count(),1); QCOMPARE(raw->commands,QList<int>({67,26})); QCOMPARE(errors.count(),0);
        raw->commands.clear(); raw->multiScreen=true; screen.start(false); QTRY_COMPARE(frames.count(),2); QCOMPARE(raw->commands,QList<int>({67,26,26})); raw->multiScreen=false;
        raw->commands.clear(); screen.start(true); QTRY_VERIFY(frames.count()>=3);
        screen.stop(); QTest::qWait(200); auto count=raw->commands.size(); QTest::qWait(200); QCOMPARE(raw->commands.size(),count);
        raw->commands.clear(); screen.start(true); screen.stop(); QTest::qWait(100); QCOMPARE(raw->commands,QList<int>({67}));
        session.stop();
    }
    void readParsers() {
        const auto entries=ReadServices::parseDirectory(".\\\r\n..\\\r\nConfig\\\nFile.TXT\n");
        QCOMPARE(entries.size(),3); QCOMPARE(entries[1].name,QString("Config")); QVERIFY(entries[1].directory);
        QCOMPARE(entries[2].name,QString("File.TXT")); QVERIFY(!entries[2].directory);
        auto info=ReadServices::parseDisk(QByteArray::fromHex("7856341200100000000010000000080000000000"));
        QCOMPARE(info.totalBytes(),quint64(4294967296ULL)); QCOMPARE(info.freeBytes(),quint64(2147483648ULL));
        QVERIFY_EXCEPTION_THROWN(ReadServices::parseDisk("short"),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(ReadServices::parseDirectory(QByteArray("x\0y",3)),std::runtime_error);
    }
    void readRequests() {
        MachineSession session; ReadServices reads(session);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); session.start(std::move(machine));
        QSignalSpy errors(&reads,&ReadServices::failed);
        reads.listDirectory("D:\\"); QCOMPARE(errors.count(),1); QVERIFY(raw->commands.isEmpty());
        reads.setReady(true);
        int directories=0,disks=0;
        connect(&reads,&ReadServices::directoryReady,this,[&](const QString& path,const QList<DirectoryEntry>& entries){ QCOMPARE(path,QString("D:\\")); QCOMPARE(entries.size(),3); ++directories; });
        connect(&reads,&ReadServices::diskReady,this,[&](const QString&,const DiskInfo& info){ QCOMPARE(info.totalBytes(),quint64(4294967296ULL)); ++disks; });
        reads.listDirectory("D:\\");
        reads.readDisk("D:\\"); QCOMPARE(errors.count(),2); // Concurrent requests rejected.
        QTRY_COMPARE(directories,1); QCOMPARE(raw->commands,QList<int>({24,23}));
        reads.readDisk("D:\\"); QTRY_COMPARE(disks,1); QCOMPARE(raw->commands,QList<int>({24,23,45}));
        session.stop(); reads.readDisk("D:\\"); QCOMPARE(errors.count(),3);
    }
    void handshake() {
        MachineSession session; VersionProbe probe(session);
        QSignalSpy result(&probe,&VersionProbe::completed); QSignalSpy errors(&probe,&VersionProbe::failed);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get();
        session.start(std::move(machine)); probe.start();
        QTRY_COMPARE(result.count(),1); QCOMPARE(errors.count(),0);
        QCOMPARE(raw->commands,QList<int>({1,2,14,38,3}));
        QCOMPARE(result.at(0).at(0).toString(),QString("Kaparossa simulator"));
        QCOMPARE(result.at(0).at(1).toInt(),2);
        session.stop();
    }
    void authenticationMismatch() {
        MachineSession session; VersionProbe probe(session); QSignalSpy errors(&probe,&VersionProbe::failed);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get(); raw->wrongAuth=true;
        session.start(std::move(machine)); probe.start();
        QTRY_COMPARE(errors.count(),1); QCOMPARE(raw->commands,QList<int>({1})); session.stop();
    }
    void serialSkipsAuthentication() {
        MachineSession session; VersionProbe probe(session); QSignalSpy result(&probe,&VersionProbe::completed);
        auto machine=std::make_unique<SimulatedMachine>(); auto* raw=machine.get();
        session.start(std::move(machine)); probe.start(true);
        QTRY_COMPARE(result.count(),1); QCOMPARE(raw->commands,QList<int>({14,38,3})); session.stop();
    }
    void cliOverTcp() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        LegacyCodec decoder; QList<int> commands;
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto* peer=server.nextPendingConnection();
            connect(peer,&QTcpSocket::readyRead,this,[&,peer] {
                for(const auto& p:decoder.feed(peer->readAll())) {
                    commands.append(p.command);
                    Packet reply; reply.destination=65; reply.sender=66; reply.command=p.command; reply.message=82;
                    if(p.command==1) reply.data=LegacyCodec::auth(p.data);
                    if(p.command==14) reply.data="TCP simulator";
                    if(p.command==38) reply.data=QByteArray(1,char(32));
                    if(p.command==3) reply.data=QByteArray(1,char(2));
                    peer->write(LegacyCodec::encode(reply));
                }
            });
        });
        QProcess child;
        child.start(QCoreApplication::applicationDirPath()+"/EasyConnProbe.exe",{"--versions","--tcp","127.0.0.1:"+QString::number(server.serverPort())});
        QVERIFY(child.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(child.state(),QProcess::NotRunning,10000);
        QCOMPARE(child.exitCode(),0); QVERIFY(child.readAllStandardOutput().contains("Software: TCP simulator"));
        QCOMPARE(commands,QList<int>({1,2,14,38,3}));
    }
};
QTEST_GUILESS_MAIN(ProtocolTest)
#include "ProtocolTest.moc"
