#include "ReadServices.h"
#include <QtEndian>
#include <stdexcept>
namespace ezconn {
QList<DirectoryEntry> ReadServices::parseDirectory(const QByteArray& data) {
    QList<DirectoryEntry> result;
    for(auto line:data.split('\n')) {
        if(line.endsWith('\r')) line.chop(1);
        if(line.isEmpty() || line==".\\") continue;
        for(auto c:line) if(quint8(c)<32 || quint8(c)>126) throw std::runtime_error("Directory contains unsupported non-ASCII characters");
        bool directory=line.endsWith('\\'); if(directory) line.chop(1);
        if(line.isEmpty() || line.contains('\\') || line.contains('/') || line.contains(':')) throw std::runtime_error("Invalid directory entry");
        result.append({QString::fromLatin1(line),directory});
    }
    return result;
}
DiskInfo ReadServices::parseDisk(const QByteArray& data) {
    if(data.size()!=20) throw std::runtime_error("Disk response must contain exactly 20 bytes");
    auto value=[&](int offset){return qFromLittleEndian<quint32>(data.constData()+offset);};
    DiskInfo info{qFromLittleEndian<qint32>(data.constData()),value(4),value(8),value(12),value(16)};
    if(!info.clusterBytes || info.freeClusters>info.totalClusters || info.badClusters>info.totalClusters) throw std::runtime_error("Invalid disk counters");
    return info;
}
ReadServices::ReadServices(MachineSession& session,QObject* parent):QObject(parent),session_(session) {
    timer_.setSingleShot(true); timer_.setInterval(5000);
    connect(&timer_,&QTimer::timeout,this,[this]{fail("Read request timed out",true);});
    connect(&session_,&MachineSession::received,this,&ReadServices::receive);
    connect(&session_,&MachineSession::stateChanged,this,[this](auto state){
        if(state==MachineSession::State::Disconnected) { bool active=expected_!=0; setReady(false); if(active) emit failed("Disconnected during read"); }
    });
}
void ReadServices::setReady(bool ready) { ready_=ready; if(!ready) { finish(); codec_.reset(); } }
void ReadServices::finish() { expected_=0; timer_.stop(); emit busyChanged(false); }
void ReadServices::fail(const QString& message,bool disconnect) {
    finish(); if(disconnect) { ready_=false; session_.stop(); } emit failed(message);
}
bool ReadServices::begin(const QString& path) {
    if(!ready_ || session_.state()!=MachineSession::State::Connected) { emit failed("Read versions first"); return false; }
    if(expected_) { emit failed("A read request is already active"); return false; }
    if(path.isEmpty() || path.size()>251) { emit failed("Invalid path length"); return false; }
    for(auto c:path) if(c.unicode()<32 || c.unicode()>126) { emit failed("Only printable ASCII paths are supported"); return false; }
    path_=path; codec_.reset(); emit busyChanged(true); return true;
}
void ReadServices::request(quint8 command,const QByteArray& data) {
    expected_=command; timer_.start(); Packet packet; packet.command=command; packet.data=data;
    if(!session_.send(LegacyCodec::encode(packet)) && expected_) fail("Request write failed",true);
}
void ReadServices::listDirectory(const QString& path) {
    if(path.size()<3 || !path[0].isLetter() || path.mid(1,2)!=":\\" || !path.endsWith('\\')) { emit failed("Use an absolute path such as D:\\ ending in a backslash"); return; }
    if(begin(path)) request(24,path.toLatin1());
}
void ReadServices::readDisk(const QString& drive) {
    if(drive.size()!=3 || !drive[0].isLetter() || drive.mid(1)!=":\\") { emit failed("Use a drive root such as D:\\"); return; }
    if(begin(drive)) request(45,drive.toLatin1());
}
void ReadServices::receive(const QByteArray& bytes) {
    if(!expected_) return;
    try {
        const auto packets=codec_.feed(bytes);
        for(const auto& p:packets) {
            if(!expected_ || p.destination!=65 || p.sender!=66 || p.command!=expected_) throw std::runtime_error("Unexpected read reply");
            if(p.message==69) { fail(QString("Device error %1 (command %2)").arg(quint8(p.data[0])).arg(p.command)); return; }
            if(expected_==24) { request(23); continue; }
            if(expected_==23) { auto entries=parseDirectory(p.data); finish(); emit directoryReady(path_,entries); }
            else if(expected_==45) { auto info=parseDisk(p.data); finish(); emit diskReady(path_,info); }
        }
    } catch(const std::exception& e) { fail(QString::fromUtf8(e.what()),true); }
}
}
