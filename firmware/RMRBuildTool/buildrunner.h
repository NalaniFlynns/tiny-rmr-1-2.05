#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>

struct VariantDef {
    QString name;
    int direct = 1;   // POWER_SOURCE_DIRECT
    int save   = 0;   // POWER_SAVE_BUILD
    int dbg    = 0;   // DEBUG_BUILD
    int dbl    = 0;   // DEBUG_LP_BUILD
};

// Runs gmake builds in a CCS project dir, optionally switching the 6 variants.
class BuildRunner : public QObject
{
    Q_OBJECT
public:
    explicit BuildRunner(QObject* parent = nullptr);
    ~BuildRunner();

    // projectDir: firmware/RMR (contains app_config.h and Debug/)
    // version: 自定义版本号(如 V4.4.0, 为空则用工程默认); note: 更新说明(为空则不修改)
    void startBuild(const QString& projectDir, bool allVariants, const QString& gmakePath,
                    const QString& version, const QString& note);
    void stop();
    bool running() const { return m_running; }

signals:
    void logLine(const QString& text, int kind);          // 0=normal 1=ok 2=err
    void progress(int percent, const QString& text);
    void finished(bool ok, const QStringList& hexFiles, const QString& error);

private:
    void runOneVariant(const VariantDef& v, bool keepConfig);
    void switchDefines(const VariantDef& v);
    void restoreDefines();
    void startGmake(const QStringList& args);
    void onGmakeFinished(int code, QProcess::ExitStatus st);
    void nextStep();
    void finish(bool ok, const QString& err);
    void emitLog(const QString& line);

    QString m_projectDir;
    QString m_configPath;
    QString m_origConfig;
    QProcess* m_proc = nullptr;
    bool m_running = false;
    bool m_allVariants = false;
    QStringList m_variantQueue;      // hex names already built
    QString m_currentVariant;
    QString m_hexOutDir;
    QStringList m_resultHex;
    int m_step = 0;
    bool m_restorePhase = false;
    QString m_gmakePath;
    QString m_version;
    QString m_note;
    bool m_cancelled = false;
};