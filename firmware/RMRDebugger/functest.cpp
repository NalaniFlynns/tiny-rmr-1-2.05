#include "functest.h"
#include "datamatrix.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QPixmap>
#include <QDateTime>
#include <QFont>
#include <algorithm>

/* ============================================================================
 * 功能测试插件 (FuncTestPanel)
 * 步骤: 开机 -> LED(关灯基准/全亮压降/PWM回显 -> 开路短路判定) -> 光感暗 -> 光感亮
 *       -> BT1 -> BT2 -> DM码 -> PASS/FAIL
 * 自动判定, 操作员仅需按提示遮挡光感 / 按按键。全部通过显示绿色 PASS。
 * - LED 开路/短路: 全亮时 VBAT 压降(开路≈0mV/正常 20-130mV/短路>250mV),
 *   接入 PowerZ 时用真实电流变化交叉验证(开路无变化/短路远大于额定)。
 * - 掉线保护: 遥测中断>2.5s 自动暂停倒计时等待重连, 恢复后继续。
 * ========================================================================== */

namespace {
const QStringList kStepNames = {
    "LED 点亮/回显",
    "光感 - 遮挡(暗)",
    "光感 - 见光(亮)",
    "按键 BT1(+)",
    "按键 BT2(-)",
    "DM 码显示",
};
/* 光感阈值: OPT3001 分辨率为 0.01 lux/bit, 固件 lux_raw 即 0.01 lux, 真实 lux = lux_raw/100 */
constexpr int kAlsDarkLux   = 10;      /* 真实 lux: 遮挡后须 < 10 lux */
constexpr int kAlsBrightLux = 40;      /* 真实 lux: 见光后须 > 40 lux */
constexpr int kKeyTimeoutMs = 20000;
constexpr int kAlsTimeoutMs = 25000;
constexpr int kLedStageTimeoutMs = 6000;
/* LED 全亮压降判定 (mV): 开路≈0, 正常 20-130(电池版), 短路 >250 */
constexpr int kLedOpenDropMv  = 8;
constexpr int kLedShortDropMv = 250;
/* PowerZ 真实电流变化判定 (mA): 开路≈0, 正常 ≈ 固件模型 i_avg, 短路远超额定 */
constexpr double kPzOpenDeltaMa  = 0.5;
constexpr double kPzShortDeltaMa = 5.0;
/* 掉线保护: 遥测超过 2.5s 视为掉线暂停; 暂停超 2min 中止 */
constexpr qint64 kTeleStaleMs   = 2500;
constexpr qint64 kPauseLimitMs  = 120000;
}

FuncTestPanel::FuncTestPanel(QWidget *parent) : QWidget(parent) {
    QVBoxLayout *v = new QVBoxLayout(this);

    QLabel *title = new QLabel("功能测试插件 (自动判定)");
    title->setStyleSheet("font-size: 13pt; font-weight: bold; color: #004085; border: none;");
    v->addWidget(title);

    QHBoxLayout *hBtn = new QHBoxLayout();
    m_btnStart = new QPushButton("开始测试");
    m_btnStart->setObjectName("BtnGreen");
    m_btnStop = new QPushButton("停止");
    m_btnStop->setEnabled(false);
    hBtn->addWidget(m_btnStart);
    hBtn->addWidget(m_btnStop);
    hBtn->addStretch();
    v->addLayout(hBtn);

    m_result = new QLabel("IDLE");
    m_result->setAlignment(Qt::AlignCenter);
    m_result->setStyleSheet("font-size: 26pt; font-weight: bold; color: #6C757D; border: 1px solid #CCC; border-radius: 6px; background: #F8F9FA;");
    m_result->setMinimumHeight(64);
    v->addWidget(m_result);

    m_instr = new QLabel("选择已连接的探针后点击“开始测试”。");
    m_instr->setWordWrap(true);
    m_instr->setStyleSheet("font-size: 11pt; font-weight: bold; color: #333; border: 1px solid #B8D4E3; border-radius: 4px; background: #EAF4FB; padding: 8px;");
    m_instr->setMinimumHeight(64);
    v->addWidget(m_instr);

    m_stepTable = new QTableWidget(6, 2);
    m_stepTable->setHorizontalHeaderLabels({"测试项", "结果"});
    m_stepTable->verticalHeader()->setVisible(false);
    m_stepTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stepTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_stepTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stepTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_stepState.fill(0, 6);
    for (int i = 0; i < 6; i++) {
        QTableWidgetItem *nameItem = new QTableWidgetItem(kStepNames[i]);
        QTableWidgetItem *stItem = new QTableWidgetItem("待测");
        stItem->setTextAlignment(Qt::AlignCenter);
        m_stepTable->setItem(i, 0, nameItem);
        m_stepTable->setItem(i, 1, stItem);
    }
    v->addWidget(m_stepTable);

    QHBoxLayout *hLive = new QHBoxLayout();
    m_live = new QLabel("PWM: - | Lux: - | BT1: - | BT2: - | VBAT: -");
    m_live->setStyleSheet("font-family: Consolas; font-size: 9pt; color: #555; border: none;");
    m_live->setMinimumHeight(14);
    hLive->addWidget(m_live);
    hLive->addStretch();
    v->addLayout(hLive);

    m_dmImage = new QLabel();
    m_dmImage->setAlignment(Qt::AlignCenter);
    m_dmImage->setStyleSheet("border: 1px solid #CCC; background: white;");
    m_dmImage->setFixedSize(240, 240);
    m_dmImage->setText("DM 码将在此显示");
    m_dmImage->hide();
    v->addWidget(m_dmImage, 0, Qt::AlignCenter);
    v->addStretch();

    connect(m_btnStart, &QPushButton::clicked, this, [this](){
        uint32_t sn = m_getActiveSn ? m_getActiveSn() : 0xFFFFFFFF;
        startTest(sn);
    });
    connect(m_btnStop, &QPushButton::clicked, this, [this](){ stopTest(); });

    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this](){ tick(); });
}

void FuncTestPanel::setCommandSender(std::function<void(const Command&)> sender) { m_sendCmd = std::move(sender); }
void FuncTestPanel::setUuidGetter(std::function<QString(uint32_t)> getter) { m_getUuid = std::move(getter); }
void FuncTestPanel::setActiveSnProvider(std::function<uint32_t()> provider) { m_getActiveSn = std::move(provider); }
void FuncTestPanel::setPollingController(std::function<void(uint32_t, bool)> ctrl) { m_pollCtrl = std::move(ctrl); }
void FuncTestPanel::setPowerZProvider(std::function<QVariantMap()> provider) { m_pzProvider = std::move(provider); }

void FuncTestPanel::updateTelemetry(uint32_t sn, const QVariantMap& data) {
    if (sn == m_sn) {
        m_tele = data;
        m_teleSeen = true;
        m_teleClock.restart();
        updateLive();
    }
}

int FuncTestPanel::medianOf(const QVector<int>& v) {
    if (v.isEmpty()) return 0;
    QVector<int> s = v;
    std::sort(s.begin(), s.end());
    return s[s.size() / 2];
}

void FuncTestPanel::updateLive() {
    if (!m_live) return;
    QString s = QString("PWM: %1 | Lux: %2 | BT1: %3 | BT2: %4 | VBAT: %5mV")
        .arg(m_tele.value("pwm").toString())
        .arg(m_tele.value("lux_raw").toInt() / 100.0, 0, 'f', 1)
        .arg(m_tele.value("raw_k_m").toString())
        .arg(m_tele.value("raw_k_p").toString())
        .arg(m_tele.value("vbatt").toString());
    if (m_running || m_phase == PhRestoreMode) {
        qint64 timeout = phaseTimeoutMs(m_phase);
        if (timeout > 0) {
            qint64 rem = timeout - phaseElapsed();
            if (rem < 0) rem = 0;
            s += QString(" | ⏳ %1 剩余 %2s").arg(phaseLabel(m_phase)).arg(rem / 1000.0, 0, 'f', 1);
        }
    }
    m_live->setText(s);
}

qint64 FuncTestPanel::phaseElapsed() const { return m_phaseClock.elapsed() - m_pauseMs; }

qint64 FuncTestPanel::phaseTimeoutMs(Phase p) {
    switch (p) {
    case PhPowerOn:     return 30000;
    case PhLedBase:
    case PhLedFull:
    case PhLedEcho:     return kLedStageTimeoutMs;
    case PhSetAls:      return 9000;
    case PhAlsDark:
    case PhAlsBright:   return kAlsTimeoutMs;
    case PhKeyBT1:
    case PhKeyBT2:      return kKeyTimeoutMs;
    case PhKeyBT1Rel:
    case PhKeyBT2Rel:   return 5000;
    case PhDm:          return 8000;
    case PhRestoreMode: return 9000;
    default:            return 0;
    }
}

const char *FuncTestPanel::phaseLabel(Phase p) {
    switch (p) {
    case PhPowerOn:     return "等待开机";
    case PhLedBase:     return "LED 关灯基准";
    case PhLedFull:     return "LED 全亮采样";
    case PhLedEcho:     return "LED 回显判定";
    case PhSetAls:      return "切换 ALS";
    case PhAlsDark:     return "光感-暗";
    case PhAlsBright:   return "光感-亮";
    case PhKeyBT1:      return "按键 BT1";
    case PhKeyBT1Rel:   return "松开 BT1";
    case PhKeyBT2:      return "按键 BT2";
    case PhKeyBT2Rel:   return "松开 BT2";
    case PhDm:          return "DM 码";
    case PhRestoreMode: return "恢复模式";
    default:            return "";
    }
}

void FuncTestPanel::startTest(uint32_t sn) {
    if (m_running) return;
    if (sn == 0 || sn == 0xFFFFFFFF) {
        setInstruction("请先在“1. Multi-Channel Programming”页连接探针，并在顶部选择活动探针后再开始测试。");
        emit sigLog("[FUNC] 未选择活动探针, 测试未启动");
        return;
    }
    m_sn = sn;
    m_running = true;
    m_ledRestored = false;
    m_physKeysBlocked = false;
    m_pollFast = false;
    m_alsSwitched = false;
    m_ledVbatOff = 0; m_ledVbatOn = 0; m_ledIavgOn = 0; m_ledSafeBrtOn = 0;
    m_pzOff = -1.0; m_pzOn = -1.0;
    m_pauseMs = 0; m_paused = false;
    m_teleSeen = false;
    m_teleClock.restart();
    m_vbuf.clear(); m_iabuf.clear(); m_sbbuf.clear();
    m_stepState.fill(0, 6);
    for (int i = 0; i < 6; i++) {
        m_stepTable->item(i, 0)->setForeground(QColor("#333333"));
        m_stepTable->item(i, 1)->setText("待测");
        m_stepTable->item(i, 1)->setForeground(QColor("#6C757D"));
    }
    m_dmImage->hide();
    m_result->setText("测试中…");
    m_result->setStyleSheet("font-size: 26pt; font-weight: bold; color: #E6A700; border: 1px solid #CCC; border-radius: 6px; background: #FFF9E6;");
    m_btnStart->setEnabled(false);
    m_btnStop->setEnabled(true);
    if (m_pollCtrl) m_pollCtrl(m_sn, true);
    m_pollFast = true;
    setPhase(PhPowerOn);
    emit sigLog("[FUNC] 功能测试开始 (SN: " + QString::number(sn) + ")");
    m_timer->start();
}

void FuncTestPanel::stopTest() {
    if (!m_running) return;
    m_running = false;
    m_resultPendingOk = false;
    m_resultPendingReason = "测试被手动停止";
    if (m_alsSwitched) {
        setPhase(PhRestoreMode);
    } else {
        completeFinish(false, "测试被手动停止");
    }
    emit sigLog("[FUNC] 功能测试被手动停止");
}

bool FuncTestPanel::hasState(int state) const {
    if (!m_tele.contains("state")) return false;
    int st = m_tele["state"].toInt();
    return st == state;
}

void FuncTestPanel::setPhase(Phase p) {
    m_phase = p;
    m_phaseClock.restart();
    switch (p) {
    case PhPowerOn:
        setInstruction("请确保设备已开机（双键长按 1.5s 开机，红灯亮起）。等待设备进入 RUN…");
        break;
    case PhLedBase:
        setStepState(0, 1);
        m_ledVbatOff = 0; m_ledVbatOn = 0;
        m_vbuf.clear();
        m_pzOff = -1.0;
        setInstruction("LED 测试(1/3)：正在采集关灯基准电压…");
        /* OVR 模式1: CC = 2399 - ovr_pwm, ovr_pwm=0 -> LED 灭 */
        sendCmd(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, 1));
        sendCmd(Command(CmdType::WRITE_16, OFS_OVR_PWM_VAL, 0));
        break;
    case PhLedFull:
        m_vbuf.clear(); m_iabuf.clear(); m_sbbuf.clear();
        m_pzOn = -1.0;
        setInstruction("LED 测试(2/3)：正在全亮采样(压降判定开路/短路)…");
        /* ovr_pwm=2399 -> CC=0 -> LED 全亮 */
        sendCmd(Command(CmdType::WRITE_16, OFS_OVR_PWM_VAL, 2399));
        break;
    case PhLedEcho:
        setInstruction("LED 测试(3/3)：正在回读 PWM 判定…");
        /* ovr_pwm=1200 -> CC=1199 -> 约 50% 占空比, 回显应≈1199 */
        sendCmd(Command(CmdType::WRITE_16, OFS_OVR_PWM_VAL, 1200));
        break;
    case PhSetAls:
        setInstruction("正在切换到自动感光(ALS)模式…");
        /* 已处于 ALS 则直接跳过, 否则虚拟双键按住 5s 触发模式切换 */
        if (m_tele.contains("cfg_params") && ((m_tele["cfg_params"].toUInt() >> 8) & 1u) != 0) {
            setPhase(PhAlsDark);
        } else {
            sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 1));
            sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 1));
            m_alsSwitched = true;
        }
        break;
    case PhAlsDark:
        setStepState(1, 1);
        setInstruction(QString("光感测试(1/2)：请用手掌或遮光罩【完全遮住】光感窗口，等待读数降到 %1 lux 以下…").arg(kAlsDarkLux));
        break;
    case PhAlsBright:
        setStepState(2, 1);
        setInstruction(QString("光感测试(2/2)：请【移开遮挡】让光感正常见光（环境光不足时可用手电筒照射），等待读数超过 %1 lux…").arg(kAlsBrightLux));
        break;
    case PhKeyBT1:
        setStepState(3, 1);
        if (!m_physKeysBlocked) { sendCmd(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 1)); m_physKeysBlocked = true; }
        setInstruction("按键测试(1/2)：请【按下并松开】 BT1（+）键…");
        break;
    case PhKeyBT1Rel:
        setInstruction("按键测试(1/2)：请【松开】 BT1（+）键…");
        break;
    case PhKeyBT2:
        setStepState(4, 1);
        setInstruction("按键测试(2/2)：请【按下并松开】 BT2（-）键…");
        break;
    case PhKeyBT2Rel:
        setInstruction("按键测试(2/2)：请【松开】 BT2（-）键…");
        break;
    case PhDm:
        setStepState(5, 1);
        setInstruction("DM 码：正在生成设备 DM 码…");
        break;
    case PhRestoreMode:
        setInstruction("测试结束：正在恢复原工作模式…");
        sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 1));
        sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 1));
        break;
    case PhDone:
    case PhIdle:
    default:
        break;
    }
    updateLive();
}

void FuncTestPanel::setInstruction(const QString& s) {
    m_instr->setText(s);
}

void FuncTestPanel::setStepState(int idx, int state) {
    if (idx < 0 || idx >= 6) return;
    m_stepState[idx] = state;
    QTableWidgetItem *item = m_stepTable->item(idx, 1);
    switch (state) {
    case 1: item->setText("测试中…"); item->setForeground(QColor("#E6A700")); break;
    case 2: item->setText("PASS"); item->setForeground(QColor("#198754")); break;
    case 3: item->setText("FAIL"); item->setForeground(QColor("#DC3545")); break;
    default: item->setText("待测"); item->setForeground(QColor("#6C757D")); break;
    }
    if (state == 3) m_stepTable->item(idx, 0)->setForeground(QColor("#DC3545"));
    else if (state == 2) m_stepTable->item(idx, 0)->setForeground(QColor("#198754"));
}

void FuncTestPanel::sendCmd(const Command& c) {
    if (m_sendCmd) m_sendCmd(c);
}

void FuncTestPanel::tick() {
    if (m_phase == PhIdle) return;
    if (!m_running && m_phase != PhRestoreMode) return;

    /* 掉线保护: 已收到过遥测但中断 >2.5s -> 暂停倒计时等待重连 */
    bool fresh = !m_teleSeen || m_teleClock.elapsed() < kTeleStaleMs;
    if (!fresh) {
        if (m_phase == PhRestoreMode && !m_running) {
            completeFinish(m_resultPendingOk, m_resultPendingReason + "（目标掉线，未能恢复原模式）");
            return;
        }
        if (!m_paused) {
            m_paused = true;
            m_pauseClock.restart();
            emit sigLog("[FUNC] 目标掉线/遥测中断，暂停测试等待重连…");
        }
        m_live->setText(QString("⏸ 目标掉线，等待重连…（已暂停 %1s）").arg(m_pauseClock.elapsed() / 1000.0, 0, 'f', 1));
        if (m_pauseClock.elapsed() > kPauseLimitMs) {
            m_paused = false;
            finish(false, "目标掉线超过 2 分钟，测试中止。");
        }
        return;
    }
    if (m_paused) {
        m_pauseMs += m_pauseClock.elapsed();
        m_paused = false;
        emit sigLog("[FUNC] 目标已重连，继续测试…");
    }
    qint64 elapsed = phaseElapsed();

    switch (m_phase) {
    case PhPowerOn: {
        if (hasState(1) || hasState(4)) {   /* RUN or TEST */
            emit sigLog("[FUNC] 设备已开机 (state=" + m_tele["state"].toString() + ")，解锁测试模式…");
            sendCmd(Command(CmdType::ENTER_TEST));
            setPhase(PhLedBase);
        } else if (elapsed > 30000) {
            finish(false, "等待开机超时：请确认设备已开机且探针已连接。");
        }
        break;
    }
    case PhLedBase: {
        int v = m_tele.value("vbatt_raw", 0).toInt();
        if (v > 0) m_vbuf.append(v);
        if (elapsed >= 900) {
            m_ledVbatOff = medianOf(m_vbuf);
            if (m_pzProvider) {
                QVariantMap pz = m_pzProvider();
                if (pz.value("connected").toBool()) m_pzOff = pz.value("ibus_ma").toDouble();
            }
            emit sigLog(QString("[FUNC] LED 关灯基准 VBAT=%1mV").arg(m_ledVbatOff));
            setPhase(PhLedFull);
        } else if (elapsed > kLedStageTimeoutMs) {
            finish(false, "LED 关灯基准采样超时。");
        }
        break;
    }
    case PhLedFull: {
        int v = m_tele.value("vbatt_raw", 0).toInt();
        if (v > 0) m_vbuf.append(v);
        int ia = m_tele.value("i_avg", 0).toInt();
        if (ia > 0) m_iabuf.append(ia);
        int sb = m_tele.value("safe_brt", 0).toInt();
        if (sb > 0) m_sbbuf.append(sb);
        if (elapsed >= 1100) {
            m_ledVbatOn = medianOf(m_vbuf);
            m_ledIavgOn = medianOf(m_iabuf);
            m_ledSafeBrtOn = medianOf(m_sbbuf);
            if (m_pzProvider) {
                QVariantMap pz = m_pzProvider();
                if (pz.value("connected").toBool()) m_pzOn = pz.value("ibus_ma").toDouble();
            }
            int drop = qMax(0, m_ledVbatOff - m_ledVbatOn);
            emit sigLog(QString("[FUNC] LED 全亮 VBAT=%1mV (压降 %2mV, i_avg=%3uA, safe_brt=%4)")
                .arg(m_ledVbatOn).arg(drop).arg(m_ledIavgOn).arg(m_ledSafeBrtOn));
            setPhase(PhLedEcho);
        } else if (elapsed > kLedStageTimeoutMs) {
            finish(false, "LED 全亮采样超时。");
        }
        break;
    }
    case PhLedEcho: {
        if (m_tele.contains("pwm")) {
            int pwm = m_tele["pwm"].toInt();
            if (pwm >= 1200 - 30 && pwm <= 1200 + 30) {
                int drop = qMax(0, m_ledVbatOff - m_ledVbatOn);
                QString extra;
                bool ledOk = true;
                /* 短路判定: 全亮压降过大 */
                if (drop >= kLedShortDropMv) {
                    ledOk = false;
                    extra = QString("疑似 LED 短路：全亮压降 %1mV 过大（正常 < %2mV）。").arg(drop).arg(kLedShortDropMv);
                }
                /* 开路判定: 全亮几乎无压降(无电流消耗) */
                else if (drop <= kLedOpenDropMv) {
                    ledOk = false;
                    extra = QString("疑似 LED 开路：全亮压降 %1mV 过小（正常 > %2mV，直连稳压源供电时请用 PowerZ 复核）。").arg(drop).arg(kLedOpenDropMv);
                }
                /* PowerZ 交叉验证(真实电流): 开路无变化 / 短路远超额定 */
                if (ledOk && m_pzOn >= 0 && m_pzOff >= 0) {
                    double dI = m_pzOn - m_pzOff;
                    if (qAbs(dI) < kPzOpenDeltaMa) {
                        ledOk = false;
                        extra = QString("疑似 LED 开路：PowerZ 电流变化仅 %1mA。").arg(dI, 0, 'f', 2);
                    } else if (dI > kPzShortDeltaMa) {
                        ledOk = false;
                        extra = QString("疑似 LED 短路：PowerZ 电流变化 %1mA 远超额定。").arg(dI, 0, 'f', 2);
                    }
                }
                sendCmd(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, 0));
                m_ledRestored = true;
                if (ledOk) {
                    setStepState(0, 2);
                    QString pzTxt = (m_pzOn >= 0 && m_pzOff >= 0)
                        ? QString(", PowerZ ΔI=%1mA").arg(m_pzOn - m_pzOff, 0, 'f', 2) : QString();
                    emit sigLog(QString("[FUNC] LED PASS (PWM 回显=%1, 全亮压降=%2mV, i_avg=%3uA%4)")
                        .arg(pwm).arg(drop).arg(m_ledIavgOn).arg(pzTxt));
                    setPhase(PhSetAls);
                } else {
                    setStepState(0, 3);
                    finish(false, extra);
                }
                break;
            }
        }
        if (elapsed > kLedStageTimeoutMs) {
            sendCmd(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, 0));
            m_ledRestored = true;
            finish(false, QString("LED 回读失败：设置 PWM=1200 后读到 %1，请检查 LED/驱动。").arg(m_tele.value("pwm").toString()));
        }
        break;
    }
    case PhAlsDark: {
        int sensor = m_tele.value("sensor", 0).toInt();
        if (sensor == 2) {
            finish(false, "光感传感器 I2C 故障（sensor_status=2），请检查 OPT3001 焊接/供电。");
            break;
        }
        if (m_tele.contains("lux_raw") && m_tele["lux_raw"].toInt() / 100 < kAlsDarkLux) {
            setStepState(1, 2);
            emit sigLog("[FUNC] 光感(暗) PASS (lux=" + QString::number(m_tele["lux_raw"].toInt() / 100.0, 'f', 1) + ")");
            setPhase(PhAlsBright);
            break;
        }
        if (elapsed > kAlsTimeoutMs) {
            finish(false, QString("光感(暗)超时：读数未降到 %1 lux 以下，请确认遮光完全。").arg(kAlsDarkLux));
        }
        break;
    }
    case PhAlsBright: {
        int sensor = m_tele.value("sensor", 0).toInt();
        if (sensor == 2) {
            finish(false, "光感传感器 I2C 故障（sensor_status=2），请检查 OPT3001 焊接/供电。");
            break;
        }
        if (m_tele.contains("lux_raw") && m_tele["lux_raw"].toInt() / 100 > kAlsBrightLux) {
            setStepState(2, 2);
            emit sigLog("[FUNC] 光感(亮) PASS (lux=" + QString::number(m_tele["lux_raw"].toInt() / 100.0, 'f', 1) + ")");
            setPhase(PhKeyBT1);
            break;
        }
        if (elapsed > kAlsTimeoutMs) {
            finish(false, QString("光感(亮)超时：读数未超过 %1 lux，请移开遮挡或用光源照射。").arg(kAlsBrightLux));
        }
        break;
    }
    case PhKeyBT1: {
        if (m_tele.value("raw_k_m").toInt() == 1) {
            emit sigLog("[FUNC] BT1 按下检测到");
            setPhase(PhKeyBT1Rel);
            break;
        }
        if (elapsed > kKeyTimeoutMs) finish(false, "按键 BT1 超时：未检测到按下，请检查按键/焊接。");
        break;
    }
    case PhKeyBT1Rel: {
        if (m_tele.value("raw_k_m").toInt() == 0) {
            setStepState(3, 2);
            emit sigLog("[FUNC] 按键 BT1 PASS");
            setPhase(PhKeyBT2);
            break;
        }
        if (elapsed > 5000) finish(false, "按键 BT1 未松开：请确认按键已释放。");
        break;
    }
    case PhKeyBT2: {
        if (m_tele.value("raw_k_p").toInt() == 1) {
            emit sigLog("[FUNC] BT2 按下检测到");
            setPhase(PhKeyBT2Rel);
            break;
        }
        if (elapsed > kKeyTimeoutMs) finish(false, "按键 BT2 超时：未检测到按下，请检查按键/焊接。");
        break;
    }
    case PhKeyBT2Rel: {
        if (m_tele.value("raw_k_p").toInt() == 0) {
            setStepState(4, 2);
            emit sigLog("[FUNC] 按键 BT2 PASS");
            if (m_physKeysBlocked) { sendCmd(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 0)); m_physKeysBlocked = false; }
            setPhase(PhDm);
            break;
        }
        if (elapsed > 5000) finish(false, "按键 BT2 未松开：请确认按键已释放。");
        break;
    }
    case PhSetAls: {
        if (m_tele.contains("cfg_params") && ((m_tele["cfg_params"].toUInt() >> 8) & 1u) != 0) {
            sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 0));
            sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 0));
            emit sigLog("[FUNC] 已切换到自动感光(ALS)模式");
            setPhase(PhAlsDark);
            break;
        }
        if (elapsed > 9000) {
            sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 0));
            sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 0));
            finish(false, "切换到自动感光模式超时(5s双键切换未生效)。");
        }
        break;
    }
    case PhRestoreMode: {
        bool alsNow = m_tele.contains("cfg_params") && (((m_tele["cfg_params"].toUInt() >> 8) & 1u) != 0);
        if (!alsNow || elapsed > 9000) {
            sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 0));
            sendCmd(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 0));
            emit sigLog("[FUNC] 已恢复原工作模式");
            completeFinish(m_resultPendingOk, m_resultPendingReason);
            break;
        }
        break;
    }
    case PhDm: {
        QString uuid = m_getUuid ? m_getUuid(m_sn) : QString();
        if (uuid.isEmpty()) {
            finish(false, "未读取到设备 UUID，无法生成 DM 码。");
            break;
        }
        QImage img = DataMatrix::renderImage(uuid, 8, 4);
        if (img.isNull()) {
            finish(false, "DM 码生成失败（内容过长？）。");
            break;
        }
        m_dmImage->setPixmap(QPixmap::fromImage(img).scaled(230, 230, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_dmImage->show();
        setStepState(5, 2);
        emit sigLog("[FUNC] DM 码已生成: " + uuid);
        finish(true, "全部测试通过");
        break;
    }
    case PhDone:
    case PhIdle:
    default:
        break;
    }
    updateLive();
}

void FuncTestPanel::finish(bool ok, const QString& reason) {
    /* 若测试中切过 ALS 模式, 先恢复原模式再收尾; 否则直接收尾 */
    m_resultPendingOk = ok;
    m_resultPendingReason = reason;
    if (m_alsSwitched) {
        m_running = false;
        setPhase(PhRestoreMode);
    } else {
        completeFinish(ok, reason);
    }
}

void FuncTestPanel::completeFinish(bool ok, const QString& reason) {
    m_running = false;
    m_timer->stop();
    /* 清理 */
    if (!m_ledRestored) sendCmd(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, 0));
    if (m_physKeysBlocked) { sendCmd(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 0)); m_physKeysBlocked = false; }
    if (m_pollFast && m_pollCtrl) { m_pollCtrl(m_sn, false); m_pollFast = false; }
    /* 退出测试模式, 保持 RUN */
    sendCmd(Command(CmdType::SEND_SYS_CMD, 6));

    m_btnStart->setEnabled(true);
    m_btnStop->setEnabled(false);
    if (ok) {
        for (int i = 0; i < 6; i++) if (m_stepState[i] != 2) setStepState(i, 2);
        m_result->setText("PASS");
        m_result->setStyleSheet("font-size: 32pt; font-weight: bold; color: white; border: 2px solid #198754; border-radius: 8px; background: #198754;");
        setInstruction("✅ 全部测试通过：LED / 光感 / 按键 / DM 码 均正常。");
        emit sigLog("[FUNC] ***** 全部测试通过 PASS *****");
    } else {
        m_result->setText("FAIL");
        m_result->setStyleSheet("font-size: 32pt; font-weight: bold; color: white; border: 2px solid #DC3545; border-radius: 8px; background: #DC3545;");
        setInstruction("❌ " + reason);
        emit sigLog("[FUNC] 测试失败: " + reason);
    }
    updateLive();
}
