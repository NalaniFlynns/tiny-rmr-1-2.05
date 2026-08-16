#include "fwsecflash.h"
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QDateTime>

bool fwsec_prepare_flash(const QString& path,
                         const std::function<FwsecUnlockResult(const fwsec::FwsecFileInfo&)>& ask,
                         QString& plainPath, QString& err)
{
    if (!path.endsWith(".fwsec", Qt::CaseInsensitive)) {
        plainPath = path;
        return true;
    }
    fwsec::FwsecFileInfo info;
    if (!fwsec::fwsec_probe(path.toStdString(), info)) {
        err = "加密固件解析失败: " + QString::fromStdString(info.error);
        return false;
    }
    FwsecUnlockResult r;
    if (ask) r = ask(info);
    if (r.cancelled) { err = "已取消解密"; return false; }
    if (!r.error.empty()) { err = QString::fromStdString(r.error); return false; }

    // Private temp file with a random name; .hex extension required by openocd.
    QString tmp = QDir::temp().filePath(
        QString("rmr_fwsec_%1_%2.hex")
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
    std::string cerr;
    if (!fwsec::fwsec_decrypt_file(path.toStdString(), tmp.toStdString(),
                                   r.password, r.fido_secret,
                                   nullptr, nullptr, cerr)) {
        err = "解密失败: " + QString::fromStdString(cerr);
        return false;
    }
    QFile f(tmp);
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    plainPath = tmp;
    return true;
}

void fwsec_cleanup(const QString& plainPath)
{
    if (plainPath.isEmpty()) return;
    if (plainPath.endsWith(".fwsec", Qt::CaseInsensitive)) return;  // never touch the container
    QFile::remove(plainPath);
}