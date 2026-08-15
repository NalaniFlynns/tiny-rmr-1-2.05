#pragma once
#include <QWidget>
#include <QVariantMap>
#include <QVector>
#include <QStringList>
#include <QElapsedTimer>
#include <functional>

#include "DebugWorkers.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

/* 功能测试插件: 引导式自动判定 (LED / 光感 / 按键 / DM码)
 * 通过后显示绿色 PASS, 任一失败显示红色 FAIL 并给出原因。 */
class FuncTestPanel : public QWidget {
    Q_OBJECT
public:
    explicit FuncTestPanel(QWidget *parent = nullptr);

    void setCommandSender(std::function<void(const Command&)> sender);
    void setUuidGetter(std::function<QString(uint32_t sn)> getter);
    void setActiveSnProvider(std::function<uint32_t()> provider);
    void setPollingController(std::function<void(uint32_t sn, bool fast)> ctrl);
    void setPowerZProvider(std::function<QVariantMap()> provider);

    void updateTelemetry(uint32_t sn, const QVariantMap& data);
    void startTest(uint32_t sn);
    void stopTest();

signals:
    void sigLog(const QString& text);

private:
    enum Phase {
        PhIdle, PhPowerOn, PhLedBase, PhLedFull, PhLedEcho, PhSetAls, PhAlsDark, PhAlsBright,
        PhKeyBT1, PhKeyBT1Rel, PhKeyBT2, PhKeyBT2Rel, PhDm, PhRestoreMode, PhDone
    };
    void tick();
    void setPhase(Phase p);
    void setInstruction(const QString& s);
    void setStepState(int idx, int state);   /* 0 wait, 1 run, 2 pass, 3 fail */
    void updateLive();
    qint64 phaseElapsed() const;
    static qint64 phaseTimeoutMs(Phase p);
    static const char *phaseLabel(Phase p);
    void finish(bool ok, const QString& reason);
    void completeFinish(bool ok, const QString& reason);
    void sendCmd(const Command& c);
    bool hasState(int state) const;
    static int medianOf(const QVector<int>& v);

    QTimer *m_timer = nullptr;
    uint32_t m_sn = 0xFFFFFFFF;
    QVariantMap m_tele;
    Phase m_phase = PhIdle;
    QElapsedTimer m_phaseClock;
    QElapsedTimer m_teleClock;
    bool m_teleSeen = false;
    bool m_paused = false;
    qint64 m_pauseMs = 0;
    QElapsedTimer m_pauseClock;
    bool m_running = false;
    bool m_ledRestored = false;
    bool m_physKeysBlocked = false;
    bool m_pollFast = false;
    bool m_alsSwitched = false;
    bool m_resultPendingOk = false;
    QString m_resultPendingReason;
    int m_ledVbatOff = 0;      /* LED 关灯基准 VBAT(mV, 中位数) */
    int m_ledVbatOn = 0;       /* LED 全亮 VBAT(mV, 中位数) */
    int m_ledIavgOn = 0;       /* LED 全亮模型电流(uA, 中位数) */
    int m_ledSafeBrtOn = 0;    /* LED 全亮 safe_brt(中位数) */
    double m_pzOff = -1.0;     /* PowerZ 关灯电流(mA), -1=未接入 */
    double m_pzOn = -1.0;      /* PowerZ 全亮电流(mA) */
    QVector<int> m_vbuf;       /* 采样缓冲(中位数滤波) */
    QVector<int> m_iabuf;
    QVector<int> m_sbbuf;

    QLabel *m_instr = nullptr;
    QLabel *m_result = nullptr;
    QLabel *m_live = nullptr;
    QLabel *m_dmImage = nullptr;
    QPushButton *m_btnStart = nullptr;
    QPushButton *m_btnStop = nullptr;
    QTableWidget *m_stepTable = nullptr;
    QVector<int> m_stepState;

    std::function<void(const Command&)> m_sendCmd;
    std::function<QString(uint32_t)> m_getUuid;
    std::function<uint32_t()> m_getActiveSn;
    std::function<void(uint32_t, bool)> m_pollCtrl;
    std::function<QVariantMap()> m_pzProvider;
};
