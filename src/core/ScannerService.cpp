#include "ScannerService.h"
#include <stdexcept>
namespace ezconn {
QString ScannerService::preset(int index) {
    static const char* values[]={
        "BF2D@HjAssemblato@rTrave 3-6@i@p1@l1323@n3@e0,520@d8@g@s32@v@Gl90@w135@l300@w90@l300@w90@l300@w90@l300@w135@l90@w0@C91@",
        "BF2D@HjAssemblato@rTrave 3-6@i@p2@l4000@n6@e1,580@d8@g@s32@v@Gl4000@w0@C75@",
        "BF2D@HjAssemblato@rTrave 3-6@i@p3@l3168@n6@e2,810@d12@g@s70@v@Gl200@w-90@l3000@w0@C72@"};
    return index>=0 && index<3 ? QString::fromLatin1(values[index]) : QString();
}
QByteArray ScannerService::preparePayload(const QString& text,bool hex,bool appendCRLF) {
    QByteArray data;
    if(hex) {
        QByteArray digits;
        for(auto c:text) {
            if(c.isSpace()) continue;
            const auto u=c.unicode();
            if(!((u>='0' && u<='9') || (u>='A' && u<='F') || (u>='a' && u<='f')))
                throw std::runtime_error("RAW HEX requires hexadecimal byte pairs separated by optional whitespace");
            digits.append(char(u));
        }
        if(digits.size()%2) throw std::runtime_error("RAW HEX has an incomplete byte");
        data=QByteArray::fromHex(digits);
    } else {
        for(auto c:text) if(c.unicode()>127) throw std::runtime_error("ASCII mode accepts only ASCII characters; use RAW HEX for other bytes");
        // QTextEdit normalizes pasted line endings to LF; restore scanner CR/LF.
        data=text.toLatin1().replace("\r\n","\n").replace("\n","\r\n");
    }
    if(data.isEmpty()) throw std::runtime_error("Scanner payload is empty");
    if(appendCRLF && !data.endsWith("\r\n")) data.append("\r\n",2);
    if(data.size()>MaxPayload) throw std::runtime_error("Scanner payload exceeds 251 bytes including CR/LF; data was not sent or split");
    return data;
}
QString ScannerService::printable(const QByteArray& data) {
    QString text;
    for(unsigned char c:data) {
        if(c==13) text+="<CR>";
        else if(c==10) text+="<LF>";
        else if(c>=32 && c<=126) text+=QChar(c);
        else text+=QString("<%1>").arg(c,2,16,QChar('0')).toUpper();
    }
    return text;
}
ScannerService::ScannerService(MachineSession& session,QObject* parent,int timeoutMs):QObject(parent),session_(session) {
    timer_.setSingleShot(true); timer_.setInterval(timeoutMs);
    connect(&timer_,&QTimer::timeout,this,[this]{fail("Scanner command 9 timed out; delivery unknown, not retried",true);});
    connect(&session_,&MachineSession::received,this,&ScannerService::receive);
    connect(&session_,&MachineSession::stateChanged,this,[this](auto state){
        if(state==MachineSession::State::Disconnected) {
            ready_=false;
            if(busy_) fail("Disconnected during scanner command 9; delivery unknown, not retried");
        }
    });
}
void ScannerService::finish() { busy_=false; timer_.stop(); codec_.reset(); emit busyChanged(false); }
void ScannerService::fail(const QString& message,bool disconnect) {
    if(disconnect) ready_=false;
    finish(); if(disconnect) session_.stop();
    emit logLine("Scanner: "+message); emit failed(message);
}
void ScannerService::sendScannerPayload(const QByteArray& payload) {
    if(busy_ || !ready_ || session_.state()!=MachineSession::State::Connected) {
        emit failed("Scanner unavailable: read versions first and stop video / wait for current operation"); return;
    }
    if(payload.isEmpty() || payload.size()>MaxPayload) { emit failed("Scanner payload must contain 1..251 bytes"); return; }
    Packet packet; packet.command=CMD_SCANNER_RX_INJECT; packet.data=payload;
    codec_.reset(); busy_=true; timer_.start(); emit busyChanged(true);
    emit logLine(QString("Scanner TX command 9 | payload length=%1 | printable: %2 | hex: %3")
        .arg(payload.size()).arg(printable(payload)).arg(QString::fromLatin1(payload.toHex(' ').toUpper())));
    if(!session_.send(LegacyCodec::encode(packet)) && busy_) fail("Scanner command 9 write failed; not retried",true);
}
void ScannerService::receive(const QByteArray& bytes) {
    if(!busy_) return;
    try {
        for(const auto& p:codec_.feed(bytes)) {
            if(!busy_ || p.destination!=65 || p.sender!=66 || p.command!=CMD_SCANNER_RX_INJECT)
                throw std::runtime_error("Unexpected scanner reply");
            emit logLine(QString("Scanner RX command 9 | message=0x%1 | payload length=%2 | printable: %3 | hex: %4")
                .arg(p.message,2,16,QChar('0')).arg(p.data.size()).arg(printable(p.data)).arg(QString::fromLatin1(p.data.toHex(' ').toUpper())));
            if(p.message==69) {
                const auto code=quint8(p.data[0]);
                fail(code==3 ? "This Rotor108 firmware does not support Scanner RX Inject (command 9)." : QString("Scanner command 9 device error %1").arg(code)); return;
            }
            if((p.message!=87 && p.message!=82) || !p.data.isEmpty()) {
                fail("Unexpected Scanner RX Inject acknowledgement; expected empty SendAnswer"); return;
            }
            finish(); emit logLine("Scanner: command 9 acknowledged by Rotor108"); emit completed();
        }
    } catch(const std::exception& e) { fail(QString::fromUtf8(e.what()),true); }
}
}
