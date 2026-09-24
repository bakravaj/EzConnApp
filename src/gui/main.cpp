#include "core/Connection.h"
#include "core/LegacyProtocol.h"
#include "core/ReadServices.h"
#include "core/ScreenService.h"
#include <QDialog>
#include "ScreenView.h"
#include "RemoteKeyFilter.h"
#include "ScannerPanel.h"
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QTreeWidget>

using namespace ezconn;
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle("EzConn — Connection probe");
    window.resize(860, 720);
    auto* layout = new QVBoxLayout(&window);
    auto* form = new QFormLayout;
    auto* host = new QLineEdit(DefaultHost);
    auto* port = new QSpinBox;
    port->setRange(1, 65535); port->setValue(DefaultPort);
    form->addRow("IP", host); form->addRow("Port", port); layout->addLayout(form);
    layout->addWidget(new QLabel("Connect, then read software and protocol versions."));
    auto* button = new QPushButton("Connect"); layout->addWidget(button);
    auto* versionButton = new QPushButton("Read versions"); versionButton->setEnabled(false); layout->addWidget(versionButton);
    auto* screenButton = new QPushButton("Screen / Video"); screenButton->setEnabled(false); layout->addWidget(screenButton);
    auto* scannerButton=new QPushButton("Scanner Emulator"); layout->addWidget(scannerButton);
    QDialog screenWindow(&window); screenWindow.setWindowTitle("Kaparossa screen"); screenWindow.resize(1000,760);
    auto* screenLayout=new QVBoxLayout(&screenWindow);
    auto* singleFrame=new QPushButton("Single frame"); auto* startVideo=new QPushButton("Start video"); auto* stopVideo=new QPushButton("Stop"); stopVideo->setEnabled(false);
    screenLayout->addWidget(singleFrame); screenLayout->addWidget(startVideo); screenLayout->addWidget(stopVideo);
    auto* statsLabel=new QLabel; statsLabel->setWordWrap(true); screenLayout->addWidget(statsLabel);
    auto* screenStatus=new QLabel("Ready"); screenLayout->addWidget(screenStatus);
    auto* screenView=new ScreenView; screenLayout->addWidget(screenView,1);
    auto* readPanel = new QWidget;
    auto* readLayout = new QVBoxLayout(readPanel);
    auto* path = new QLineEdit("D:\\"); readLayout->addWidget(path);
    auto* listButton = new QPushButton("List directory"); readLayout->addWidget(listButton);
    auto* diskButton = new QPushButton("Disk information"); readLayout->addWidget(diskButton);
    auto* diskLabel = new QLabel("Disk information not read"); diskLabel->setWordWrap(true); readLayout->addWidget(diskLabel);
    auto* files = new QTreeWidget; files->setHeaderLabels({"Name","Type"}); readLayout->addWidget(files);
    readPanel->setEnabled(false); layout->addWidget(readPanel);
    auto* output = new QPlainTextEdit; output->setReadOnly(true);
    output->setMaximumBlockCount(2000); layout->addWidget(output);
    MachineSession session;
    VersionProbe versions(session);
    ReadServices reads(session);
    ScreenService screen(session);
    ScannerService scanner(session);
    ScannerPanel scannerPanel(scanner,&window);
    QObject::connect(scannerButton,&QPushButton::clicked,&window,[&]{scannerPanel.show(); scannerPanel.raise(); scannerPanel.activateWindow();});
    QObject::connect(&scanner,&ScannerService::logLine,output,&QPlainTextEdit::appendPlainText);
    QObject::connect(&scanner,&ScannerService::failed,output,[&](const QString& message){output->appendPlainText("Scanner error: "+message);});
    RemoteKeyFilter keyFilter(&screenWindow,[&](quint8 key){screen.queueKey(key);});
    screenView->touch=[&](QPoint point){screen.queueTouch(quint16(point.x()),quint16(point.y()));};
    bool authenticated=false;
    bool screenActive=false;
    int frameCount=0;
    bool videoAfterProfiler=false;
    QObject::connect(screenButton,&QPushButton::clicked,&window,[&]{screenWindow.show(); screenWindow.raise(); screenWindow.activateWindow(); screenView->setFocus();});
    auto configureScreen=[&]{frameCount=0; statsLabel->clear(); };
    QObject::connect(singleFrame,&QPushButton::clicked,&window,[&]{configureScreen(); screen.start(false);});
    QObject::connect(startVideo,&QPushButton::clicked,&window,[&]{configureScreen(); screen.start(true); screenView->setFocus();});
    QObject::connect(stopVideo,&QPushButton::clicked,&window,[&]{screenStatus->setText("Stopping..."); screen.stop();});
    QObject::connect(&screenWindow,&QDialog::finished,&window,[&]{screen.stop();});
    QObject::connect(&screen,&ScreenService::activeChanged,&window,[&](bool active){
        screenActive=active; readPanel->setEnabled(authenticated && !active);
        keyFilter.setActive(active);
        session.setWireLogging(!active);
        singleFrame->setEnabled(authenticated && !active); startVideo->setEnabled(authenticated && !active); stopVideo->setEnabled(active);
        screenStatus->setText(active ? "Receiving..." : "Stopped");
    });
    QObject::connect(&screen,&ScreenService::frameReady,&window,[&](const QImage& image){
        screenView->setFrame(image);
        screenStatus->setText(QString("Frame %1 | %2 x %3").arg(++frameCount).arg(image.width()).arg(image.height()));
    });
    QObject::connect(&screen,&ScreenService::failed,&window,[&](const QString& error){ screenStatus->setText(error); output->appendPlainText("Screen: "+error); });
    QObject::connect(&screen,&ScreenService::status,&window,[&](const QString& message){ screenStatus->setText(message); output->appendPlainText("Screen: "+message); });
    QObject::connect(&screen,&ScreenService::keyLog,output,&QPlainTextEdit::appendPlainText);
    QObject::connect(&screen,&ScreenService::profilerLog,output,[&](const QString& message){
        output->appendPlainText(message);
        if(message.startsWith("Profiler PLANES: P0=")) videoAfterProfiler=true;
    });
    QObject::connect(&screen,&ScreenService::statistics,&window,[&](const QString& message){
        statsLabel->setText(message);
        if(videoAfterProfiler || frameCount==1 || frameCount%10==0) output->appendPlainText("Video: "+message);
        videoAfterProfiler=false;
    });
    QString displayedPath;
    QObject::connect(listButton,&QPushButton::clicked,&window,[&]{ files->clear(); reads.listDirectory(path->text()); });
    QObject::connect(diskButton,&QPushButton::clicked,&window,[&]{ diskLabel->setText("Reading..."); reads.readDisk(path->text().left(3)); });
    QObject::connect(&reads,&ReadServices::busyChanged,&window,[&](bool busy){ readPanel->setEnabled(authenticated && !busy && !screenActive); screenButton->setEnabled(authenticated && !busy); singleFrame->setEnabled(authenticated && !busy && !screenActive); startVideo->setEnabled(authenticated && !busy && !screenActive); });
    QObject::connect(&reads,&ReadServices::failed,output,[&](const QString& error){ output->appendPlainText("Read error: "+error); diskLabel->setText("Read failed; see log"); });
    QObject::connect(&reads,&ReadServices::directoryReady,&window,[&](const QString& current,const QList<DirectoryEntry>& entries){
        displayedPath=current; path->setText(current); files->clear();
        for(const auto& entry:entries) { auto* item=new QTreeWidgetItem(files,{entry.name,entry.directory ? "Directory" : "File"}); item->setData(0,Qt::UserRole,entry.directory); }
        output->appendPlainText(QString("Directory %1: %2 entries").arg(current).arg(entries.size()));
    });
    QObject::connect(files,&QTreeWidget::itemDoubleClicked,&window,[&](QTreeWidgetItem* item,int){
        if(!item->data(0,Qt::UserRole).toBool()) return;
        auto current=displayedPath;
        if(item->text(0)=="..") { if(current.size()>3) { current.chop(1); current=current.left(current.lastIndexOf('\\')+1); } }
        else current+=item->text(0)+"\\";
        files->clear(); reads.listDirectory(current);
    });
    QObject::connect(&reads,&ReadServices::diskReady,&window,[&](const QString& drive,const DiskInfo& info){
        diskLabel->setText(QString("%1  Serial: %2 | Cluster: %3 bytes\nTotal: %4 bytes | Free: %5 bytes | Bad clusters: %6")
            .arg(drive).arg(info.serial).arg(info.clusterBytes).arg(info.totalBytes()).arg(info.freeBytes()).arg(info.badClusters));
    });
    QObject::connect(versionButton, &QPushButton::clicked, &window, [&] {
        versionButton->setEnabled(false); versions.start();
    });
    QObject::connect(&versions, &VersionProbe::completed, &window, [&](const QString& software,int protocol,quint8 options) {
        authenticated=true; reads.setReady(true); readPanel->setEnabled(true);
        screenButton->setEnabled(true); singleFrame->setEnabled(true); startVideo->setEnabled(true);
        output->appendPlainText(QString("Software: %1\nProtocol: %2\nOptions: 0x%3").arg(software).arg(protocol).arg(options,2,16,QChar('0')));
    });
    QObject::connect(&versions, &VersionProbe::failed, &window, [&](const QString& error) {
        output->appendPlainText("Protocol error: " + error); session.stop();
    });
    QObject::connect(&session, &MachineSession::logLine, output, &QPlainTextEdit::appendPlainText);
    QObject::connect(&session, &MachineSession::stateChanged, &window, [&](auto state) {
        const bool idle = state == MachineSession::State::Disconnected;
        if(state!=MachineSession::State::Connected) { screenButton->setEnabled(false); singleFrame->setEnabled(false); startVideo->setEnabled(false); }
        if(state!=MachineSession::State::Connected) { authenticated=false; reads.setReady(false); readPanel->setEnabled(false); files->clear(); diskLabel->setText("Disk information not read"); }
        host->setEnabled(idle); port->setEnabled(idle);
        button->setText(idle ? "Connect" : "Disconnect");
        versionButton->setEnabled(state == MachineSession::State::Connected);
    });
    QObject::connect(button, &QPushButton::clicked, &window, [&] {
        if (session.state() != MachineSession::State::Disconnected) { session.stop(); return; }
        Endpoint endpoint;
        if (!Endpoint::parse(host->text().trimmed() + ':' + QString::number(port->value()), endpoint)) {
            output->appendPlainText("Invalid IP address"); return;
        }
        session.start(std::make_unique<TcpTransport>(endpoint));
    });
    // Serialize the independent services on the existing authenticated session.
    bool readBusy=false;
    auto updateAvailability=[&]{
        const bool connected=session.state()==MachineSession::State::Connected;
        const bool free=authenticated && connected && !screenActive && !readBusy && !scanner.busy();
        scanner.setReady(authenticated && connected && !screenActive && !readBusy);
        scannerPanel.setAvailable(free);
        readPanel->setEnabled(free);
        screenButton->setEnabled(authenticated && connected && !readBusy && !scanner.busy());
        singleFrame->setEnabled(free); startVideo->setEnabled(free);
        versionButton->setEnabled(connected && !authenticated && !screenActive && !readBusy && !scanner.busy());
    };
    QObject::connect(&reads,&ReadServices::busyChanged,&window,[&](bool busy){readBusy=busy; updateAvailability();});
    QObject::connect(&screen,&ScreenService::activeChanged,&window,[&](bool){updateAvailability();});
    QObject::connect(&scanner,&ScannerService::busyChanged,&window,[&](bool){updateAvailability();});
    QObject::connect(&versions,&VersionProbe::completed,&window,[&]{updateAvailability();});
    QObject::connect(&session,&MachineSession::stateChanged,&window,[&](auto){updateAvailability();});
    window.show();
    return app.exec();
}
