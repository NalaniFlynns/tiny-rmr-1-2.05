#include "encryptworker.h"
#include "fwsec_container.h"
#include "fwsec_util.h"
#include "fwsec_webauthn.h"
#include "fwsec_ctap2.h"
#include <QFileInfo>

void EncryptWorker::doEncrypt(EncryptJob job)
{
    using namespace fwsec;
    FwsecEncryptOptions opt;
    opt.auth_type = (job.authType == 2) ? AUTH_FIDO2 : (job.authType == 3 ? AUTH_PASSKEY : AUTH_PASSWORD);
    opt.password = job.password.toStdString();

    // FIDO2 / 通行证: 在加密线程中触碰密钥派生 32 字节密钥 (UI 不冻结)
    if (job.authType == 2 || job.authType == 3) {
        if (job.salt.size() != 16 || job.credId.isEmpty() || job.rpId.isEmpty()) {
            emit finished(false, "凭据信息缺失", "加密失败: 凭据信息缺失");
            return;
        }
        emit status(job.authType == 2
            ? "请触碰 FIDO2 安全密钥 (30 秒内) ..."
            : "请完成 Windows Hello / 通行证验证 ...");
        u8 prfSalt[32], secret[32];
        webauthn_prf_salt((const u8*)job.salt.constData(), prfSalt);
        std::vector<uint8_t> cid(job.credId.begin(), job.credId.end());
        if (job.device == 1) {
            ctap2_set_status_cb([](const char*, void* ctx) {
                QObject* obj = static_cast<QObject*>(ctx);
                QMetaObject::invokeMethod(obj, [obj] {
                    auto* w = qobject_cast<EncryptWorker*>(obj);
                    if (w) emit w->status("请触碰 FIDO2 安全密钥 ...");
                }, Qt::QueuedConnection);
            }, this);
        }
        std::string werr;
        bool okDerive = webauthn_get_prf_secret(nullptr, job.rpId.toStdString(), cid,
                                                (u8)job.device, prfSalt, secret, werr);
        ctap2_set_status_cb(nullptr, nullptr);
        if (!okDerive) {
            QString msg = QString::fromStdString(werr);
            if (msg.contains("NotSupportedError"))
                msg += "\n\n手机跨设备通行证不支持密钥派生 (PRF), 请改用 FIDO2 安全密钥或 Windows Hello。";
            secure_zero(secret, sizeof(secret));
            secure_zero(prfSalt, sizeof(prfSalt));
            emit finished(false, msg, "加密失败: " + msg);
            return;
        }
        opt.fido_secret.assign(secret, secret + 32);
        opt.cred_id = cid;
        opt.rp_id = job.rpId.toStdString();
        opt.device = (u8)job.device;
        secure_zero(secret, sizeof(secret));
        secure_zero(prfSalt, sizeof(prfSalt));
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
