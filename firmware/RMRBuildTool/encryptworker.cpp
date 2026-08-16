#include "encryptworker.h"
#include "fwsec_container.h"
#include "fwsec_util.h"
#include <QFileInfo>

void EncryptWorker::doEncrypt(EncryptJob job)
{
    using namespace fwsec;
    FwsecEncryptOptions opt;
    opt.auth_type = (job.authType == 2) ? AUTH_FIDO2 : (job.authType == 3 ? AUTH_PASSKEY : AUTH_PASSWORD);
    opt.password = job.password.toStdString();
    if (job.fidoSecret.size() == 32) {
        opt.fido_secret.assign(job.fidoSecret.begin(), job.fidoSecret.end());
        opt.cred_id.assign(job.credId.begin(), job.credId.end());
        opt.rp_id = job.rpId.toStdString();
        opt.device = (u8)job.device;
    }
    if (job.salt.size() == 16) {
        memcpy(opt.salt, job.salt.constData(), 16);
        opt.use_salt = true;
    }
    opt.file_name = job.fileName.toStdString();
    opt.file_version = job.fileVersion.toStdString();
    opt.update_note = job.updateNote.toStdString();
    opt.build_meta = job.buildMeta.toStdString();
    opt.block_size = job.blockSize;
    opt.progress = [](void* ctx, u64 done, u64 total) -> bool {
        QObject* obj = static_cast<QObject*>(ctx);
        // Emit through the queued connection from the worker thread.
        QMetaObject::invokeMethod(obj, [obj, done, total] {
            auto* w = qobject_cast<EncryptWorker*>(obj);
            if (w) emit w->progress((qint64)done, (qint64)total);
        }, Qt::QueuedConnection);
        return true;
    };
    opt.progress_ctx = this;

    std::string err;
    bool ok = fwsec_encrypt_file(job.inPath.toStdString(), job.outPath.toStdString(), opt, err);

    QFileInfo fi(job.outPath);
    QString summary;
    if (ok) {
        QString method = job.authType == 2 ? "FIDO2 安全密钥" : (job.authType == 3 ? "手机通行证 / Windows Hello" : "自动强密码");
        summary = QString("加密完成\n方式: %1\n容器: FWSEC1 v2 (ML-KEM-768 + ChaCha20-Poly1305)\n分块: %2 KB\n输出: %3\n大小: %4 KB\n嵌入: 版本 / 更新说明 / 构建元信息")
                      .arg(method)
                      .arg(job.blockSize / 1024)
                      .arg(fi.fileName())
                      .arg(fi.size() / 1024.0, 0, 'f', 1);
    } else {
        summary = "加密失败: " + QString::fromStdString(err);
    }
    emit finished(ok, QString::fromStdString(err), summary);
}