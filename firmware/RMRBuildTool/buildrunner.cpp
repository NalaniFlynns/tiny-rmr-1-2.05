#include "buildrunner.h"
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QTimer>

BuildRunner::BuildRunner(QObject* parent) : QObject(parent)
{
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray d = m_proc->readAllStandardOutput();
        const QStringList lines = QString::fromLocal8Bit(d).split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
        for (const QString& l : lines) emitLog(l);
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
        const QByteArray d = m_proc->readAllStandardError();
        const QStringList lines = QString::fromLocal8Bit(d).split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
        for (const QString& l : lines) emitLog(l);
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BuildRunner::onGmakeFinished);
}

BuildRunner::~BuildRunner() { stop(); }

void BuildRunner::emitLog(const QString& line)
{
    QString l = line.trimmed();
    if (l.isEmpty()) return;
    int kind = 0;
    if (l.contains("error", Qt::CaseInsensitive) || l.contains("failed", Qt::CaseInsensitive) ||
        l.contains("cannot", Qt::CaseInsensitive) || l.contains("undefined reference", Qt::CaseInsensitive))
        kind = 2;
    else if (l.startsWith("Finished building") || l.startsWith("Building") || l.contains("OK:"))
        kind = 1;
    emit logLine(l, kind);
}

void BuildRunner::startBuild(const QString& projectDir, bool allVariants, const QString& gmakePath,
                             const QString& version, const QString& note)
{
    if (m_running) return;
    m_projectDir = projectDir;
    m_allVariants = allVariants;
    m_gmakePath = gmakePath;
    m_version = version.trimmed();
    m_note = note.trimmed();
    m_configPath = QDir(projectDir).filePath("app_config.h");
    m_hexOutDir = QDir(projectDir).filePath("Debug");
    m_resultHex.clear();
    m_cancelled = false;

    QFile cfg(m_configPath);
    if (!cfg.open(QIODevice::ReadOnly)) {
        emit finished(false, {}, "无法读取 " + m_configPath);
        return;
    }
    m_origConfig = QString::fromUtf8(cfg.readAll());
    cfg.close();
    if (!QDir(m_hexOutDir).exists()) {
        emit finished(false, {}, "找不到 Debug 目录: " + m_hexOutDir);
        return;
    }

    m_running = true;
    m_step = 0;
    if (m_allVariants) {
        m_variantQueue = { "DBG", "DBGL", "DIRECT", "BATT", "ECO_D", "ECO_B" };
    } else {
        m_variantQueue = { "DBGL" };
    }
    nextStep();
}

void BuildRunner::nextStep()
{
    if (m_cancelled) { restoreDefines(); finish(false, "已停止"); return; }
    if (!m_variantQueue.isEmpty()) {
        QString name = m_variantQueue.takeFirst();
        VariantDef v;
        if (name == "DBG")          v = { "DBG", 1, 0, 1, 0 };
        else if (name == "DIRECT")  v = { "DIRECT", 1, 0, 0, 0 };
        else if (name == "BATT")    v = { "BATT", 0, 0, 0, 0 };
        else if (name == "ECO_D")   v = { "ECO_D", 1, 1, 0, 0 };
        else if (name == "ECO_B")   v = { "ECO_B", 0, 1, 0, 0 };
        else                        v = { "DBGL", 1, 0, 0, 1 };
        m_currentVariant = v.name;
        emit logLine(QString("===== 构建变体 %1 =====").arg(v.name), 1);
        emit progress(0, QString("变体 %1 / %2 ...").arg(m_resultHex.size() + 1).arg(m_allVariants ? 7 : 1));
        switchDefines(v);
        startGmake({ "clean" });
        return;
    }
    if (!m_restorePhase) {
        m_restorePhase = true;
        emit logLine("===== 还原默认配置 (DBGL) 并重新构建 =====", 1);
        emit progress(85, "还原默认配置 ...");
        restoreDefines();
        startGmake({ "clean" });
        return;
    }
    finish(true, {});
}

void BuildRunner::finish(bool ok, const QString& err)
{
    m_running = false;
    m_restorePhase = false;
    emit finished(ok, m_resultHex, err);
}

void BuildRunner::switchDefines(const VariantDef& v)
{
    QString t = m_origConfig;
    auto rep = [&t](const QString& key, int val) {
        QRegularExpression re(QString("#define %1\\s+\\d+").arg(key));
        t.replace(re, QString("#define %1 %2").arg(key, QString::number(val)));
    };
    rep("POWER_SOURCE_DIRECT", v.direct);
    rep("POWER_SAVE_BUILD", v.save);
    rep("DEBUG_BUILD", v.dbg);
    rep("DEBUG_LP_BUILD", v.dbl);
    // 自定义版本号: 替换 FW_VERSION_STR 的版本前缀, 保留变体后缀 (V4.3.6_DBG -> V4.4.0_DBG)
    if (!m_version.isEmpty()) {
        QRegularExpression reVer("FW_VERSION_STR \"V[0-9]+\\.[0-9]+\\.[0-9]+");
        t.replace(reVer, QString("FW_VERSION_STR \"%1").arg(m_version));
    }
    // 更新说明等额外信息不进固件, 仅随 FWSEC1 加密容器嵌入 (RMRDebugger 解码时读取)
    QFile f(m_configPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(t.toUtf8());
}

void BuildRunner::restoreDefines()
{
    QFile f(m_configPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(m_origConfig.toUtf8());
}

void BuildRunner::startGmake(const QStringList& args)
{
    m_proc->setWorkingDirectory(m_hexOutDir);
    m_proc->start(m_gmakePath, args);
    if (!m_proc->waitForStarted(5000)) {
        emit logLine("gmake 启动失败: " + m_proc->errorString(), 2);
        finish(false, "gmake 启动失败");
    }
}

void BuildRunner::onGmakeFinished(int code, QProcess::ExitStatus st)
{
    if (st != QProcess::NormalExit || code != 0) {
        if (!m_cancelled) emit logLine(QString("gmake 失败 (exit=%1)").arg(code), 2);
        restoreDefines();
        finish(false, QString("构建失败: %1 (exit=%2)").arg(m_currentVariant, QString::number(code)));
        return;
    }
    if (m_cancelled) { restoreDefines(); finish(false, "已停止"); return; }
    QString hex = QDir(m_hexOutDir).filePath("RMR.hex");
    if (QFile::exists(hex)) {
        QString outHex = QDir(m_hexOutDir).filePath(QString("RMR_%1.hex").arg(m_currentVariant));
        if (m_currentVariant == "DBGL")
            QFile::remove(outHex);
        QFile::copy(hex, outHex);
        m_resultHex << outHex;
        emit logLine(QString("OK: %1").arg(outHex), 1);
    }
    emit progress(m_allVariants ? int(10.0 * m_resultHex.size()) : 100,
                  QString("完成 %1").arg(m_currentVariant));
    QTimer::singleShot(50, this, [this] { nextStep(); });
}

void BuildRunner::stop()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_cancelled = true;
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
    if (m_running && !m_origConfig.isEmpty()) restoreDefines();
    m_running = false;
}