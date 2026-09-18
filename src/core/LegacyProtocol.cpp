#include "LegacyProtocol.h"
#include <QRandomGenerator>
#include <stdexcept>
#include <zlib.h>
namespace ezconn {
namespace {
int width(quint8 type) {
    switch(type) { case 80: return 1; case 67: case 84: case 90: return 2; case 66: case 88: case 72: return 4;
    default: throw std::runtime_error("Unsupported packet type"); }
}
constexpr quint32 MaxBody=1024*1024+4;
}
QByteArray LegacyCodec::decompress(const QByteArray& bytes,int maxBytes) {
    if(bytes.isEmpty() || bytes.size()>16*1024*1024 || maxBytes<=0) throw std::runtime_error("Invalid compressed payload size");
    z_stream stream{};
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.constData())); stream.avail_in=uInt(bytes.size());
    if(inflateInit(&stream)!=Z_OK) throw std::runtime_error("Cannot initialize zlib");
    struct Cleanup { z_stream* stream; ~Cleanup(){inflateEnd(stream);} } cleanup{&stream};
    QByteArray result; char chunk[16384];
    for(;;) {
        stream.next_out=reinterpret_cast<Bytef*>(chunk); stream.avail_out=sizeof(chunk);
        const auto code=inflate(&stream,Z_NO_FLUSH);
        const int count=int(sizeof(chunk)-stream.avail_out);
        if(result.size()+count>maxBytes) throw std::runtime_error("Decompressed payload exceeds limit");
        result.append(chunk,count);
        if(code==Z_STREAM_END) { if(stream.avail_in) throw std::runtime_error("Trailing compressed data"); return result; }
        if(code!=Z_OK || (!count && !stream.avail_in)) throw std::runtime_error("Invalid or truncated zlib payload");
    }
}
QByteArray LegacyCodec::encode(const Packet& p) {
    const int w=width(p.type); const auto len=p.data.size()+4;
    if (len>MaxBody || (w==1 && len>255) || (w==2 && len>65535)) throw std::runtime_error("Payload too large");
    QByteArray b; b.append(char(p.type));
    for(int i=0;i<w;++i) b.append(char((len>>(i*8))&255));
    b.append(char(p.destination)); b.append(char(p.sender)); b.append(char(p.command)); b.append(char(p.message)); b+=p.data;
    quint8 lrc=0; for(qsizetype i=1;i<b.size();++i) lrc^=quint8(b[i]);
    b.append(char(lrc)); return b;
}
QByteArray LegacyCodec::auth(const QByteArray& challenge) {
    bool ok=false; auto n=challenge.toUInt(&ok,16);
    if (!ok || challenge.size()!=8) throw std::runtime_error("Invalid authentication challenge");
    n^=0x1234FEDCu; n=(n<<16)|(n>>16); n=(n>>1)|(n<<31);
    return QByteArray::number(n,16).rightJustified(8,'0').toUpper();
}
QList<Packet> LegacyCodec::feed(const QByteArray& bytes) {
    if (buffer_.size()+bytes.size()>2*MaxBody) throw std::runtime_error("Receive buffer limit exceeded");
    buffer_+=bytes; QList<Packet> packets;
    while(!buffer_.isEmpty()) {
        const int w=width(quint8(buffer_[0]));
        if(buffer_.size()<w+1) break;
        quint32 len=0; for(int i=0;i<w;++i) len|=quint32(quint8(buffer_[i+1]))<<(8*i);
        if(len<4 || len>MaxBody) throw std::runtime_error("Invalid packet length");
        const qsizetype total=len+w+2;
        if(buffer_.size()<total) break;
        quint8 check=0; for(qsizetype i=1;i<total;++i) check^=quint8(buffer_[i]);
        if(check) throw std::runtime_error("Invalid LRC");
        Packet p{quint8(buffer_[0]),quint8(buffer_[w+1]),quint8(buffer_[w+2]),quint8(buffer_[w+3]),quint8(buffer_[w+4]),buffer_.mid(w+5,len-4)};
        if(p.type==90 || p.type==72) p.data=decompress(p.data,int(MaxBody)-4);
        if(p.message==69 && p.data.isEmpty()) throw std::runtime_error("Empty error payload");
        packets.append(p); buffer_.remove(0,total);
    }
    return packets;
}
VersionProbe::VersionProbe(MachineSession& session,QObject* parent):QObject(parent),session_(session) {
    timer_.setSingleShot(true); timer_.setInterval(5000);
    connect(&timer_,&QTimer::timeout,this,[this]{fail("Protocol response timeout");});
    connect(&session_,&MachineSession::received,this,&VersionProbe::receive);
    connect(&session_,&MachineSession::stateChanged,this,[this](auto s){
        if(s==MachineSession::State::Disconnected && expected_) fail("Disconnected during handshake");
    });
}
void VersionProbe::fail(const QString& reason) { expected_=0; timer_.stop(); emit failed(reason); }
void VersionProbe::request(quint8 command,const QByteArray& data) {
    expected_=command; timer_.start();
    Packet p; p.command=command; p.data=data;
    if(!session_.send(LegacyCodec::encode(p)) && expected_) fail("Request write failed");
}
void VersionProbe::start(bool serial) {
    if(expected_) { fail("Probe already active"); return; }
    if(session_.state()!=MachineSession::State::Connected) { fail("Not connected"); return; }
    codec_.reset(); software_.clear(); options_=0;
    if(serial) request(14,QByteArray(1,char(0x31)));
    else { challenge_=QByteArray::number(QRandomGenerator::global()->generate(),16).rightJustified(8,'0').toUpper(); request(1,challenge_); }
}
void VersionProbe::receive(const QByteArray& bytes) {
    if(!expected_) return;
    try {
        const auto packets=codec_.feed(bytes);
        for(const auto& p:packets) {
            if(!expected_) throw std::runtime_error("Unexpected extra packet");
            if(p.destination!=65 || p.sender!=66 || p.command!=expected_) throw std::runtime_error("Unexpected reply address or command");
            if(p.message==69) {
                if(expected_==3) { expected_=0; timer_.stop(); emit completed(QString::fromLatin1(software_),0,options_); return; }
                throw std::runtime_error("Device returned error code " + std::to_string(quint8(p.data[0])));
            }
            timer_.stop();
            switch(expected_) {
            case 1:
                if(p.data!=LegacyCodec::auth(challenge_)) throw std::runtime_error("Authentication response mismatch");
                request(2,LegacyCodec::auth(LegacyCodec::auth(challenge_))); break;
            case 2: request(14,QByteArray(1,char(0x31))); break;
            case 14:
                if(p.data.isEmpty()) throw std::runtime_error("Empty software version");
                software_=p.data; request(38); break;
            case 38:
                if(p.data.isEmpty()) throw std::runtime_error("Empty options");
                options_=quint8(p.data[0]); request(3); break;
            case 3:
                if(p.data.isEmpty()) throw std::runtime_error("Empty protocol version");
                expected_=0; emit completed(QString::fromLatin1(software_),quint8(p.data[0]),options_); break;
            }
        }
    } catch(const std::exception& e) { fail(QString::fromUtf8(e.what())); }
}
}
