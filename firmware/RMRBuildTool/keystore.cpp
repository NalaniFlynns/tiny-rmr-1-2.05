#include "keystore.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

QString Keystore::path()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QDir::home().filePath(".rmrbuildtool");
    QDir().mkpath(dir);
    return dir + "/fwsec_creds.json";
}

bool Keystore::load(std::vector<FidoCredential>& out, QString& err)
{
    out.clear();
    QFile f(path());
    if (!f.exists()) return true;
    if (!f.open(QIODevice::ReadOnly)) { err = "无法读取凭据库: " + f.errorString(); return false; }
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) { err = "凭据库损坏: " + pe.errorString(); return false; }
    QJsonArray arr = doc.object().value("credentials").toArray();
    for (const QJsonValue& v : arr) {
        QJsonObject o = v.toObject();
        FidoCredential c;
        c.name = o.value("name").toString();
        c.rpId = o.value("rp_id").toString();
        c.credId = QByteArray::fromBase64(o.value("cred_id_b64").toString().toUtf8());
        c.device = o.value("device").toInt(1);
        c.yubicoId = o.value("yubico_id").toString();
        if (c.name.isEmpty() || c.rpId.isEmpty() || c.credId.isEmpty()) continue;
        out.push_back(c);
    }
    return true;
}

bool Keystore::save(const std::vector<FidoCredential>& creds, QString& err)
{
    QJsonArray arr;
    for (const FidoCredential& c : creds) {
        QJsonObject o;
        o.insert("name", c.name);
        o.insert("rp_id", c.rpId);
        o.insert("cred_id_b64", QString::fromLatin1(c.credId.toBase64()));
        o.insert("device", c.device);
        if (!c.yubicoId.isEmpty()) o.insert("yubico_id", c.yubicoId);
        arr.append(o);
    }
    QJsonObject root; root.insert("credentials", arr);
    QFile f(path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { err = "无法写入凭据库: " + f.errorString(); return false; }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}