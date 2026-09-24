#pragma once
#include "LegacyProtocol.h"
#include <optional>
namespace ezconn {
class RemoteKey {
public:
    static std::optional<quint8> fromQt(int key) {
        if(key>=Qt::Key_F1 && key<=Qt::Key_F12) return quint8(0x70+key-Qt::Key_F1);
        if(key>=Qt::Key_0 && key<=Qt::Key_9) return quint8(0x30+key-Qt::Key_0);
        if(key>=Qt::Key_A && key<=Qt::Key_Z) return quint8(0x41+key-Qt::Key_A);
        switch(key) {
        case Qt::Key_Home:return 0x24;
        case Qt::Key_Left:return 0x25;
        case Qt::Key_Up:return 0x26;
        case Qt::Key_Right:return 0x27;
        case Qt::Key_Down:return 0x28;
        case Qt::Key_Return:case Qt::Key_Enter:return 0x0d;
        case Qt::Key_Delete:return 0x2e;
        default:return std::nullopt;
        }
    }
    static QString name(quint8 key) {
        if(key>=0x70 && key<=0x7b) return QString("F%1").arg(key-0x70+1);
        if((key>=0x30 && key<=0x39) || (key>=0x41 && key<=0x5a)) return QString(QChar(key));
        switch(key) {case 0x24:return "Home";case 0x25:return "Left";case 0x26:return "Up";
        case 0x27:return "Right";case 0x28:return "Down";case 0x0d:return "Enter";case 0x2e:return "Delete";default:return {};}
    }
    static Packet keyboardPacket(quint8 key) {
        Packet p; p.command=66; p.data=QByteArray(6,char(0));
        // Original UCStreamVideo_KeyPress sends digits as characters in byte 5.
        // Keep the existing virtual-key path for the other supported keys.
        const int field=(key>=0x30 && key<=0x39) ? 5 : 4;
        p.data[field]=char(key); return p;
    }
    static Packet touchPacket(quint16 x,quint16 y) {
        Packet p; p.command=66; p.data=QByteArray(6,char(0));
        p.data[0]=char(x&255); p.data[1]=char(x>>8);
        p.data[2]=char(y&255); p.data[3]=char(y>>8); return p;
    }
};
}
