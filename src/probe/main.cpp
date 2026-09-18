#include "core/Connection.h"
#include "core/LegacyProtocol.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
using namespace ezconn;
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("EasyConnProbe");
    QCommandLineParser parser;
    parser.setApplicationDescription("EzConn connection and read-only version probe.");
    parser.addHelpOption();
    parser.addOption({"versions", "Authenticate and read software/protocol versions, then disconnect"});
    parser.addOption({"tcp", "TCP IP:port (default 192.168.1.66:2000)", "endpoint"});
    parser.addOption({"serial", "Serial port; requires --baud", "port"});
    parser.addOption({"baud", "Serial baud rate (8N1)", "rate"});
    parser.addOption({"duration", "Observation time after connection, milliseconds", "ms", "5000"});
    auto invalid = [](const QString& message) { QTextStream(stderr) << message << Qt::endl; return 2; };
    if (!parser.parse(app.arguments())) return invalid(parser.errorText());
    if (parser.isSet("help") || parser.isSet("help-all")) { QTextStream(stdout) << parser.helpText(); return 0; }
    bool ok = false;
    const int duration = parser.value("duration").toInt(&ok);
    if (!ok || duration <= 0) return invalid("Invalid duration");
    if (parser.isSet("tcp") && parser.isSet("serial")) return invalid("Choose TCP or serial");
    std::unique_ptr<ITransport> transport;
    if (parser.isSet("serial")) {
        const int baud = parser.value("baud").toInt(&ok);
        if (!ok || baud <= 0 || parser.value("serial").trimmed().isEmpty()) return invalid("Serial requires a port and positive --baud");
        transport = std::make_unique<SerialTransport>(parser.value("serial"), baud);
    } else {
        if (parser.isSet("baud")) return invalid("--baud requires --serial");
        Endpoint endpoint;
        if (parser.isSet("tcp") && !Endpoint::parse(parser.value("tcp"), endpoint)) return invalid("Invalid TCP endpoint (expected IP:port)");
        QTextStream(stdout) << "TCP " << endpoint.host << ':' << endpoint.port << Qt::endl;
        transport = std::make_unique<TcpTransport>(endpoint);
    }
    MachineSession session;
    VersionProbe versions(session);
    bool connected = false;
    bool stopping = false;
    QObject::connect(&versions, &VersionProbe::failed, &app, [&](const QString& reason) {
        QTextStream(stderr) << reason << Qt::endl; stopping=true; session.stop(); app.exit(1);
    });
    QObject::connect(&versions, &VersionProbe::completed, &app, [&](const QString& software,int protocol,quint8 options) {
        QTextStream(stdout) << "Software: " << software << "\nProtocol: " << protocol << "\nOptions: " << options << Qt::endl;
        stopping=true; session.stop(); app.exit(0);
    });
    QObject::connect(&session, &MachineSession::logLine, &app, [](const QString& line) { QTextStream(stdout) << line << Qt::endl; });
    QObject::connect(&session, &MachineSession::failed, &app, [&] { app.exit(1); });
    QObject::connect(&session, &MachineSession::stateChanged, &app, [&](auto state) {
        if (state == MachineSession::State::Connected) {
            connected = true;
            if (parser.isSet("versions")) { versions.start(parser.isSet("serial")); return; }
            QTimer::singleShot(duration, &app, [&] { stopping = true; session.stop(); app.exit(0); });
        } else if (state == MachineSession::State::Disconnected && connected && !stopping) app.exit(1);
    });
    QTimer::singleShot(0, &app, [&] { session.start(std::move(transport)); });
    return app.exec();
}
