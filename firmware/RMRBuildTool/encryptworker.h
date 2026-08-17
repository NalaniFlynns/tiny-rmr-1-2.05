#pragma once
#include <QObject>
#include <QString>
#include <QByteArray>

struct EncryptJob {
    int authType = 1;            // 1=password 2=fido2 3=passkey
    QString password;
    QByteArray fidoSecret;       // 32 bytes (PRF output), 工作线程派生
    QByteArray salt;             // 16 bytes container salt
    QByteArray credId;
    QString rpId;
    int device = 1;
    QString fileVersion;
    QString updateNote;
    QString buildMeta;
    QString inPath;
    QString outPath;
    QString fileName = "RMR.hex";
    quint32 blockSize = 16384;
};

Q_DECLARE_METATYPE(EncryptJob)

class EncryptWorker : public QObject
{
    Q_OBJECT
public:
    explicit EncryptWorker(QObject* parent = nullptr) : QObject(parent) {}
public slots:
    void doEncrypt(EncryptJob job);
signals:
    void progress(qint64 done, qint64 total);
    void status(const QString& msg);
    void finished(bool ok, const QString& error, const QString& summary);
};
