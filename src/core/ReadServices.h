#pragma once
#include "LegacyProtocol.h"
namespace ezconn {
struct DirectoryEntry { QString name; bool directory; };
struct DiskInfo {
    qint32 serial; quint32 clusterBytes, totalClusters, freeClusters, badClusters;
    quint64 totalBytes() const { return quint64(clusterBytes)*totalClusters; }
    quint64 freeBytes() const { return quint64(clusterBytes)*freeClusters; }
};
class ReadServices : public QObject {
    Q_OBJECT
public:
    explicit ReadServices(MachineSession& session,QObject* parent=nullptr);
    void setReady(bool ready);
    void listDirectory(const QString& path);
    void readDisk(const QString& drive);
    static QList<DirectoryEntry> parseDirectory(const QByteArray& data);
    static DiskInfo parseDisk(const QByteArray& data);
signals:
    void directoryReady(const QString& path,const QList<ezconn::DirectoryEntry>& entries);
    void diskReady(const QString& drive,const ezconn::DiskInfo& info);
    void busyChanged(bool busy);
    void failed(const QString& message);
private:
    bool begin(const QString& path);
    void request(quint8 command,const QByteArray& data={});
    void receive(const QByteArray& bytes);
    void finish();
    void fail(const QString& message,bool disconnect=false);
    MachineSession& session_;
    LegacyCodec codec_;
    QTimer timer_;
    bool ready_=false;
    int expected_=0;
    QString path_;
};
}
