#include "ScreenService.h"
#include <QtEndian>
#include <QStandardPaths>
#include <QDir>
#include <QSaveFile>
#include <QDateTime>
#include <stdexcept>
#include <cstring>
namespace ezconn {
QImage ScreenService::decodePcx(const QByteArray& b,const QImage& previous) {
    if(b.size()<128 || quint8(b[0])!=10 || quint8(b[2])!=1 || quint8(b[3])!=8 || quint8(b[65])!=3)
        throw std::runtime_error("Expected 24-bit RLE PCX screen image");
    auto u16=[&](int p){return int(qFromLittleEndian<quint16>(b.constData()+p));};
    const int x=u16(4),y=u16(6),w=u16(8)-x+1,h=u16(10)-y+1,stride=u16(66);
    // Delta headers describe the update rectangle, not the persistent screen size.
    // The original BMP compositor uses XscreenBase/YscreenBase from the first frame.
    int sw=previous.isNull()?u16(70):previous.width();
    int sh=previous.isNull()?u16(72):previous.height();
    if(!sw) sw=x+w;
    if(!sh) sh=y+h;
    if(w<=0 || h<=0 || stride<w || stride>8192 || sw<=0 || sh<=0 || sw>4096 || sh>4096 || x+w>sw || y+h>sh)
        throw std::runtime_error("Invalid PCX dimensions");
    if(previous.isNull() && (x || y || w!=sw || h!=sh)) throw std::runtime_error("First frame must cover the full screen");
    QImage output=previous.isNull()?QImage(sw,sh,QImage::Format_RGB32):previous.copy();
    if(output.isNull()) throw std::runtime_error("Cannot allocate screen image");
    qsizetype pos=128;
    QByteArray scan(stride*3,char(0));
    for(int row=0;row<h;++row) {
        int filled=0;
        while(filled<stride*3) {
            if(pos>=b.size()) throw std::runtime_error("Truncated PCX data");
            quint8 value=quint8(b[pos++]); int count=1;
            if((value&0xc0)==0xc0) {
                count=value&0x3f;
                if(pos>=b.size()) throw std::runtime_error("Truncated PCX run at byte " + std::to_string(pos-1) + ", row " + std::to_string(row));
                value=quint8(b[pos++]);
                // Match the original PCX constructor: C0 consumes its value but emits zero bytes.
                // The input cursor still advances, so zero-length runs cannot stall decoding.
            }
            if(filled+count>stride*3) throw std::runtime_error("PCX run exceeds scanline");
            std::memset(scan.data()+filled,value,size_t(count)); filled+=count;
        }
        auto* pixels=reinterpret_cast<QRgb*>(output.scanLine(y+row));
        for(int col=0;col<w;++col) {
            auto color=qRgb(quint8(scan[col]),quint8(scan[stride+col]),quint8(scan[2*stride+col]));
            // Original BMP merger treats RGB B5 B7 F8 as unchanged in delta frames.
            if(previous.isNull() || color!=qRgb(181,183,248)) pixels[x+col]=color;
        }
    }
    return output;
}
ScreenService::ScreenService(MachineSession& session,QObject* parent,int responseWaitMs,int blockIdleMs):QObject(parent),session_(session),responseWaitMs_(qMax(1,responseWaitMs)),blockIdleMs_(qMax(1,blockIdleMs)) {
    timeout_.setSingleShot(true); timeout_.setInterval(5000);
    poll_.setSingleShot(true); poll_.setInterval(100);
    connect(&timeout_,&QTimer::timeout,this,[this]{
        if(expected_==67 && requestWait_.elapsed()<responseWaitMs_) {
            emit status(QString("Waiting for screen update (%1 s)...").arg(requestWait_.elapsed()/1000));
            timeout_.start(qMin(5000,int(responseWaitMs_-requestWait_.elapsed())));
            return;
        }
        fail(QString("Screen response timeout: command %1, block %2, elapsed %3 ms, frame bytes %4")
            .arg(expected_).arg(block_).arg(requestWait_.elapsed()).arg(frame_.size()));
    });
    connect(&poll_,&QTimer::timeout,this,[this]{if(active_) request(67,QByteArray(1,char(0)));});
    connect(&session_,&MachineSession::received,this,&ScreenService::receive);
    connect(&session_,&MachineSession::stateChanged,this,[this](auto state){
        if(state==MachineSession::State::Disconnected && active_) { active_=false; expected_=0; timeout_.stop(); poll_.stop(); emit activeChanged(false); }
    });
}
void ScreenService::start(bool continuous) {
    if(active_) return;
    if(session_.state()!=MachineSession::State::Connected) { emit failed("Not connected"); return; }
    codec_.reset(); image_=QImage(); frame_.clear(); continuous_=continuous; stopping_=false; active_=true;
    firstFrameWait_.start();
    fpsClock_.start(); frameCount_=0; cycle_.start();
    emit activeChanged(true); request(67,QByteArray(1,char(1)));
}
void ScreenService::stop() {
    if(!active_) return;
    stopping_=true; continuous_=false; poll_.stop();
    // Drain the outstanding response before allowing another service to use the socket.
    if(!expected_) { active_=false; emit activeChanged(false); }
}
void ScreenService::fail(const QString& error) {
    active_=false; expected_=0; timeout_.stop(); poll_.stop(); session_.stop(); emit activeChanged(false); emit failed(error);
}
void ScreenService::request(quint8 command,const QByteArray& data) {
    if(command==67 && !cycle_.isValid()) cycle_.start();
    expected_=command; requestWait_.start();
    timeout_.start(command==67 ? qMin(5000,responseWaitMs_) : blockIdleMs_);
    Packet p; p.command=command; p.data=data;
    if(!session_.send(LegacyCodec::encode(p))) fail("Screen request write failed");
}
void ScreenService::nextBlock() {
    QByteArray data; data.append(char(block_&255)); if(block_>255) data.append(char((block_>>8)&255));
    data.append(char(blockKiB_)); request(26,data);
}
void ScreenService::receive(const QByteArray& bytes) {
    if(!active_ || !expected_) return;
    if(expected_==26 && !bytes.isEmpty()) {
        // Receiving a block can take longer than its idle timeout; bound total time as well.
        const auto remaining=60000-requestWait_.elapsed();
        if(remaining<=0) { fail("Screen block transfer exceeded 60 seconds"); return; }
        timeout_.start(qMin(blockIdleMs_,int(remaining)));
    }
    try {
        for(const auto& p:codec_.feed(bytes)) {
            if(p.destination!=65 || p.sender!=66 || p.command!=expected_) throw std::runtime_error("Unexpected screen response");
            timeout_.stop(); expected_=0;
            if(p.message==69) throw std::runtime_error("Screen device error " + std::to_string(quint8(p.data[0])));
            if(stopping_) { active_=false; emit activeChanged(false); return; }
            if(p.command==67) {
                if(p.data.size()!=2) {
                    if(!continuous_ && firstFrameWait_.elapsed()>=15000) {
                        active_=false; emit activeChanged(false); emit failed("No screen image received within 15 seconds");
                    } else poll_.start(100);
                    continue;
                }
                const int sizeKiB=qFromLittleEndian<quint16>(p.data.constData());
                if(!sizeKiB || sizeKiB>16384) throw std::runtime_error("Invalid screen transfer size");
                captureMs_=cycle_.elapsed(); transfer_.start();
                blocks_=(sizeKiB+blockKiB_-1)/blockKiB_; block_=0; frame_.clear(); frame_.reserve(sizeKiB*1024); nextBlock();
            } else {
                if(p.type!=88) throw std::runtime_error("Video requires large turbo packets (X)");
                if(p.data.isEmpty() || p.data.size()>blockKiB_*1024 || frame_.size()+p.data.size()>16*1024*1024) throw std::runtime_error("Invalid screen block size");
                frame_+=p.data;
                if(++block_<blocks_) { nextBlock(); continue; }
                const auto transferMs=transfer_.elapsed(); const auto packedBytes=frame_.size();
                QElapsedTimer decode; decode.start();
                try { frame_=LegacyCodec::decompress(frame_); image_=decodePcx(frame_,image_); }
                catch(const std::exception& e) {
                    QString message=QString::fromUtf8(e.what())+QString("; received %1 bytes, %2 blocks; header: %3")
                        .arg(frame_.size()).arg(blocks_).arg(QString::fromLatin1(frame_.left(80).toHex(' ')));
                    const auto directory=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/EzConn/diagnostics";
                    if(QDir().mkpath(directory)) {
                        const auto path=directory+"/screen-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz")+".pcx";
                        QSaveFile file(path);
                        if(file.open(QIODevice::WriteOnly) && file.write(frame_)==frame_.size() && file.commit()) message+="; saved: "+path;
                        else message+="; diagnostic file could not be saved";
                    } else message+="; diagnostic directory could not be created";
                    // The complete reply was consumed: only decoding failed. Keep the connection usable.
                    active_=false; expected_=0; timeout_.stop(); poll_.stop(); emit activeChanged(false); emit failed(message); return;
                }
                emit frameReady(image_);
                ++frameCount_;
                emit statistics(QString("FPS %1 | capture/wait %2 ms | transfer %3 ms | decode/display %4 ms | %5 KiB, %6 blocks x %7 KiB%8")
                    .arg(1000.0*frameCount_/qMax<qint64>(1,fpsClock_.elapsed()),0,'f',1).arg(captureMs_).arg(transferMs).arg(decode.elapsed())
                    .arg(packedBytes/1024.0,0,'f',1).arg(blocks_).arg(blockKiB_).arg(" | turbo"));
                cycle_.invalidate();
                if(continuous_) poll_.start(0); else { active_=false; emit activeChanged(false); }
            }
        }
    } catch(const std::exception& e) { fail(QString::fromUtf8(e.what())); }
}
}
