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

    void updateTelemetry(uint32_t sn, const QVariantMap& data);
    void startTest(uint32_t sn);
    void stopTest();

signals:
    void sigLog(const QString& text);

private:
    enum Phase {
        PhIdle, PhPowerOn, PhLed, PhSetAls, PhAlsDark, PhAlsBright,
        PhKeyBT1, PhKeyBT1Rel, PhKeyBT2, PhKeyBT2Rel, PhDm, PhRestoreMode, PhDone
    };
    void tick();
    void setPhase(Phase p);
    void setInstruction(const QString& s);
    void setStepState(int idx, int state);   /* 0 wait, 1 run, 2 pass, 3 fail */
    void finish(bool ok, const QString& reason);
    void completeFinish(bool ok, const QString& reason);
    void sendCmd(const Command& c);
    bool hasState(int state) const;

    QTimer *m_timer = nullptr;
    uint32_t m_sn = 0xFFFFFFFF;
    QVariantMap m_tele;
    Phase m_phase = PhIdle;
    QElapsedTimer m_phaseClock;
    bool m_running = false;
    bool m_ledRestored = false;
    bool m_physKeysBlocked = false;
    bool m_pollFast = false;
    bool m_alsSwitched = false;
    bool m_resultPendingOk = false;
    QString m_resultPendingReason;
    int m_ledVbatStart = 0;

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
};
