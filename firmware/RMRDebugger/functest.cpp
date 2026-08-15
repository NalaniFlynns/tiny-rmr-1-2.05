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

/* ============================================================================
 * 功能测试插件 (FuncTestPanel)
 * 步骤: 开机 -> LED(PWM回显) -> 光感暗 -> 光感亮 -> BT1 -> BT2 -> DM码 -> PASS/FAIL
 * 自动判定, 操作员仅需按提示遮挡光感 / 按按键。全部通过显示绿色 PASS。
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
/* 阈值 (与固件 OPT3001 一致: lux_raw 即 lux) */
constexpr int kAlsDarkLux   = 1000;
constexpr int kAlsBrightLux = 4000;
constexpr int kKeyTimeoutMs = 20000;
constexpr int kAlsTimeoutMs = 25000;
constexpr int kLedTimeoutMs = 4000;
}

FuncTestPanel::FuncTestPanel(QWidget *parent) : QWidget(parent) {
    QVBoxLayout *v = new QVBoxLayout(this);

    QLabel *title = new QLabel("功能测试插件 (引导式自动判定)");
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

void FuncTestPanel::updateTelemetry(uint32_t sn, const QVariantMap& data) {
    if (sn == m_sn) {
        m_tele = data;
        m_live->setText(QString("PWM: %1 | Lux: %2 | BT1: %3 | BT2: %4 | VBAT: %5mV")
            .arg(data.value("pwm").toString())
            .arg(data.value("lux_raw").toString())
            .arg(data.value("raw_k_m").toString())
            .arg(data.value("raw_k_p").toString())
            .arg(data.value("vbatt").toString()));
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
    m_timer->stop();
    /* 清理: 恢复 LED / 按键 / 轮询 */
    if (!m_ledRestored) sendCmd(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, 0));
    if (m_physKeysBlocked) { sendCmd(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 0)); m_physKeysBlocked = false; }
    if (m_pollFast && m_pollCtrl) { m_pollCtrl(m_sn, false); m_pollFast = false; }
    m_result->setText("已停止");
    m_result->setStyleSheet("font-size: 26pt; font-weight: bold; color: #6C757D; border: 1px solid #CCC; border-radius: 6px; background: #F8F9FA;");
    m_btnStart->setEnabled(true);
    m_btnStop->setEnabled(false);
    setInstruction("测试已停止。可修复后重新点击“开始测试”。");
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
    case PhLed:
        setStepState(0, 1);
        setInstruction("LED 测试：正在点亮 LED 并回读 PWM 判断是否正常…");
        sendCmd(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, 1));
        sendCmd(Command(CmdType::WRITE_16, OFS_OVR_PWM_VAL, 1200));
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
        setInstruction("DM 码：正在生成并显示设备 DM 码，请用扫码枪扫描确认…");
        break;
    case PhDone:
    case PhIdle:
    default:
        break;
    }
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
    if (!m_running) return;
    qint64 elapsed = m_phaseClock.elapsed();

    switch (m_phase) {
    case PhPowerOn: {
        if (hasState(1) || hasState(4)) {   /* RUN or TEST */
            emit sigLog("[FUNC] 设备已开机 (state=" + m_tele["state"].toString() + ")，解锁测试模式…");
            sendCmd(Command(CmdType::ENTER_TEST));
            setPhase(PhLed);
        } else if (elapsed > 30000) {
            finish(false, "等待开机超时：请确认设备已开机且探针已连接。");
        }
        break;
    }
    case PhLed: {
        if (m_tele.contains("pwm")) {
            int pwm = m_tele["pwm"].toInt();
            if (pwm >= 1200 - 30 && pwm <= 1200 + 30) {
                sendCmd(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, 0));
                m_ledRestored = true;
                setStepState(0, 2);
                emit sigLog(QString("[FUNC] LED PASS (PWM 回显=%1)").arg(pwm));
                setPhase(PhAlsDark);
                break;
            }
        }
        if (elapsed > kLedTimeoutMs) {
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
        if (m_tele.contains("lux_raw") && m_tele["lux_raw"].toInt() < kAlsDarkLux) {
            setStepState(1, 2);
            emit sigLog("[FUNC] 光感(暗) PASS (lux=" + m_tele["lux_raw"].toString() + ")");
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
        if (m_tele.contains("lux_raw") && m_tele["lux_raw"].toInt() > kAlsBrightLux) {
            setStepState(2, 2);
            emit sigLog("[FUNC] 光感(亮) PASS (lux=" + m_tele["lux_raw"].toString() + ")");
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
}

void FuncTestPanel::finish(bool ok, const QString& reason) {
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
}
