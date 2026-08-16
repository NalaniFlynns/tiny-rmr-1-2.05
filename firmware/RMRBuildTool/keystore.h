#pragma once
#include <QString>
#include <QByteArray>
#include <vector>

struct FidoCredential {
    QString name;
    QString rpId;
    QByteArray credId;   // binary
    int device = 1;      // 1=cross-platform key, 2=platform/Windows Hello
    QString yubicoId;    // YubiKey 序列号/ID (可选, 加密时嵌入构建元信息)
};

class Keystore
{
public:
    static QString path();
    static bool load(std::vector<FidoCredential>& out, QString& err);
    static bool save(const std::vector<FidoCredential>& creds, QString& err);
};