#include "MainWindow.h"
#include "datamatrix.h"
#include <QApplication>
#include <windows.h>
    int prmMax[] = {9999, 999999, 5000, 100000, 99999999, 1000, 5000, 5000, 8, 20, 20, 20};
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QDateTime>
#include <QPixmap>
#include <QFileDialog>
#include <QSqlQuery>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QFrame>
#include <QScrollArea>
#include <QFrame>
#include <QGraphicsLayout>
#include <QLegendMarker>
#include <QSplitter>
#include <vector>
#include <QTimer>
#include <QIcon>
#include <QSet>
#include <QSettings>
#include <QSerialPortInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>

#pragma pack(push, 8)
struct JLINKARM_EMU_CONNECT_INFO {
    uint32_t SerialNo;
    uint8_t  Connection;
    uint8_t  aIPAddr[16];
    uint8_t  aPad[3];
    int      TimeSize;
    int      TimeCnt;
    uint16_t Status;
    uint8_t  aMACAddr[6];
    char     aHostStr[32];
};
#pragma pack(pop)

typedef int (*JLINK_EMU_GetListFunc)(int, void*, int);
typedef void (*JLinkLogCB)(const char*);
typedef void (*JLINK_SetLogFunc)(JLinkLogCB);

static QLibrary* g_globalScannerLib = nullptr;
static MainWindow* g_mainWindowContext = nullptr;

void GlobalJLinkLogHandler(const char* s) {
    if (g_mainWindowContext && s) {
        QString logStr = QString::fromUtf8(s).trimmed();
        if (!logStr.isEmpty()) {
            QMetaObject::invokeMethod(g_mainWindowContext, "onRawJLinkLog", Qt::QueuedConnection, Q_ARG(QString, logStr));
        }
    }
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    g_mainWindowContext = this;
    plotClock.start();
    setWindowTitle("RMR Factory Programmer");
    setWindowIcon(QIcon(":/logo.ico"));
    setMinimumSize(1180, 780);
    resize(1450, 950);

    initDatabase();
    setupUI();
    /* Power-Z KM003C: 真实电压电流计, HID Basic Mode, 200ms 轮询 */
    pzWorker = new PowerZWorker(this);
    connect(pzWorker, &PowerZWorker::telemetry, this, &MainWindow::onPowerZTelemetry);
    connect(pzWorker, &PowerZWorker::stateChanged, this, [this](bool ok, const QString &detail){
        if (!lblPzStatus) return;
        lblPzStatus->setText(ok ? "Power-Z: 已连接" : detail);
        lblPzStatus->setStyleSheet(ok ? "border: none; color: #198754; font-size: 8pt; font-weight: bold;"
                                      : "border: none; color: #C00000; font-size: 8pt; font-weight: bold;");
    });
    pzWorker->start(200);
    applyTheme();
    setupIpc();

    autoScanTimer = new QTimer(this);
    connect(autoScanTimer, &QTimer::timeout, this, [this](){ scanProbes(false); });
    autoScanTimer->start(2000);

    QTimer::singleShot(500, this, [this](){ scanProbes(false); });
    /* ????????: ?? QtCharts ??? append/remove + setRange ???????? */
    chartRefreshTimer = new QTimer(this);
    connect(chartRefreshTimer, &QTimer::timeout, this, [this](){
        if (viewVBatt) viewVBatt->viewport()->update();
        if (viewLux) viewLux->viewport()->update();
        if (viewPwmBrt) viewPwmBrt->viewport()->update();
    });
    chartRefreshTimer->start(200);
    /* 窗口显示并完成首次布局后再恢复分栏比例, 避免被初始布局覆盖 */
    QTimer::singleShot(0, this, &MainWindow::restoreLayoutState);
}

MainWindow::~MainWindow() {
    g_mainWindowContext = nullptr;
    for (auto w : activeWorkers) {
        w->disconnect(this);
        w->stop();
        w->wait(1000);
        w->deleteLater();
    }
    if (pzWorker) pzWorker->stop();
    if (consoleWindow) consoleWindow->deleteLater();
    db.close();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    /* 固定字号与内边距：不再随窗口缩放，避免元素被压缩 (设计基准 1450x950) */
    applyPendingRatios();
}

void MainWindow::saveLayoutState() {
    QSettings s("RMR", "RMRDebugger");
    s.setValue("window/geometry", saveGeometry());
    /* 保存各分栏的归一化比例, 不依赖页签可见性/绝对像素 */
    auto saveSplit = [&](const char* key, QSplitter* sp){
        if (!sp) return;
        QVariantList vl;
        if (pendingRatios.contains(sp)) {
            for (double r : pendingRatios.value(sp)) vl.append(r);
        } else {
            QList<int> sz = sp->sizes();
            int total = 0;
            for (int v : sz) total += v;
            if (total <= 0) return;
            for (int v : sz) vl.append(qreal(v) / qreal(total));
        }
        s.setValue(key, vl);
    };
    saveSplit("layout/global", globalSplitter);
    saveSplit("layout/right", rightSplitter);
    saveSplit("layout/test", testSplitter);
    saveSplit("layout/cols", colsSplitter);
}

void MainWindow::applyPendingRatios() {
    for (auto it = pendingRatios.begin(); it != pendingRatios.end(); ) {
        QSplitter* sp = it.key();
        const QList<double>& ratios = it.value();
        if (sp && sp->isVisible() && sp->width() > 0) {
            int total = (sp->orientation() == Qt::Horizontal) ? sp->width() : sp->height();
            if (total > 0) {
                QList<int> sizes;
                for (double r : ratios) sizes.append(qRound(r * total));
                sp->setSizes(sizes);
                it = pendingRatios.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void MainWindow::restoreLayoutState() {
    QSettings s("RMR", "RMRDebugger");
    QByteArray geo = s.value("window/geometry").toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
    pendingRatios.clear();
    auto restoreSplit = [&](const char* key, QSplitter* sp) {
        if (!sp) return;
        QVariantList vl = s.value(key).toList();
        if (vl.size() != sp->count() || vl.isEmpty()) return;
        QList<double> ratios;
        for (auto v : vl) ratios.append(v.toDouble());
        pendingRatios[sp] = ratios;
    };
    restoreSplit("layout/global", globalSplitter);
    restoreSplit("layout/right", rightSplitter);
    restoreSplit("layout/test", testSplitter);
    restoreSplit("layout/cols", colsSplitter);
    applyPendingRatios();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveLayoutState();
    QMainWindow::closeEvent(event);
}

void MainWindow::applyTheme() {
    /* 固定字号/内边距 (设计基准 1450x950), 不随窗口缩放 */
    QString qss = QString(
                      "QWidget { font-family: 'Segoe UI', Arial; font-size: %1pt; color: #333333; } "
                      "QMainWindow { background-color: #F4F6F9; } "
                      "QDialog, QMessageBox { background-color: #FFFFFF; } "
                      "QMessageBox QLabel { color: #333333; font-size: %2pt; font-weight: bold; } "
                      "QPushButton { background-color: #0078D7; color: white; border-radius: 3px; padding: %3px 12px; font-weight: bold; border: none; } "
                      "QPushButton:hover { background-color: #005A9E; } "
                      "QPushButton#BtnGreen { background-color: #198754; } "
                      "QPushButton#BtnGreen:hover { background-color: #157347; } "
                      "QPushButton#BtnRed { background-color: #DC3545; } "
                      "QPushButton#BtnRed:hover { background-color: #C82333; } "
                      "QPushButton#BtnPurple { background-color: #6F42C1; color: white; font-size: %2pt; padding: %3px; } "
                      "QPushButton#BtnPurple:hover { background-color: #59339D; } "
                      "QGroupBox { border: 1px solid #B0C4DE; border-radius: 4px; margin-top: 10px; font-weight: bold; color: #004085; padding-top: 15px; background-color: #FFFFFF; font-size: %2pt;} "
                      "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #004085; } "
                      "QLineEdit, QSpinBox, QComboBox { border: 1px solid #CCCCCC; padding: 3px; border-radius: 3px; background: #FFFFFF; font-size: %1pt; } "
                      "QLineEdit[readOnly=\"true\"] { background: #E9ECEF; color: #495057; font-weight: bold; } "
                      "QTextEdit { border: 1px solid #B0C4DE; border-radius: 4px; background: #FFFFFF; color: #333333; font-family: Consolas; font-size: %1pt; } "
                      "QProgressBar { border: 1px solid #CCCCCC; text-align: center; color: black; border-radius: 3px; font-weight: bold; background-color: #E9ECEF; height: 18px; } "
                      "QProgressBar::chunk { background-color: #28A745; border-radius: 2px; } "
                      "QTabWidget::pane { border: 1px solid #B0C4DE; background: #FFFFFF; border-radius: 4px; } "
                      "QTabBar::tab { background: #E9ECEF; color: #495057; padding: 6px 20px; border: 1px solid #B0C4DE; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; font-weight: bold; font-size: %2pt; } "
                      "QTabBar::tab:selected { background: #FFFFFF; color: #0078D7; border-top: 3px solid #0078D7; } "
                      "QTableWidget { background-color: #FFFFFF; alternate-background-color: #F8F9FA; color: #333333; gridline-color: #E0E0E0; border: 1px solid #B0C4DE; } "
                      "QHeaderView::section { background-color: #F1F3F5; padding: 4px; border: 1px solid #DEE2E6; font-weight: bold; color: #495057; } "
                      "QSplitter::handle:vertical { background-color: #DEE2E6; border: 1px solid #CCCCCC; border-radius: 2px; margin: 2px 0px; height: 6px; } "
                      "QSplitter::handle:vertical:hover { background-color: #0078D7; } "
                      "QScrollArea { background-color: #F4F6F9; border: none; } "
                      "QScrollBar:vertical { background: #F1F3F5; width: 12px; margin: 0; } "
                      "QScrollBar::handle:vertical { background: #C5CED6; border-radius: 6px; min-height: 30px; margin: 2px; } "
                      "QScrollBar::handle:vertical:hover { background: #9FB2C8; } "
                      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } "
                      "QScrollBar:horizontal { background: #F1F3F5; height: 12px; margin: 0; } "
                      "QScrollBar::handle:horizontal { background: #C5CED6; border-radius: 6px; min-width: 30px; margin: 2px; } "
                      "QScrollBar::handle:horizontal:hover { background: #9FB2C8; } "
                      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; } "
                      ).arg(8).arg(9).arg(6);
    this->setStyleSheet(qss);
}

void MainWindow::setFwPath(const QString& path) {
    txtFwPath->setText(path);
    QString foundVer = parseHexVersion(path);
    txtHexVer->setText(foundVer.isEmpty() ? "Not Found" : foundVer);
    for (auto w : activeWorkers) w->fwPath = path;
}

void MainWindow::setupIpc() {
    /* 本地隐藏调试接口: 127.0.0.1:17345, JSON 行协议 (供自动化测试/外部读取实时数据) */
    ipcServer = new QTcpServer(this);
    if (!ipcServer->listen(QHostAddress::LocalHost, 17345)) {
        onLog(0, QString("[SYS] IPC 接口启动失败: %1").arg(ipcServer->errorString()));
        return;
    }
    onLog(0, "[SYS] IPC 接口已启动: 127.0.0.1:17345 (JSON)");
    connect(ipcServer, &QTcpServer::newConnection, this, [this](){
        while (QTcpSocket *s = ipcServer->nextPendingConnection()) {
            ipcClients.append(s);
            ipcBuffers.insert(s, QByteArray());
            connect(s, &QTcpSocket::readyRead, this, [this, s](){ handleIpcRead(s); });
            connect(s, &QTcpSocket::disconnected, this, [this, s](){
                ipcClients.removeAll(s); ipcBuffers.remove(s); s->deleteLater();
            });
            ipcSendHello(s);
        }
    });
}

void MainWindow::ipcSend(QTcpSocket *s, const QJsonObject& obj) {
    if (!s) return;
    QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    s->write(line);
    s->flush();
}

void MainWindow::ipcBroadcast(const QJsonObject& obj) {
    if (ipcClients.isEmpty()) return;
    QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    for (QTcpSocket *s : ipcClients) { s->write(line); s->flush(); }
}

void MainWindow::ipcSendHello(QTcpSocket *s) {
    QJsonObject hello;
    hello["type"] = "hello";
    hello["app"] = "RMRDebugger";
    hello["ts"] = QString::number(QDateTime::currentMSecsSinceEpoch());
    hello["fw_path"] = txtFwPath ? txtFwPath->text() : QString();
    hello["active_probe"] = QString::number(cmbActiveProbe ? cmbActiveProbe->currentData().toUInt() : 0);
    hello["poll_enabled"] = chkPoll && chkPoll->isChecked();
    hello["poll_interval_ms"] = spinPollMs ? spinPollMs->value() : 150;
    QJsonArray probes;
    for (auto it = activeWorkers.constBegin(); it != activeWorkers.constEnd(); ++it) {
        QJsonObject p;
        p["sn"] = QString::number(it.key());
        p["type"] = it.value()->probeType == ProbeType::XDS110 ? "XDS110" : "JLINK";
        p["connected"] = lastStatusCode.value(it.key(), 0) == 1;
        p["status"] = lastStatusMsg.value(it.key(), QString());
        p["uuid"] = lastUuid.value(it.key(), QString());
        p["fw"] = lastFwVer.value(it.key(), QString());
        if (lastTelemetry.contains(it.key()))
            p["telemetry"] = QJsonObject::fromVariantMap(lastTelemetry[it.key()]);
        probes.append(p);
    }
    hello["probes"] = probes;
    if (pzWorker) {
        QJsonObject pz;
        pz["connected"] = pzWorker->isConnected();
        pz["vbus_v"] = pzWorker->vbus();
        pz["ibus_a"] = pzWorker->ibus();
        pz["power_w"] = pzWorker->power();
        pz["temp_c"] = pzWorker->tempC();
        pz["avg_v"] = pzWorker->avgV();
        pz["avg_a"] = pzWorker->avgI();
        pz["avg_w"] = pzWorker->avgP();
        pz["stat_sec"] = pzWorker->statSec();
        hello["powerz"] = pz;
    }
    ipcSend(s, hello);
}

void MainWindow::handleIpcRead(QTcpSocket *s) {
    ipcBuffers[s] += s->readAll();
    while (true) {
        int nl = ipcBuffers[s].indexOf('\n');
        if (nl < 0) break;
        QByteArray line = ipcBuffers[s].left(nl).trimmed();
        ipcBuffers[s].remove(0, nl + 1);
        if (line.isEmpty()) continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QJsonObject r; r["type"]="error"; r["msg"]="bad json"; ipcSend(s, r);
            continue;
        }
        handleIpcCommand(s, doc.object());
    }
}

void MainWindow::handleIpcCommand(QTcpSocket *s, const QJsonObject& cmd) {
    QString c = cmd["cmd"].toString();
    auto reply = [&](bool ok, const QString& msg = QString()){
        QJsonObject r; r["type"]="reply"; r["cmd"]=c; r["ok"]=ok; r["msg"]=msg; ipcSend(s, r);
    };
    auto toU32 = [](const QJsonValue& v, uint32_t def) -> uint32_t {
        if (v.isString()) return v.toString().toUInt(nullptr, 0);
        if (v.isDouble()) return (uint32_t)v.toInt();
        return def;
    };

    if (c == "ping") { reply(true, "pong"); return; }
    if (c == "state") { ipcSendHello(s); return; }
    if (c == "layout") {
        auto sizesToJson = [](QSplitter* sp){
            QJsonArray arr;
            if (sp) for (int sz : sp->sizes()) arr.append(sz);
            return arr;
        };
        QJsonObject sizes;
        sizes["global"] = sizesToJson(globalSplitter);
        sizes["right"] = sizesToJson(rightSplitter);
        sizes["test"] = sizesToJson(testSplitter);
        sizes["cols"] = sizesToJson(colsSplitter);
        if (cmd.contains("set")) {
            QJsonObject set = cmd["set"].toObject();
            auto applySplit = [&](const char* name, QSplitter* sp){
                if (!sp || !set.contains(name)) return;
                QJsonArray arr = set[name].toArray();
                int totalPx = 0;
                for (auto v : arr) totalPx += v.toInt();
                if (totalPx <= 0) return;
                QList<double> ratios;
                for (auto v : arr) ratios.append(v.toInt() / (double)totalPx);
                if (ratios.size() != sp->count()) return;
                pendingRatios[sp] = ratios;
                if (sp->isVisible() && sp->width() > 0) {
                    int total = (sp->orientation() == Qt::Horizontal) ? sp->width() : sp->height();
                    QList<int> sizesList;
                    for (double r : ratios) sizesList.append(qRound(r * total));
                    sp->setSizes(sizesList);
                }
            };
            applySplit("global", globalSplitter);
            applySplit("right", rightSplitter);
            applySplit("test", testSplitter);
            applySplit("cols", colsSplitter);
            reply(true, "layout applied");
            /* 等 splitter 完成重排后再保存与回读, 避免存到旧值 */
            QTimer::singleShot(150, this, [this, s](){
                saveLayoutState();
                QJsonObject s2;
                s2["global"] = [this](){ QJsonArray a; if (globalSplitter) for (int v : globalSplitter->sizes()) a.append(v); return a; }();
                s2["right"]  = [this](){ QJsonArray a; if (rightSplitter)  for (int v : rightSplitter->sizes())  a.append(v); return a; }();
                s2["test"]   = [this](){ QJsonArray a; if (testSplitter)   for (int v : testSplitter->sizes())   a.append(v); return a; }();
                s2["cols"]   = [this](){ QJsonArray a; if (colsSplitter)   for (int v : colsSplitter->sizes())   a.append(v); return a; }();
                QJsonObject l2; l2["type"]="layout"; l2["sizes"]=s2; ipcSend(s, l2);
            });
        } else {
            QJsonObject l1; l1["type"]="layout"; l1["sizes"]=sizes; ipcSend(s, l1);
        }
        return;
    }
    if (c == "scan") { reply(true, "scanning"); scanProbes(true); return; }
    if (c == "setfw") {
        QString path = cmd["path"].toString();
        if (path.isEmpty()) { reply(false, "path required"); return; }
        setFwPath(path);
        reply(true, "fw set: " + path);
        return;
    }
    if (c == "flash") {
        if (cmd.contains("path")) setFwPath(cmd["path"].toString());
        if (txtFwPath->text().isEmpty()) { reply(false, "no fw path"); return; }
        reply(true, "flash started");
        triggerFlashAll();
        return;
    }
    if (c == "unlock") {
        reply(true, "unlock queued");
        enqueueToActive(Command(CmdType::ENTER_TEST));
        enqueueToActive(Command(CmdType::READ_CFG));
        return;
    }
    if (c == "syscmd") {
        uint32_t n = toU32(cmd["n"], 0);
        reply(true, "syscmd queued");
        enqueueToActive(Command(CmdType::SEND_SYS_CMD, n));
        return;
    }
    if (c == "write") {
        uint32_t ofs = toU32(cmd["ofs"], 0);
        uint32_t val = toU32(cmd["val"], 0);
        int size = cmd["size"].toInt(4);
        if (size == 8) enqueueToActive(Command(CmdType::WRITE_8, ofs, val));
        else if (size == 16) enqueueToActive(Command(CmdType::WRITE_16, ofs, val));
        else enqueueToActive(Command(CmdType::WRITE_32, ofs, val));
        reply(true, "write queued");
        return;
    }
    if (c == "read") {
        uint32_t ofs = toU32(cmd["ofs"], 0);
        int size = cmd["size"].toInt(4);
        QVariantMap m; m["size"] = size;
        enqueueToActive(Command(CmdType::READ_MEM_ABS, BASE_ADDR + ofs, 0, "", m));
        reply(true, "read queued");
        return;
    }
    if (c == "poll") {
        if (cmd.contains("enabled")) chkPoll->setChecked(cmd["enabled"].toVariant().toBool());
        if (cmd.contains("intervalMs")) spinPollMs->setValue(cmd["intervalMs"].toInt());
        onActiveProbeChanged();
        reply(true, "poll updated");
        return;
    }
    if (c == "speed") {
        int khz = cmd["khz"].toInt();
        int idx = cmbSpeed->findText(QString("%1 kHz").arg(khz));
        if (idx >= 0) cmbSpeed->setCurrentIndex(idx);
        else { cmbSpeed->addItem(QString("%1 kHz").arg(khz)); cmbSpeed->setCurrentIndex(cmbSpeed->count() - 1); }
        reply(true, "speed set");
        return;
    }
    if (c == "key") {
        int holdMs = cmd.contains("holdMs") ? cmd["holdMs"].toInt() : 400;
        QString k = cmd["key"].toString();
        bool block = !cmd.contains("block") || cmd["block"].toBool();
        bool plus = (k == "plus" || k == "both");
        bool minus = (k == "minus" || k == "both");
        if (cmd.contains("tap")) { plus = cmd["tap"].toInt() > 0; minus = false; }
        /* 与 UI 虚拟按键一致: 非 TEST 态先自动解锁(授权后注入任意状态生效, 模式保持) */
        const QVariantMap &kt = lastTelemetry.value(cmbActiveProbe->currentData().toUInt());
        if (kt.isEmpty() || kt["state"].toInt() != 4) {
            enqueueToActive(Command(CmdType::ENTER_TEST));
            enqueueToActive(Command(CmdType::READ_CFG));
        }
        auto release = [this, plus, minus, block]() {
            if (plus) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 0));
            if (minus) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 0));
            if (block) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 0));
        };
        if (block) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 1));
        if (plus) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 1));
        if (minus) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 1));
        if (holdMs > 0) QTimer::singleShot(holdMs, this, release);
        reply(true, "key queued");
        return;
    }
    if (c == "power") {
        QString st = cmd["state"].toString();
        enqueueToActive(Command(CmdType::SEND_SYS_CMD, st == "on" ? 6 : 7));
        reply(true, "power " + st);
        return;
    }
    if (c == "led") {
        int mode = cmd["mode"].toInt();
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, (uint32_t)mode));
        /* 兼容 value/pwm/brt 三种字段: mode1 写 PWM, mode2/3 写 Brt */
        int ledVal = -1;
        if (cmd.contains("value")) ledVal = cmd["value"].toInt();
        else if (cmd.contains("pwm")) ledVal = cmd["pwm"].toInt();
        else if (cmd.contains("brt")) ledVal = cmd["brt"].toInt();
        if (ledVal >= 0) {
            if (mode == 1) enqueueToActive(Command(CmdType::WRITE_16, OFS_OVR_PWM_VAL, (uint32_t)ledVal));
            else enqueueToActive(Command(CmdType::WRITE_16, OFS_OVR_BRT_VAL, (uint32_t)ledVal));
        }
        reply(true, "led queued");
        return;
    }
    if (c == "als") {
        if (cmd.contains("enabled")) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_ALS_EN, cmd["enabled"].toVariant().toBool() ? 1 : 0));
        if (cmd.contains("lux")) enqueueToActive(Command(CmdType::WRITE_32, OFS_OVR_ALS_LUX, (uint32_t)cmd["lux"].toInt()));
        reply(true, "als queued");
        return;
    }
    if (c == "cmd") {
        QString t = cmd["type"].toString();
        CmdType ct;
        if (t == "FLASH") ct = CmdType::FLASH;
        else if (t == "ENTER_TEST") ct = CmdType::ENTER_TEST;
        else if (t == "WRITE_8") ct = CmdType::WRITE_8;
        else if (t == "WRITE_16") ct = CmdType::WRITE_16;
        else if (t == "WRITE_32") ct = CmdType::WRITE_32;
        else if (t == "READ_CFG") ct = CmdType::READ_CFG;
        else if (t == "SEND_SYS_CMD") ct = CmdType::SEND_SYS_CMD;
        else if (t == "AUTO_CALIBRATE") ct = CmdType::AUTO_CALIBRATE;
        else if (t == "AUTO_TEST") ct = CmdType::AUTO_TEST;
        else { reply(false, "unknown type: " + t); return; }
        uint32_t a1 = toU32(cmd["arg1"], 0);
        uint32_t a2 = toU32(cmd["arg2"], 0);
        QString str = cmd["str"].toString();
        QVariantMap m = cmd["map"].toObject().toVariantMap();
        enqueueToActive(Command(ct, a1, a2, str, m));
        reply(true, "cmd queued");
        return;
    }
    if (c == "autotest") {
        enqueueToActive(Command(CmdType::AUTO_TEST));
        reply(true, "autotest queued");
        return;
    }
    if (c == "calib") {
        enqueueToActive(Command(CmdType::AUTO_CALIBRATE));
        reply(true, "calib queued");
        return;
    }
    if (c == "powerz") {
        if (cmd.contains("reset") && cmd["reset"].toBool() && pzWorker) pzWorker->resetStats();
        reply(true, "powerz stats reset");
        return;
    }
    if (c == "tab") {
        int idx = cmd["index"].toInt();
        if (tabs && idx >= 0 && idx < tabs->count()) { tabs->setCurrentIndex(idx); reply(true, "tab switched"); }
        else reply(false, "bad tab index");
        return;
    }
    reply(false, "unknown cmd: " + c);
}

void MainWindow::setupUI() {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QHBoxLayout *globalLayout = new QHBoxLayout(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout();

    QFrame *headerFrame = new QFrame();
    headerFrame->setStyleSheet("background: #FFFFFF; border-radius: 4px; border: 1px solid #CCC;");
    QHBoxLayout *hl = new QHBoxLayout(headerFrame);
    hl->setContentsMargins(5, 5, 5, 5);

    lblIndicator = new QLabel("*");
    lblIndicator->setStyleSheet("color: #DC3545; font-size: 16pt; border: none;");
    lblStatus = new QLabel("Disconnected");
    lblStatus->setStyleSheet("color: #DC3545; font-weight: bold; border: none;");

    hl->addWidget(lblIndicator); hl->addWidget(lblStatus); hl->addSpacing(15);

    QLabel *lblProbe = new QLabel("Test Mode Probe:");
    lblProbe->setStyleSheet("border: none; font-weight: bold; color: #004085;");
    hl->addWidget(lblProbe);

    cmbActiveProbe = new QComboBox();
    cmbActiveProbe->setFixedWidth(130);
    connect(cmbActiveProbe, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onActiveProbeChanged);
    hl->addWidget(cmbActiveProbe);
    hl->addSpacing(15);

    QLabel *lblSpeed = new QLabel("Speed:");
    lblSpeed->setStyleSheet("border: none; font-weight: bold;");
    hl->addWidget(lblSpeed);

    cmbSpeed = new QComboBox();
    cmbSpeed->addItems({"5 kHz", "10 kHz", "20 kHz", "30 kHz", "40 kHz", "50 kHz", "100 kHz", "500 kHz", "1000 kHz", "2000 kHz", "4000 kHz"});
    cmbSpeed->setCurrentIndex(6); // 100 kHz
    cmbSpeed->setFixedWidth(90);
    connect(cmbSpeed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onSpeedChanged);
    hl->addWidget(cmbSpeed);

    hl->addStretch();

    QLabel *lblUuidTitle = new QLabel("UUID:");
    lblUuidTitle->setStyleSheet("border: none; font-weight: bold;");
    hl->addWidget(lblUuidTitle);

    txtUuid = new QLineEdit();
    txtUuid->setReadOnly(true);
    txtUuid->setFixedWidth(220);
    txtUuid->setStyleSheet("font-family: Consolas; color: #D35400; border: 1px solid #CCC;");
    hl->addWidget(txtUuid);

    btnDxf = new QPushButton("显示DM码");
    btnDxf->setToolTip("点击显示/隐藏当前设备 UUID 的小 DM 码");
    connect(btnDxf, &QPushButton::clicked, this, &MainWindow::showDmCode);
    hl->addWidget(btnDxf);
    dmPreview = new QLabel();
    dmPreview->setFixedSize(96, 96);
    dmPreview->setStyleSheet("border: 1px solid #AAA; background: white;");
    dmPreview->hide();
    hl->addWidget(dmPreview);

    lblVer = new QLabel("FW: N/A");
    lblVer->setStyleSheet("font-weight: bold; border: none; padding-left: 5px;");
    hl->addWidget(lblVer);

    mainLayout->addWidget(headerFrame);

    tabs = new QTabWidget();
    QWidget *tabProg = new QWidget();
    QVBoxLayout *vProg = new QVBoxLayout(tabProg);

    QHBoxLayout *hFile = new QHBoxLayout();
    QPushButton *btnSel = new QPushButton("Browse Hex");
    connect(btnSel, &QPushButton::clicked, this, &MainWindow::selectFirmware);
    txtFwPath = new QLineEdit();
    txtFwPath->setReadOnly(true);
    txtFwPath->setPlaceholderText("Please select firmware to flash...");

    QLabel *lblHexVerTitle = new QLabel("Hex Ver:");
    lblHexVerTitle->setStyleSheet("font-weight: bold; color: #0078D7;");
    txtHexVer = new QLineEdit();
    txtHexVer->setFixedWidth(100);
    txtHexVer->setPlaceholderText("e.g. V1.0.1");

    hFile->addWidget(btnSel); hFile->addWidget(txtFwPath, 1);
    hFile->addWidget(lblHexVerTitle); hFile->addWidget(txtHexVer);
    vProg->addLayout(hFile);

    QHBoxLayout *hScan = new QHBoxLayout();
    QPushButton *btnScan = new QPushButton("Scan For Connected J-Links & XDS110");
    btnScan->setStyleSheet("background-color: #17A2B8; color: white;");
    connect(btnScan, &QPushButton::clicked, this, [this](){ scanProbes(true); });
    hScan->addWidget(btnScan);

    hScan->addStretch();

    btnFlashAll = new QPushButton("Flash ALL Connected");
    btnFlashAll->setObjectName("BtnGreen");
    connect(btnFlashAll, &QPushButton::clicked, this, &MainWindow::triggerFlashAll);
    hScan->addWidget(btnFlashAll);
    vProg->addLayout(hScan);

    probeTable = new QTableWidget(0, 8);
    probeTable->setHorizontalHeaderLabels({"Target Type", "Probe SN", "Status", "UUID", "FW Ver", "Auto-Flash", "Progress", "Action"});
    probeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    probeTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    probeTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    probeTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    probeTable->setAlternatingRowColors(true);
    probeTable->setSelectionMode(QAbstractItemView::NoSelection);
    vProg->addWidget(probeTable, 1);

    tabs->addTab(tabProg, "1. Multi-Channel Programming");

    QWidget *tabTest = new QWidget();
    QVBoxLayout *vTestMain = new QVBoxLayout(tabTest);
    /* 左右两列也支持鼠标拖动调整比例, 并随布局记忆保存 */
    colsSplitter = new QSplitter(Qt::Horizontal);
    colsSplitter->setHandleWidth(8);
    colsSplitter->setChildrenCollapsible(false);
    colsSplitter->setStretchFactor(0, 1);
    colsSplitter->setStretchFactor(1, 1);
    colsSplitter->setSizes({520, 400});   /* 默认初始比例, 避免首次保存时为全 0 */

    // =============== 鍒?1: 鍩虹鍛戒护鍜岃缃?===============
    QVBoxLayout *col1 = new QVBoxLayout();
    QPushButton *btnEnter = new QPushButton("1. Unlock Test Mode (Active Probe)");
    btnEnter->setObjectName("BtnGreen");
    connect(btnEnter, &QPushButton::clicked, [this](){
        enqueueToActive(Command(CmdType::ENTER_TEST));
        enqueueToActive(Command(CmdType::READ_CFG));
    });
    col1->addWidget(btnEnter);

    QMap<int, QString> fMap = {
        {0, "VOLTAGE_COMPENSATION"}, {1, "ADAPTIVE_GEAR_LIMIT"},
        {2, "ALS_MODE"}, {3, "INACTIVITY_AUTO_DIM"},
        {4, "LOWPOWER_STANDBY"}, {5, "LVP_FLASH_WARNING"},
        {6, "SWD_IN_OFF_STATE"}
    };
    QGroupBox *grpFeat = new QGroupBox("Features Toggle");
    QVBoxLayout *lFeat = new QVBoxLayout(grpFeat);
    for (auto it = fMap.begin(); it != fMap.end(); ++it) {
        QCheckBox *chk = new QCheckBox(QString("[%1] %2").arg(it.key()).arg(it.value()));
        if (it.key() == 2 || it.key() == 6) chk->setStyleSheet("color: #D35400;");
        featureCheckboxes[it.key()] = chk;
        lFeat->addWidget(chk);
    }
    col1->addWidget(grpFeat);

    QGroupBox *grpAdv = new QGroupBox("Advanced Parameters");
    QGridLayout *gridAdv = new QGridLayout(grpAdv);
    QStringList prmTexts = {"R_Base(mOhm):", "Series(mOhm):", "LED Vf(mV):", "Max I(uA):", "Max P(uW):", "ALS Min:", "LVP Crit:", "LVP Ext:", "Def Lvl:", "ALS Sqrt:", "ALS Cap Lo:", "ALS Cap Hi:", "ALS Offset:"};
    QStringList prmKeys = {"r_base", "r_series", "v_fw", "i_max", "p_batt", "als_min", "lvp_crit", "lvp_ext", "def_lvl", "als_sqrt", "als_cap_low", "als_cap_high", "als_offset"};
    int prmMax[] = {9999, 999999, 5000, 100000, 99999999, 1000, 5000, 5000, 8, 20, 20, 20, 4};
    for (int i = 0; i < prmKeys.size(); ++i) {
        QLabel *l = new QLabel(prmTexts[i]);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QSpinBox *sp = new QSpinBox();
        sp->setRange(0, prmMax[i]);
        sp->setButtonSymbols(QSpinBox::NoButtons);
        cfgVars[prmKeys[i]] = sp;
        gridAdv->addWidget(l, i/2, (i%2)*2);
        gridAdv->addWidget(sp, i/2, (i%2)*2 + 1);
    }
    col1->addWidget(grpAdv);

    QHBoxLayout *hBtn1 = new QHBoxLayout();
    QPushButton *btnRd = new QPushButton("Read");
    connect(btnRd, &QPushButton::clicked, [this](){ enqueueToActive(Command(CmdType::READ_CFG)); });
    QPushButton *btnAp = new QPushButton("Apply");
    connect(btnAp, &QPushButton::clicked, this, &MainWindow::triggerApplyConfig);
    QPushButton *btnSv = new QPushButton("Save");
    btnSv->setObjectName("BtnGreen");
    connect(btnSv, &QPushButton::clicked, [this](){ enqueueToActive(Command(CmdType::SEND_SYS_CMD, 4)); });
    hBtn1->addWidget(btnRd); hBtn1->addWidget(btnAp); hBtn1->addWidget(btnSv);
    col1->addLayout(hBtn1);

    QPushButton *btnCalib = new QPushButton("Auto-Calibrate R_Base");
    btnCalib->setStyleSheet("background-color: #17A2B8; color: white;");
    connect(btnCalib, &QPushButton::clicked, [this](){ enqueueToActive(Command(CmdType::AUTO_CALIBRATE)); });
    col1->addWidget(btnCalib);

    QHBoxLayout *hSys = new QHBoxLayout();
    QPushButton *btnFac = new QPushButton("Factory Reset");
    btnFac->setObjectName("BtnRed");
    connect(btnFac, &QPushButton::clicked, [this](){ enqueueToActive(Command(CmdType::SEND_SYS_CMD, 5)); });
    QPushButton *btnRst = new QPushButton("Soft Reset");
    connect(btnRst, &QPushButton::clicked, [this](){ enqueueToActive(Command(CmdType::SEND_SYS_CMD, 2)); });
    hSys->addWidget(btnFac); hSys->addWidget(btnRst);
    col1->addLayout(hSys);

    QGroupBox *grpDma = new QGroupBox("Direct Memory / Flash Access");
    QGridLayout *gridDma = new QGridLayout(grpDma);

    gridDma->addWidget(new QLabel("Addr(Hex):"), 0, 0);
    txtMemAddr = new QLineEdit("0x20000000");
    gridDma->addWidget(txtMemAddr, 0, 1);

    cmbMemSize = new QComboBox();
    cmbMemSize->addItems({"32-bit", "16-bit", "8-bit"});
    gridDma->addWidget(cmbMemSize, 0, 2);

    gridDma->addWidget(new QLabel("Val(Hex):"), 1, 0);
    txtMemVal = new QLineEdit("0x00000000");
    gridDma->addWidget(txtMemVal, 1, 1);

    QHBoxLayout *hDmaBtn = new QHBoxLayout();
    QPushButton *btnMemRd = new QPushButton("Read");
    connect(btnMemRd, &QPushButton::clicked, this, &MainWindow::onMemRead);
    QPushButton *btnMemWr = new QPushButton("Write");
    btnMemWr->setObjectName("BtnRed");
    connect(btnMemWr, &QPushButton::clicked, this, &MainWindow::onMemWrite);
    hDmaBtn->addWidget(btnMemRd); hDmaBtn->addWidget(btnMemWr);
    gridDma->addLayout(hDmaBtn, 1, 2);

    col1->addWidget(grpDma);
    col1->addStretch();

    // =============== 实时遥测: 固定高度长条(不缩放) ===============
    QGroupBox *grpMon = new QGroupBox("Real-time Telemetry Monitor (fixed strip)");
    QVBoxLayout *vMon = new QVBoxLayout(grpMon);
    vMon->setSpacing(2);
    vMon->setContentsMargins(8, 4, 8, 4);
    QHBoxLayout *lPoll = new QHBoxLayout();
    chkPoll = new QCheckBox("Enable Background Polling");
    chkPoll->setStyleSheet("color: #0078D7; font-weight: bold;");
    connect(chkPoll, &QCheckBox::toggled, this, &MainWindow::onActiveProbeChanged);
    lPoll->addWidget(chkPoll);
    lPoll->addStretch();
    lPoll->addWidget(new QLabel("Poll(ms):"));
    spinPollMs = new QSpinBox();
    spinPollMs->setRange(20, 5000);
    spinPollMs->setValue(150);
    spinPollMs->setSuffix(" ms");
    spinPollMs->setToolTip("遥测轮询间隔。J-Link/OpenOCD(XDS110) 持续会话可达 20ms(50Hz)。");
    connect(spinPollMs, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ for (auto w : activeWorkers) w->pollIntervalMs = v; });
    lPoll->addWidget(spinPollMs);
    vMon->addLayout(lPoll);

    QStringList monTexts = {"State", "VBATT RAW(mV)", "Est R(Ω)", "Level", "Brt Tgt", "Duty", "V_LED(mV)", "V-Limit", "I-Lim(Brt)", "P-Limit(W)", "I-Lim(LED)", "Est.P(mW)", "HW P(mW)", "Avg I(mA)", "Peak I(mA)", "HW PWM", "I2C Sensor", "I2C Err", "Lux(Filt)", "Lux(RAW)", "ALS Off", "NVM", "Save Fail", "Inactive", "NVM Seq", "NVM Sector", "NVM Slot", "Ovr Mode", "Cmd Ack", "FW Ver", "Run Flags", "Params", "Sys Clk", "Boot Refuse", "Stby Cnt"};
    QStringList monKeys = {"state", "vbatt", "dyn_r", "level", "brt", "duty", "v_led", "l_v_drop", "l_i_brt", "l_p_avg", "l_i_led", "p_led", "p_hw", "i_avg", "i_peak", "pwm", "sensor", "err_cnt", "lux", "lux_raw", "als_off", "nvm_dirty", "nvm_fail", "inactivity", "nvm_seq", "nvm_sector", "nvm_slot", "ovr_mode", "cmd_ack", "fw_ver", "flags", "cfg_params", "sys_clk", "boot_refuse", "stby_cnt"};
    const int MON_COLS = 10;
    QGridLayout *gridMon = new QGridLayout();
    gridMon->setHorizontalSpacing(6);
    gridMon->setVerticalSpacing(2);
    for (int i = 0; i < monKeys.size(); ++i) {
        QWidget *cell = new QWidget();
        cell->setFixedHeight(40);
        QVBoxLayout *vCell = new QVBoxLayout(cell);
        vCell->setContentsMargins(4, 1, 4, 1);
        vCell->setSpacing(0);
        QLabel *l = new QLabel(monTexts[i]);
        l->setStyleSheet("border: none; color: #666666; font-size: 7pt; font-weight: bold;");
        QLineEdit *le = new QLineEdit("-");
        le->setReadOnly(true);
        le->setFixedHeight(22);
        le->setStyleSheet("color: #0078D7; font-weight: bold; border: 1px solid #DDDDDD; border-radius: 2px; background: #FFFFFF; padding: 0 3px; font-size: 8pt;");
        monVars[monKeys[i]] = le;
        vCell->addWidget(l);
        vCell->addWidget(le);
        gridMon->addWidget(cell, i / MON_COLS, i % MON_COLS);
    }
    for (int c = 0; c < MON_COLS; ++c) gridMon->setColumnStretch(c, 1);
    vMon->addLayout(gridMon);
    /* Power-Z KM003C 真实电压电流计 (真实电压/电流/功耗, 6 位小数) */
    QHBoxLayout *hPz = new QHBoxLayout();
    hPz->setSpacing(6);
    lblPzStatus = new QLabel("Power-Z: 未连接");
    lblPzStatus->setStyleSheet("border: none; color: #999999; font-size: 8pt; font-weight: bold;");
    hPz->addWidget(lblPzStatus);
    auto addPzField = [&](const QString &label, QLineEdit *&out){
        QWidget *cell = new QWidget();
        cell->setFixedHeight(40);
        QVBoxLayout *vCell = new QVBoxLayout(cell);
        vCell->setContentsMargins(2, 1, 2, 1);
        vCell->setSpacing(0);
        QLabel *l = new QLabel(label);
        l->setStyleSheet("border: none; color: #666666; font-size: 7pt; font-weight: bold;");
        QLineEdit *le = new QLineEdit("-");
        le->setReadOnly(true);
        le->setFixedHeight(22);
        le->setStyleSheet("color: #00897B; font-weight: bold; border: 1px solid #DDDDDD; border-radius: 2px; background: #FFFFFF; padding: 0 3px; font-size: 8pt;");
        out = le;
        vCell->addWidget(l);
        vCell->addWidget(le);
        hPz->addWidget(cell, 1);
    };
    addPzField("Real V (V)", lePzV);
    addPzField("Real I (A)", lePzI);
    addPzField("Real P (W)", lePzP);
    addPzField("AVG V (V)", lePzVAvg);
    addPzField("AVG I (A)", lePzIAvg);
    addPzField("Temp (C)", lePzTemp);
    vMon->addLayout(hPz);

    /* Power-Z 平均统计: 最近 10s 滑动窗口的平均电压/电流/功耗(200ms 采样, 约 50 样本) */
    QHBoxLayout *hPzStat = new QHBoxLayout();
    hPzStat->setSpacing(6);
    auto addPzStatField = [&](const QString &label, QLineEdit *&out){
        QWidget *cell = new QWidget();
        cell->setFixedHeight(40);
        QVBoxLayout *vCell = new QVBoxLayout(cell);
        vCell->setContentsMargins(2, 1, 2, 1);
        vCell->setSpacing(0);
        QLabel *l = new QLabel(label);
        l->setStyleSheet("border: none; color: #666666; font-size: 7pt; font-weight: bold;");
        QLineEdit *le = new QLineEdit("-");
        le->setReadOnly(true);
        le->setFixedHeight(22);
        le->setStyleSheet("color: #8E44AD; font-weight: bold; border: 1px solid #DDDDDD; border-radius: 2px; background: #FFFFFF; padding: 0 3px; font-size: 8pt;");
        out = le;
        vCell->addWidget(l);
        vCell->addWidget(le);
        hPzStat->addWidget(cell, 1);
    };
    addPzStatField("Avg V (V)", lePzAvgV);
    addPzStatField("Avg I (A)", lePzAvgI);
    addPzStatField("Avg P (W)", lePzAvgP);
    addPzStatField("10s Win(s)", lePzStatSec);
    btnPzReset = new QPushButton("Reset Stats");
    btnPzReset->setFixedHeight(22);
    btnPzReset->setStyleSheet("font-size: 8pt;");
    connect(btnPzReset, &QPushButton::clicked, this, [this](){ if (pzWorker) pzWorker->resetStats(); });
    hPzStat->addWidget(btnPzReset);
    vMon->addLayout(hPzStat);

    // =============== 鍒?3: 鎺ョ瑕嗙洊鍙婃祴璇?===============
    QVBoxLayout *col3 = new QVBoxLayout();
    QGroupBox *grpOvr = new QGroupBox("Hardware Overrides");
    QVBoxLayout *vOvr = new QVBoxLayout(grpOvr);

    chkBlockPhysKeys = new QCheckBox("Block Physical Buttons");
    chkBlockPhysKeys->setToolTip("Block Hardware Interferences (Ignore physical buttons)");
    chkBlockPhysKeys->setStyleSheet("color: #D35400; font-weight: bold;");
    connect(chkBlockPhysKeys, &QCheckBox::toggled, this, [this](bool checked) {
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, checked ? 1 : 0));
    });
    vOvr->addWidget(chkBlockPhysKeys);

    QHBoxLayout *lKey = new QHBoxLayout();
    lKey->addWidget(new QLabel("Key Status & Control:"));
    /* 状态与操作合并显示: 操作按钮即状态指示器。
       按住=注入按下(同时拦截物理键), 松开=真正松开(解除拦截); 按下时长=实际按住时长, 按钮实时刷新秒数;
       快速点击<120ms 补足到最短有效时长(等效真实按键去抖, 防止 SWD 写队列延迟吞掉短按);
       物理键按下且未注入时按钮显示 PHY(琥珀底), 注入按住显示 PRESSED(红底) */
    const int MIN_HOLD_MS = 120;
    auto pressUi = [](QPushButton *b, const QString &txt){ b->setText(txt); b->setStyleSheet("background:#C00000; color:#FFFFFF; font-weight:bold; border:1px solid #800000;"); };
    auto releaseUi = [](QPushButton *b, const QString &txt){ b->setText(txt); b->setStyleSheet(""); };
    keyUiTimer = new QTimer(this);
    keyUiTimer->setInterval(100);
    auto startUiTimer = [this](){ if (!keyUiTimer->isActive()) keyUiTimer->start(); };
    auto stopUiTimer = [this](){ if (!keyPlusHeld && !keyMinusHeld && !keyBothHeld) keyUiTimer->stop(); };
    /* 虚拟按键与真实按键走同一套去抖/事件状态机: 授权后任意状态生效, 模式保持(ALS 就是 ALS, MAN 就是 MAN);
       + 键: MAN 加档 / ALS 偏移+, - 键: MAN 减档 / ALS 偏移-, 双键 5s 切换 ALS<->MAN;
       非 TEST 态按下时先自动解锁(ENTER_TEST), 保证注入立即生效且不改变当前模式 */
    auto ensureTestMode = [this](){
        const QVariantMap &t = lastTelemetry.value(cmbActiveProbe->currentData().toUInt());
        if (t.isEmpty() || t["state"].toInt() != 4) {
            enqueueToActive(Command(CmdType::ENTER_TEST));
            enqueueToActive(Command(CmdType::READ_CFG));
        }
    };

    btnKeyPlus = new QPushButton("[+]");
    btnKeyPlus->setToolTip(u8"按住=+键按下(拦截物理键), 松开=松开; 短按: MAN 加档 / ALS 偏移+; 保持当前模式(ALS 就是 ALS, MAN 就是 MAN), 与真实按键一致");
    auto releasePlus = [this, releaseUi, stopUiTimer](){
        keyPlusHeld = false;
        if (btnKeyPlus) releaseUi(btnKeyPlus, "[+]");
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 0));
        if (!chkBlockPhysKeys->isChecked()) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 0));
        stopUiTimer();
    };
    connect(btnKeyPlus, &QPushButton::pressed, this, [this, pressUi, startUiTimer, ensureTestMode](){
        keyPlusHeld = true;
        ensureTestMode();
        keyClockPlus.restart();
        pressUi(btnKeyPlus, "[+] PRESSED 0.0s");
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 1));
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 1));
        startUiTimer();
    });
    connect(btnKeyPlus, &QPushButton::released, this, [this, releasePlus](){
        qint64 held = keyClockPlus.elapsed();
        if (held < MIN_HOLD_MS) QTimer::singleShot(MIN_HOLD_MS - held, this, [this, releasePlus](){ if (!keyPlusHeld) releasePlus(); });
        else releasePlus();
    });

    btnKeyMinus = new QPushButton("[-]");
    btnKeyMinus->setToolTip(u8"按住=-键按下(拦截物理键), 松开=松开; 短按: MAN 减档 / ALS 偏移-; 保持当前模式(ALS 就是 ALS, MAN 就是 MAN), 与真实按键一致");
    auto releaseMinus = [this, releaseUi, stopUiTimer](){
        keyMinusHeld = false;
        if (btnKeyMinus) releaseUi(btnKeyMinus, "[-]");
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 0));
        if (!chkBlockPhysKeys->isChecked()) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 0));
        stopUiTimer();
    };
    connect(btnKeyMinus, &QPushButton::pressed, this, [this, pressUi, startUiTimer, ensureTestMode](){
        keyMinusHeld = true;
        ensureTestMode();
        keyClockMinus.restart();
        pressUi(btnKeyMinus, "[-] PRESSED 0.0s");
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 1));
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 1));
        startUiTimer();
    });
    connect(btnKeyMinus, &QPushButton::released, this, [this, releaseMinus](){
        qint64 held = keyClockMinus.elapsed();
        if (held < MIN_HOLD_MS) QTimer::singleShot(MIN_HOLD_MS - held, this, [this, releaseMinus](){ if (!keyMinusHeld) releaseMinus(); });
        else releaseMinus();
    });

    btnKeyBoth = new QPushButton("[+&&-]");
    btnKeyBoth->setToolTip(u8"双键: 按住 5s 切换 ALS<->手动(模式保持, 与真实按键一致); 按住 1.5s 松开=关机/开机(RUN); 松开才真正松开");
    auto releaseBoth = [this, releaseUi, stopUiTimer](){
        keyBothHeld = false;
        if (btnKeyBoth) releaseUi(btnKeyBoth, "[+&&-]");
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 0));
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 0));
        if (!chkBlockPhysKeys->isChecked()) enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 0));
        stopUiTimer();
    };
    connect(btnKeyBoth, &QPushButton::pressed, this, [this, pressUi, startUiTimer, ensureTestMode](){
        keyBothHeld = true;
        ensureTestMode();
        keyClockBoth.restart();
        pressUi(btnKeyBoth, "[+&&-] PRESSED 0.0s");
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 1));
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 1));
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 1));
        startUiTimer();
    });
    connect(btnKeyBoth, &QPushButton::released, this, [this, releaseBoth](){
        qint64 held = keyClockBoth.elapsed();
        if (held < MIN_HOLD_MS) QTimer::singleShot(MIN_HOLD_MS - held, this, [this, releaseBoth](){ if (!keyBothHeld) releaseBoth(); });
        else releaseBoth();
    });
    /* 实时刷新按住秒数 + 松开丢失看门狗:
       若按钮仍标记按住但鼠标左键已不在按下状态(松开事件丢失/窗口失焦), 强制真正松开, 防止
       虚拟键在固件侧持续注入导致误触发 5s 双键事件 */
    connect(keyUiTimer, &QTimer::timeout, this, [this, releasePlus, releaseMinus, releaseBoth](){
        /* 用系统物理鼠标状态而非 Qt 内部状态: 松开事件丢失(窗口失焦/外部注入)时 Qt 会一直认为左键按下,
           GetAsyncKeyState 反映真实物理按键, 保证看门狗可靠释放 */
        const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (keyPlusHeld && !leftDown) releasePlus();
        if (keyMinusHeld && !leftDown) releaseMinus();
        if (keyBothHeld && !leftDown) releaseBoth();
        auto heldText = [](const QString &label, const QElapsedTimer &clk){
            return QString("%1 PRESSED %2s").arg(label).arg(clk.elapsed() / 1000.0, 0, 'f', 1);
        };
        if (keyPlusHeld && btnKeyPlus) btnKeyPlus->setText(heldText("[+]", keyClockPlus));
        if (keyMinusHeld && btnKeyMinus) btnKeyMinus->setText(heldText("[-]", keyClockMinus));
        if (keyBothHeld && btnKeyBoth) btnKeyBoth->setText(heldText("[+&&-]", keyClockBoth));
    });
    lKey->addWidget(btnKeyPlus); lKey->addWidget(btnKeyMinus); lKey->addWidget(btnKeyBoth);
    vOvr->addLayout(lKey);
    QHBoxLayout *lPwr = new QHBoxLayout();
    QPushButton *btnPwrOn = new QPushButton("Power ON");
    btnPwrOn->setObjectName("BtnGreen");
    connect(btnPwrOn, &QPushButton::clicked, this, [this](){ enqueueToActive(Command(CmdType::SEND_SYS_CMD, 6)); });
    QPushButton *btnPwrOff = new QPushButton("Power OFF");
    btnPwrOff->setObjectName("BtnRed");
    connect(btnPwrOff, &QPushButton::clicked, this, [this](){ enqueueToActive(Command(CmdType::SEND_SYS_CMD, 7)); });
    lPwr->addWidget(btnPwrOn); lPwr->addWidget(btnPwrOff);
    vOvr->addLayout(lPwr);

    QHBoxLayout *lAls = new QHBoxLayout();
    chkOvrAls = new QCheckBox("Fake Lux:");
    connect(chkOvrAls, &QCheckBox::toggled, this, &MainWindow::updateAls);
    spinLux = new QSpinBox();
    spinLux->setRange(0, 8386500);
    spinLux->setToolTip(u8"注入环境光(lux)覆盖真实传感器, 覆盖 OPT3001 全量程 0..8386500");
    connect(spinLux, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::updateAls);
    connect(spinLux, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::updateAlsPreview);
    lAls->addWidget(chkOvrAls); lAls->addWidget(spinLux);
    lblAlsMap = new QLabel("-> --");
    lblAlsMap->setStyleSheet("color: #0066CC; font-weight: bold; border: 1px solid #B0C4DE; border-radius: 3px; background: #EFF6FF; padding: 2px 6px;");
    lblAlsMap->setToolTip(u8"ALS 亮度映射预览(参考固件 7*sqrt 曲线 + 5 档偏移 + 低/高量程钳位): 目标 Brt% 与输出 PWM");
    lAls->addWidget(lblAlsMap, 1);
    vOvr->addLayout(lAls);
    updateAlsPreview();

    QVBoxLayout *lLed = new QVBoxLayout();
    lLed->addWidget(new QLabel("LED Output Mode:"));
    cmbLedMode = new QComboBox();
    cmbLedMode->addItems({
        "0: Normal Auto",
        "1: Force PWM (0-2400)",
        "2: Force Safe Brt (0-1000)",
        "3: Force Abs Brt (0-1000, No LVP)"
    });
    connect(cmbLedMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onLedModeChanged);
    lLed->addWidget(cmbLedMode);

    spinLed = new QSpinBox();
    spinLed->setRange(0, 2400);
    spinLed->setEnabled(false);
    connect(spinLed, &QSpinBox::valueChanged, this, &MainWindow::updateLed);
    lLed->addWidget(spinLed);
    vOvr->addLayout(lLed);
    col3->addWidget(grpOvr);

    QGroupBox *grpAt = new QGroupBox("Factory Auto-Test Script");
    QVBoxLayout *vAt = new QVBoxLayout(grpAt);
    QPushButton *btnAt = new QPushButton("START AUTO-TEST");
    btnAt->setObjectName("BtnPurple");
    connect(btnAt, &QPushButton::clicked, [this](){ enqueueToActive(Command(CmdType::AUTO_TEST)); });
    vAt->addWidget(btnAt);
    lblPassFail = new QLabel("READY");
    lblPassFail->setAlignment(Qt::AlignCenter);
    lblPassFail->setStyleSheet("font-size: 20pt; font-weight: bold; background-color: #6C757D; color: white; border-radius: 4px; min-height: 45px;");
    vAt->addWidget(lblPassFail);
    col3->addWidget(grpAt);
    col3->addStretch();

    QWidget *wCol1 = new QWidget(); wCol1->setLayout(col1);
    QScrollArea *saCol1 = new QScrollArea(); saCol1->setWidget(wCol1); saCol1->setWidgetResizable(true); saCol1->setFrameShape(QFrame::NoFrame); saCol1->setMinimumWidth(340);
    QWidget *wCol3 = new QWidget(); wCol3->setLayout(col3);
    QScrollArea *saCol3 = new QScrollArea(); saCol3->setWidget(wCol3); saCol3->setWidgetResizable(true); saCol3->setFrameShape(QFrame::NoFrame); saCol3->setMinimumWidth(340);
    /* 深色模式下 QScrollArea viewport/滚动条默认用深色 Base，会导致列内容四周出现黑边；统一强制浅色 */
    const QColor colBg("#F4F6F9");
    for (QScrollArea *sa : {saCol1, saCol3}) {
        QPalette vpPal = sa->viewport()->palette();
        vpPal.setColor(QPalette::Base, colBg);
        sa->viewport()->setPalette(vpPal);
        sa->viewport()->setAutoFillBackground(true);
    }
    for (QWidget *wc : {wCol1, wCol3}) {
        wc->setAutoFillBackground(true);
        QPalette wcPal = wc->palette();
        wcPal.setColor(QPalette::Window, colBg);
        wc->setPalette(wcPal);
    }
    colsSplitter->addWidget(saCol1);
    colsSplitter->addWidget(saCol3);

    // =============== 鏇茬嚎鍥?===============
    /* ??????: ?? / ???? / PWM+BRT, Y ??????? */
    auto makePlotChart = [&](const QString &title) {
        QChart *ch = new QChart();
        ch->setTitle(title);
        ch->legend()->show();
        ch->legend()->setAlignment(Qt::AlignTop);
        ch->layout()->setContentsMargins(0, 0, 0, 0);
        return ch;
    };
    auto makePlotView = [&](QChart *ch) {
        QChartView *v = new QChartView(ch);
        v->setRenderHint(QPainter::Antialiasing);
        v->setStyleSheet("background: transparent; border: 1px solid #CCC; border-radius: 4px;");
        v->setMinimumHeight(120);
        return v;
    };

    chartVBatt = makePlotChart("V_Batt (mV)");
    seriesVBatt = new QLineSeries();
    seriesVBatt->setName("V_Batt");
    QPen penVBatt(Qt::red); penVBatt.setWidth(2);
    seriesVBatt->setPen(penVBatt);
    chartVBatt->addSeries(seriesVBatt);
    axisX1 = new QValueAxis(); axisX1->setRange(0, 100); axisX1->setLabelFormat("%d");
    axisYV = new QValueAxis(); axisYV->setRange(0, 4500); axisYV->setLabelFormat("%d");
    chartVBatt->addAxis(axisX1, Qt::AlignBottom);
    chartVBatt->addAxis(axisYV, Qt::AlignLeft);
    seriesVBatt->attachAxis(axisX1); seriesVBatt->attachAxis(axisYV);

    chartLux = makePlotChart("Ambient Lux");
    seriesLux = new QLineSeries();
    seriesLux->setName("Lux");
    QPen penLux(Qt::blue); penLux.setWidth(2);
    seriesLux->setPen(penLux);
    chartLux->addSeries(seriesLux);
    axisX2 = new QValueAxis(); axisX2->setRange(0, 100); axisX2->setLabelFormat("%d");
    axisYL = new QValueAxis(); axisYL->setRange(0, 10000); axisYL->setLabelFormat("%d");
    chartLux->addAxis(axisX2, Qt::AlignBottom);
    chartLux->addAxis(axisYL, Qt::AlignLeft);
    seriesLux->attachAxis(axisX2); seriesLux->attachAxis(axisYL);

    chartPwmBrt = makePlotChart("PWM & Brt");
    seriesPwm = new QLineSeries();
    seriesPwm->setName("PWM");
    QPen penPwm(Qt::darkCyan); penPwm.setWidth(2);
    seriesPwm->setPen(penPwm);
    seriesBrt = new QLineSeries();
    seriesBrt->setName("Brt");
    QPen penBrt(Qt::magenta); penBrt.setWidth(2);
    seriesBrt->setPen(penBrt);
    chartPwmBrt->addSeries(seriesPwm);
    chartPwmBrt->addSeries(seriesBrt);
    axisX3 = new QValueAxis(); axisX3->setRange(0, 100); axisX3->setLabelFormat("%d");
    axisYP = new QValueAxis(); axisYP->setRange(0, 2400); axisYP->setLabelFormat("%d");
    chartPwmBrt->addAxis(axisX3, Qt::AlignBottom);
    chartPwmBrt->addAxis(axisYP, Qt::AlignLeft);
    seriesPwm->attachAxis(axisX3); seriesPwm->attachAxis(axisYP);
    seriesBrt->attachAxis(axisX3); seriesBrt->attachAxis(axisYP);

    QWidget *chartPanel = new QWidget();
    QHBoxLayout *hCharts = new QHBoxLayout(chartPanel);
    hCharts->setContentsMargins(0, 0, 0, 0);
    hCharts->setSpacing(6);
    viewVBatt = makePlotView(chartVBatt); hCharts->addWidget(viewVBatt, 1);
    viewLux = makePlotView(chartLux); hCharts->addWidget(viewLux, 1);
    viewPwmBrt = makePlotView(chartPwmBrt); hCharts->addWidget(viewPwmBrt, 1);

    /* ????????????; ?????????????? testSplitter */
    QWidget *colsWidget = new QWidget();
    QVBoxLayout *vColsWrap = new QVBoxLayout(colsWidget);
    vColsWrap->setContentsMargins(0, 0, 0, 0);
    vColsWrap->addWidget(colsSplitter);
    testSplitter = new QSplitter(Qt::Vertical);
    testSplitter->setHandleWidth(8);
    testSplitter->setChildrenCollapsible(false);
    testSplitter->addWidget(chartPanel);
    testSplitter->addWidget(colsWidget);
    testSplitter->setStretchFactor(0, 0);
    testSplitter->setStretchFactor(1, 1);
    testSplitter->setSizes({190, 400});
    vTestMain->addWidget(grpMon);
    vTestMain->addWidget(testSplitter, 1);

    tabs->addTab(tabTest, "2. Test & Calibration Mode");

    QWidget *tabFunc = new QWidget();
    QVBoxLayout *vFunc = new QVBoxLayout(tabFunc);
    funcTest = new FuncTestPanel(tabFunc);
    funcTest->setCommandSender([this](const Command& c){
        uint32_t sn = cmbActiveProbe->currentData().toUInt();
        if (activeWorkers.contains(sn)) activeWorkers[sn]->enqueueCommand(c);
    });
    funcTest->setUuidGetter([this](uint32_t sn){ return probeUuids.value(sn); });
    funcTest->setActiveSnProvider([this](){ return cmbActiveProbe->currentData().toUInt(); });
    funcTest->setPollingController([this](uint32_t sn, bool fast){
        BaseWorker *w = activeWorkers.value(sn, nullptr);
        if (!w) return;
        if (fast) { w->enablePolling = true; w->pollIntervalMs = 80; }
        else {
            w->enablePolling = (sn == cmbActiveProbe->currentData().toUInt() && chkPoll && chkPoll->isChecked());
            w->pollIntervalMs = spinPollMs ? spinPollMs->value() : 150;
        }
    });
    connect(funcTest, &FuncTestPanel::sigLog, this, [this](const QString& s){ onLog(0, s); });
    vFunc->addWidget(funcTest);
    tabs->addTab(tabFunc, "3. Function Test");
    /* 切换页签时应用待定分栏比例 (页签显示后才有真实尺寸) */
    connect(tabs, &QTabWidget::currentChanged, this, [this](int){ applyPendingRatios(); });
    mainLayout->addWidget(tabs, 1);

    // =============== 缁堢鏃ュ織鎺у埗鍙?===============
    consoleContainer = new QWidget();
    consoleContainer->setMinimumHeight(80);
    vConsoleLayout = new QVBoxLayout(consoleContainer);
    vConsoleLayout->setContentsMargins(0, 0, 0, 0);
    vConsoleLayout->setSpacing(0);

    QWidget *consoleHeader = new QWidget();
    consoleHeader->setStyleSheet("background-color: #F1F3F5; border-top-left-radius: 4px; border-top-right-radius: 4px; border: 1px solid #B0C4DE; border-bottom: none;");
    QHBoxLayout *hlHeader = new QHBoxLayout(consoleHeader);
    hlHeader->setContentsMargins(10, 4, 10, 4);

    QLabel *lblConsoleTitle = new QLabel("Global Output Console");
    lblConsoleTitle->setStyleSheet("color: #004085; font-weight: bold; font-size: 10pt; border: none;");

    btnDetachConsole = new QPushButton("Detach");
    btnDetachConsole->setStyleSheet("background-color: #E9ECEF; color: #495057; border: 1px solid #B0C4DE; padding: 2px 8px; border-radius: 3px; font-weight: bold;");
    btnDetachConsole->setCursor(Qt::PointingHandCursor);
    connect(btnDetachConsole, &QPushButton::clicked, this, &MainWindow::toggleConsoleWindow);

    hlHeader->addWidget(lblConsoleTitle);
    hlHeader->addStretch();
    hlHeader->addWidget(btnDetachConsole);

    txtGlobalLog = new QTextEdit();
    txtGlobalLog->setReadOnly(true);
    txtGlobalLog->setStyleSheet("background-color: #FFFFFF; color: #333333; font-family: Consolas, 'Courier New'; font-size: 10pt; border: 1px solid #B0C4DE; border-bottom-left-radius: 4px; border-bottom-right-radius: 4px;");

    vConsoleLayout->addWidget(consoleHeader);
    vConsoleLayout->addWidget(txtGlobalLog);

    consoleWindow = new QDialog(nullptr);
    consoleWindow->setAttribute(Qt::WA_QuitOnClose, false);
    consoleWindow->setWindowTitle("RMR Global Terminal (Verbose Mode)");
    consoleWindow->resize(1000, 600);
    consoleWindow->setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    consoleWindow->setWindowIcon(QIcon(":/logo.ico"));

    consoleWindow->setStyleSheet("background-color: #1E1E1E;");
    QVBoxLayout *dlgLayout = new QVBoxLayout(consoleWindow);
    dlgLayout->setContentsMargins(0, 0, 0, 0);

    connect(consoleWindow, &QDialog::finished, this, [this](){
        txtGlobalLog->setStyleSheet("background-color: #FFFFFF; color: #333333; font-family: Consolas, 'Courier New'; font-size: 10pt; border: 1px solid #B0C4DE; border-bottom-left-radius: 4px; border-bottom-right-radius: 4px;");
        vConsoleLayout->addWidget(txtGlobalLog);
        consoleContainer->show();
    });

    QGroupBox *grpLog = new QGroupBox("Session Logs (Auto Saved)");
    QVBoxLayout *vLog = new QVBoxLayout(grpLog);
    logTable = new QTableWidget(0, 3);
    logTable->setHorizontalHeaderLabels({"No.", "Chip UUID", "Status"});

    logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    logTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    logTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    logTable->horizontalHeader()->setStretchLastSection(false);
    logTable->setAlternatingRowColors(true);
    logTable->setSelectionMode(QAbstractItemView::NoSelection);
    vLog->addWidget(logTable);

    /* 全局水平分栏: 左侧主区(头部+页签), 右侧一列(会话日志 + 内嵌日志窗口) */
    globalSplitter = new QSplitter(Qt::Horizontal);
    globalSplitter->setHandleWidth(8);
    globalSplitter->setChildrenCollapsible(false);
    QWidget *leftPane = new QWidget();
    leftPane->setLayout(mainLayout);
    globalSplitter->addWidget(leftPane);
    rightSplitter = new QSplitter(Qt::Vertical);
    rightSplitter->setHandleWidth(8);
    rightSplitter->setChildrenCollapsible(false);
    grpLog->setMinimumWidth(280);
    rightSplitter->addWidget(grpLog);
    rightSplitter->addWidget(consoleContainer);
    rightSplitter->setSizes({560, 300});
    globalSplitter->addWidget(rightSplitter);
    globalSplitter->setStretchFactor(0, 7);
    globalSplitter->setStretchFactor(1, 3);
    globalSplitter->setSizes({1000, 420});
    globalLayout->addWidget(globalSplitter, 1);

    /* 拖动分栏后自动记忆布局比例 */
    layoutSaveTimer = new QTimer(this);
    layoutSaveTimer->setSingleShot(true);
    layoutSaveTimer->setInterval(250);
    connect(layoutSaveTimer, &QTimer::timeout, this, &MainWindow::saveLayoutState);
    auto armSave = [this](){ if (layoutSaveTimer) layoutSaveTimer->start(); };
    if (globalSplitter) connect(globalSplitter, &QSplitter::splitterMoved, this, [armSave](int,int){ armSave(); });
    if (rightSplitter) connect(rightSplitter, &QSplitter::splitterMoved, this, [armSave](int,int){ armSave(); });
    if (testSplitter) connect(testSplitter, &QSplitter::splitterMoved, this, [armSave](int,int){ armSave(); });
    if (colsSplitter) connect(colsSplitter, &QSplitter::splitterMoved, this, [armSave](int,int){ armSave(); });

}

void MainWindow::toggleConsoleWindow() {
    consoleContainer->hide();
    txtGlobalLog->setStyleSheet("background-color: #1E1E1E; color: #D4D4D4; font-family: Consolas, 'Courier New'; font-size: 10pt; border: none;");
    consoleWindow->layout()->addWidget(txtGlobalLog);
    consoleWindow->show();
    txtGlobalLog->append("<span style='color:#FFCC00;'><b>[SYS] Terminal detached. Raw Probe Verbose logs are now ENABLED.</b></span>");
}

void MainWindow::scanProbes(bool isManual) {
    int newlyAdded = 0;
    QSet<uint32_t> currentScanned;

    if (!g_globalScannerLib) {
        g_globalScannerLib = new QLibrary("JLink_x64.dll");
        g_globalScannerLib->load();
    }
    if (g_globalScannerLib->isLoaded()) {
        auto setLog = (JLINK_SetLogFunc)g_globalScannerLib->resolve("JLINKARM_SetLogHandler");
        auto setWarn = (JLINK_SetLogFunc)g_globalScannerLib->resolve("JLINKARM_SetWarnOutHandler");
        auto setErr = (JLINK_SetLogFunc)g_globalScannerLib->resolve("JLINKARM_SetErrorOutHandler");
        if (setLog) setLog(GlobalJLinkLogHandler);
        if (setWarn) setWarn(GlobalJLinkLogHandler);
        if (setErr) setErr(GlobalJLinkLogHandler);

        auto getList = (JLINK_EMU_GetListFunc)g_globalScannerLib->resolve("JLINKARM_EMU_GetList");
        if (getList) {
            std::vector<uint8_t> safeBuffer(8 * 1024 * 1024, 0);
            int count = getList(1 /* USB */, safeBuffer.data(), 8);
            if (count > 0) {
                for (size_t i = 0; i < safeBuffer.size() - 4; i += 4) {
                    uint32_t sn = *(uint32_t*)(&safeBuffer[i]);
                    if (sn > 10000000 && sn < 1999999999) {
                        currentScanned.insert(sn);
                        if (!activeWorkers.contains(sn)) {
                            addProbeToUI(sn, ProbeType::JLINK);
                            newlyAdded++;
                        }
                        i += 32;
                    }
                }
            }
        }
    }

    QSet<uint32_t> seenXds;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        QString desc = info.description().toUpper();
        bool isXds = desc.contains("XDS110");
        bool isDap = desc.contains("CMSIS-DAP");
        if (!isXds && !isDap) continue;
        uint32_t sn = 0;
        if (isXds) {
            /* XDS110 同时枚举 COM4(背通道UART) 与 COM5(Aux), 同一探针只建立一个 SWD 通道 */
            if (info.vendorIdentifier() == 0x0451 && info.productIdentifier() == 0xBEF3) sn = 0x0451BEF3u;
            else if (!info.serialNumber().isEmpty()) sn = qHash(info.serialNumber());
            if (sn == 0 || seenXds.contains(sn)) continue;
            seenXds.insert(sn);
        } else {
            sn = info.serialNumber().toUInt();
            if (sn == 0) sn = qHash(info.systemLocation() + info.description());
            if (sn == 0) continue;
        }
        currentScanned.insert(sn);
        if (!activeWorkers.contains(sn)) {
            addProbeToUI(sn, ProbeType::XDS110, isXds);
            newlyAdded++;
        }
    }
    QList<uint32_t> toRemove;
    for (uint32_t activeSn : activeWorkers.keys()) {
        if (!currentScanned.contains(activeSn)) {
            toRemove.append(activeSn);
        }
    }
    for (uint32_t sn : toRemove) {
        removeProbeFromUI(sn);
    }

    if (newlyAdded > 0 || !toRemove.isEmpty()) {
        onLog(0, QString("[INFO] Topology updated: %1 added, %2 removed. Total active channels: %3")
                  .arg(newlyAdded).arg(toRemove.size()).arg(activeWorkers.size()));
    } else if (isManual) {
        if (activeWorkers.isEmpty()) onLog(0, "[WARN] No probes detected via USB. Please check connection.");
        else onLog(0, "[SYS] Scan complete. No new probes found.");
    }
}

void MainWindow::removeProbeFromUI(uint32_t sn) {
    if (!activeWorkers.contains(sn)) return;
    lastTelemetry.remove(sn); lastUuid.remove(sn); lastFwVer.remove(sn); lastStatusCode.remove(sn); lastStatusMsg.remove(sn);
    QJsonObject r2; r2["type"]="probe_removed"; r2["sn"]=QString::number(sn); ipcBroadcast(r2);

    BaseWorker *w = activeWorkers[sn];
    w->disconnect(this);
    activeWorkers.remove(sn);
    probeUuids.remove(sn);

    w->stop();
    connect(w, &QThread::finished, w, &QObject::deleteLater);

    int cbIdx = cmbActiveProbe->findData(sn);
    if (cbIdx >= 0) cmbActiveProbe->removeItem(cbIdx);

    int row = probeRowMap.value(sn, -1);
    if (row >= 0) {
        probeTable->removeRow(row);
        probeRowMap.remove(sn);
        for (auto it = probeRowMap.begin(); it != probeRowMap.end(); ++it) {
            if (it.value() > row) it.value()--;
        }
    }
    onLog(sn, "[SYS] Probe physically disconnected and securely removed from system.");
}

void MainWindow::addProbeToUI(uint32_t sn, ProbeType type, bool useXdsAdapter) {
    BaseWorker *w = nullptr;
    QString typeStr = "";

    if (type == ProbeType::JLINK) {
        w = new JLinkWorker(sn, this);
        typeStr = "J-Link";
        onLog(sn, "[SYS] Engine assigned: SEGGER J-Link DLL.");
    } else {
        /* 自动发现 OpenOCD 安装位置 (xPack / 系统目录 / PATH) */
        static QString ocdBin, ocdScripts;
        if (ocdBin.isEmpty()) {
            QStringList bins = {
                "C:/ti/openocd-xpack/xpack-openocd-0.12.0-7/bin/openocd.exe",
                "C:/ti/openocd/bin/openocd.exe",
                "C:/OpenOCD/bin/openocd.exe",
                "C:/Program Files/OpenOCD/bin/openocd.exe"
            };
            for (const QString &b : bins) { if (QFile::exists(b)) { ocdBin = b; break; } }
            QStringList scr = {
                "C:/ti/openocd-xpack/xpack-openocd-0.12.0-7/openocd/scripts",
                "C:/ti/openocd/share/openocd/scripts",
                "C:/OpenOCD/share/openocd/scripts",
                "C:/Program Files/OpenOCD/share/openocd/scripts"
            };
            for (const QString &s2 : scr) { if (QDir(s2).exists()) { ocdScripts = s2; break; } }
        }
        int ocdPort = 3334 + activeWorkers.size();  // 6666 落在 Hyper-V 排除端口段(6515-6714)，OpenOCD 无法绑定
        w = new OpenOcdWorker(sn, ocdPort, useXdsAdapter, ocdBin, ocdScripts, this);
        if (useXdsAdapter) {
            typeStr = "XDS110 (OpenOCD)";
            onLog(sn, ocdBin.isEmpty() ? "[SYS] Engine: OpenOCD (openocd.exe 需在 PATH, 否则无法连接 XDS110)."
                                       : QString("[SYS] Engine: OpenOCD + XDS110 native driver (%1).").arg(ocdBin));
        } else {
            typeStr = "DAPLink (OpenOCD)";
            onLog(sn, "[SYS] Engine: OpenOCD + CMSIS-DAP driver.");
        }
    }

    w->fwPath = txtFwPath->text();
    w->fwPath = txtFwPath->text();
    w->setSpeed(cmbSpeed->currentText().remove(" kHz").toInt());
    w->pollIntervalMs = spinPollMs ? spinPollMs->value() : 150;

    connect(w, &BaseWorker::sigStatus, this, &MainWindow::onStatus);
    connect(w, &BaseWorker::sigUuid, this, &MainWindow::onUuid);
    connect(w, &BaseWorker::sigFwVer, this, &MainWindow::onFwVer);
    connect(w, &BaseWorker::sigTelemetry, this, &MainWindow::onTelemetry);
    connect(w, &BaseWorker::sigProgress, this, &MainWindow::onProgress);
    connect(w, &BaseWorker::sigLog, this, &MainWindow::onLog);
    connect(w, &BaseWorker::sigMsg, this, &MainWindow::onMsg);
    connect(w, &BaseWorker::sigAutoTestRes, this, &MainWindow::onAutoTestRes);
    connect(w, &BaseWorker::sigConfigRead, this, &MainWindow::onConfigRead);
    connect(w, &BaseWorker::sigMemReadRes, this, &MainWindow::onMemReadRes);

    activeWorkers[sn] = w;
    cmbActiveProbe->addItem(QString::number(sn), sn);

    int row = probeTable->rowCount();
    probeTable->insertRow(row);
    probeRowMap[sn] = row;

    QTableWidgetItem *itemType = new QTableWidgetItem(typeStr);
    itemType->setTextAlignment(Qt::AlignCenter);
    itemType->setFont(QFont("Segoe UI", 9, QFont::Bold));
    itemType->setForeground(type == ProbeType::JLINK ? QColor("#0078D7") : QColor("#6F42C1"));
    probeTable->setItem(row, 0, itemType);

    QTableWidgetItem *itemSN = new QTableWidgetItem(QString::number(sn));
    itemSN->setTextAlignment(Qt::AlignCenter);
    probeTable->setItem(row, 1, itemSN);

    QTableWidgetItem *itemSt = new QTableWidgetItem("Initializing...");
    itemSt->setTextAlignment(Qt::AlignCenter);
    probeTable->setItem(row, 2, itemSt);

    QTableWidgetItem *itemUuid = new QTableWidgetItem("-");
    itemUuid->setTextAlignment(Qt::AlignCenter);
    probeTable->setItem(row, 3, itemUuid);

    QTableWidgetItem *itemFw = new QTableWidgetItem("N/A");
    itemFw->setTextAlignment(Qt::AlignCenter);
    probeTable->setItem(row, 4, itemFw);

    QCheckBox *chkAuto = new QCheckBox("Enabled");
    chkAuto->setChecked(false);
    connect(chkAuto, &QCheckBox::toggled, w, [w](bool checked){ w->autoFlashEnabled = checked; if (checked) w->resetAutoFlash(); });
    QWidget *wAuto = new QWidget(); QHBoxLayout *lAuto = new QHBoxLayout(wAuto);
    lAuto->addWidget(chkAuto); lAuto->setAlignment(Qt::AlignCenter); lAuto->setContentsMargins(0,0,0,0);
    probeTable->setCellWidget(row, 5, wAuto);

    QProgressBar *pb = new QProgressBar();
    pb->setValue(0); pb->setMinimumHeight(22);
    probeTable->setCellWidget(row, 6, pb);

    QPushButton *btnFlash = new QPushButton("Flash");
    btnFlash->setObjectName("BtnGreen");
    connect(btnFlash, &QPushButton::clicked, [this, w](){
        if (txtFwPath->text().isEmpty()) { QMessageBox::warning(this, "Error", "Select Firmware First!"); return; }
        w->enqueueCommand(Command(CmdType::FLASH, 0, 0, w->fwPath));
    });
    QWidget *wAct = new QWidget(); QHBoxLayout *lAct = new QHBoxLayout(wAct);
    lAct->addWidget(btnFlash); lAct->setAlignment(Qt::AlignCenter); lAct->setContentsMargins(5,2,5,2);
    probeTable->setCellWidget(row, 7, wAct);

    w->start();
    QJsonObject a3; a3["type"]="probe_added"; a3["sn"]=QString::number(sn); a3["kind"]=typeStr; ipcBroadcast(a3);
}

void MainWindow::initDatabase() {
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("flash_history.db");
    if (db.open()) {
        QSqlQuery query;
        query.exec("CREATE TABLE IF NOT EXISTS logs (id INTEGER PRIMARY KEY AUTOINCREMENT, uuid TEXT, status TEXT, ts DATETIME DEFAULT CURRENT_TIMESTAMP)");
    }
}

void MainWindow::saveLogToDb(const QString& uuid, bool success) {
    if(uuid.isEmpty() || uuid.length() < 10) return;
    QSqlQuery query;
    query.prepare("INSERT INTO logs (uuid, status) VALUES (?, ?)");
    query.addBindValue(uuid);
    query.addBindValue(success ? "PASS" : "FAIL");
    query.exec();
    sessionCounter++;
    addLogItem(sessionCounter, uuid, success);
}

void MainWindow::addLogItem(int id, const QString& uuid, bool success) {
    int row = logTable->rowCount();
    logTable->insertRow(row);
    logTable->setItem(row, 0, new QTableWidgetItem(QString::number(id)));
    logTable->setItem(row, 1, new QTableWidgetItem(uuid));
    auto *stItem = new QTableWidgetItem(success ? "PASS" : "FAIL");
    stItem->setForeground(success ? QColor("#198754") : QColor("#DC3545"));
    stItem->setFont(QFont("Segoe UI", 9, QFont::Bold));
    logTable->setItem(row, 2, stItem);
    logTable->scrollToBottom();
}

QString MainWindow::parseHexVersion(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return "";

    QByteArray binData;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith(":") && line.length() >= 11) {
            int byteCount = line.mid(1, 2).toInt(nullptr, 16);
            int type = line.mid(7, 2).toInt(nullptr, 16);
            if (type == 0) {
                QString hexBytes = line.mid(9, byteCount * 2);
                binData.append(QByteArray::fromHex(hexBytes.toUtf8()));
            }
        }
    }
    file.close();

    QString printable;
    for (char c : binData) {
        if (c >= 32 && c <= 126) printable += c;
        else printable += ' ';
    }

    QRegularExpression re("V\\d+\\.\\d+(?:\\.\\d+)?(?:_[a-zA-Z0-9_]+)?");
    QRegularExpressionMatch match = re.match(printable);
    if (match.hasMatch()) {
        return match.captured(0);
    }
    return "";
}

void MainWindow::selectFirmware() {
    QString path = QFileDialog::getOpenFileName(this, "Select Firmware", "", "Hex Files (*.hex)");
    if (!path.isEmpty()) {
        txtFwPath->setText(path);
        QString foundVer = parseHexVersion(path);
        if (foundVer.isEmpty()) {
            txtHexVer->setText("Not Found");
        } else {
            txtHexVer->setText(foundVer);
        }
        for (auto w : activeWorkers) w->fwPath = path;
    }
}

void MainWindow::onSpeedChanged() {
    int khz = cmbSpeed->currentText().remove(" kHz").toInt();
    for (auto w : activeWorkers) {
        w->setSpeed(khz);
    }
}

void MainWindow::triggerFlashAll() {
    if (txtFwPath->text().isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select firmware first!");
        return;
    }
    for (auto w : activeWorkers) {
        w->enqueueCommand(Command(CmdType::FLASH, 0, 0, w->fwPath));
    }
}

void MainWindow::enqueueToActive(const Command& cmd) {
    if (cmbActiveProbe->count() == 0) {
        QMessageBox::warning(this, "No Probe", "Please scan and connect a J-Link or XDS110 probe first.");
        return;
    }
    uint32_t activeSn = cmbActiveProbe->currentData().toUInt();
    if (activeWorkers.contains(activeSn)) activeWorkers[activeSn]->enqueueCommand(cmd);
}

void MainWindow::onActiveProbeChanged() {
    uint32_t activeSn = cmbActiveProbe->currentData().toUInt();
    for (auto w : activeWorkers) {
        w->enablePolling = (w->probeSN == activeSn && chkPoll->isChecked());
    }
    /* 连接/切换探针时清空上次会话残留的调试覆盖(虚拟键/亮度注入/物理键拦截),
       防止残留 ovr 位在固件侧被当作持续按住, 误触发 5s 双键模式切换 */
    enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 0));
    enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 0));
    enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, 0));
    enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_ALS_EN, 0));
    enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, 0));
    for (auto le : monVars.values()) le->setText("-");
    seriesVBatt->clear(); seriesLux->clear(); seriesPwm->clear(); seriesBrt->clear(); plotClock.restart();

    if (activeWorkers.contains(activeSn)) {
        lblVer->setText("FW: " + probeFwVers.value(activeSn, "N/A"));
        txtUuid->setText(probeUuids.value(activeSn, ""));
    QJsonObject a2; a2["type"]="active"; a2["sn"]=QString::number(activeSn); ipcBroadcast(a2);
    }
}

void MainWindow::triggerApplyConfig() {
    QVariantMap cfg;
    uint32_t feat = 0;
    for(auto it = featureCheckboxes.begin(); it != featureCheckboxes.end(); ++it) {
        if(it.value()->isChecked()) feat |= (1 << it.key());
    }
    cfg["feat"] = feat;
    for(auto key : cfgVars.keys()) cfg[key] = cfgVars[key]->value();
    enqueueToActive(Command(CmdType::SEND_SYS_CMD, 3, 0, "", cfg));
}

void MainWindow::updateAls() {
    enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_ALS_EN, (uint32_t)(chkOvrAls->isChecked() ? 1 : 0)));
    enqueueToActive(Command(CmdType::WRITE_32, OFS_OVR_ALS_LUX, (uint32_t)spinLux->value()));
}

static uint32_t alsFastIsqrt(uint32_t n) {
    uint32_t root = 0, bit = 1UL << 30;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}

void MainWindow::updateAlsPreview() {
    if (!lblAlsMap) return;
    const uint32_t PWM_REG_MAX = 2399;
    const uint32_t BRT_SCALE_MAX = 1000;
    uint32_t lux = (uint32_t)spinLux->value();
    uint32_t lux_int = lux / 100;
    uint32_t factor = cfgVars.contains("als_sqrt") && cfgVars["als_sqrt"]->value() ? (uint32_t)cfgVars["als_sqrt"]->value() : 5u;
    uint32_t capLow  = cfgVars.contains("als_cap_low")  && cfgVars["als_cap_low"]->value()  ? (uint32_t)cfgVars["als_cap_low"]->value() * 100u  : 600u;
    uint32_t capHigh = cfgVars.contains("als_cap_high") && cfgVars["als_cap_high"]->value() ? (uint32_t)cfgVars["als_cap_high"]->value() * 100u : 800u;
    uint32_t minBrt  = cfgVars.contains("als_min") ? (uint32_t)cfgVars["als_min"]->value() : 50u;
    int offIdx = cfgVars.contains("als_offset") ? cfgVars["als_offset"]->value() : 2;
    if (offIdx > 4) offIdx = 2;
    static const int AUTO_OFFSET_PCT[5] = {-50, -30, 0, 30, 50};

    /* ??? als_lux_to_brt ??: base = factor*sqrt(lux/100), 5 ???, min/cap/1000 ?? */
    int64_t base = (int64_t)factor * (int64_t)alsFastIsqrt(lux_int);
    int64_t target = base + (base * AUTO_OFFSET_PCT[offIdx]) / 100;
    if (target < (int64_t)minBrt) target = minBrt;
    int64_t cap = (lux_int <= 10000) ? (int64_t)capLow : (int64_t)capHigh;
    if (target > cap) target = cap;
    if (target > (int64_t)BRT_SCALE_MAX) target = BRT_SCALE_MAX;
    if (target < 0) target = 0;
    uint32_t pwm = (uint32_t)(PWM_REG_MAX - (uint32_t)target * PWM_REG_MAX / BRT_SCALE_MAX);
    lblAlsMap->setText(QString("-> %1% / PWM %2").arg(target / 10.0, 0, 'f', 1).arg(pwm));
}

void MainWindow::onLedModeChanged() {
    int mode = cmbLedMode->currentIndex();
    spinLed->setEnabled(mode != 0);
    spinLed->setMaximum(mode == 1 ? 2400 : 1000);
    updateLed();
}

void MainWindow::updateLed() {
    int mode = cmbLedMode->currentIndex();
    enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_LED_MODE, (uint32_t)mode));
    if (mode == 1) {
        enqueueToActive(Command(CmdType::WRITE_16, OFS_OVR_PWM_VAL, (uint32_t)spinLed->value()));
    } else if (mode == 2 || mode == 3) {
        enqueueToActive(Command(CmdType::WRITE_16, OFS_OVR_BRT_VAL, (uint32_t)spinLed->value()));
    }
}

void MainWindow::showDmCode() {
    /* 点击在按钮旁显示/隐藏一个小的 DM 码 */
    if (dmPreview && dmPreview->isVisible()) { dmPreview->hide(); return; }
    uint32_t sn = cmbActiveProbe->currentData().toUInt();
    QString uuid = probeUuids.value(sn);
    if (uuid.isEmpty()) { showModalMsg("Error", "请先连接探针并读取 UUID。", true); return; }
    QImage img = DataMatrix::renderImage(uuid, 4, 2);
    if (img.isNull()) { showModalMsg("Error", "DM 码生成失败。", true); return; }
    if (dmPreview) {
        dmPreview->setPixmap(QPixmap::fromImage(img).scaled(92, 92, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        dmPreview->setToolTip(uuid);
        dmPreview->show();
    }
}

void MainWindow::onStatus(uint32_t sn, int code, const QString& msg) {
    int row = probeRowMap.value(sn, -1);
    if (row >= 0) {
        QTableWidgetItem *item = probeTable->item(row, 2);
    lastStatusCode[sn] = code; lastStatusMsg[sn] = msg;
    QJsonObject s1; s1["type"]="status"; s1["sn"]=QString::number(sn); s1["code"]=code; s1["msg"]=msg; ipcBroadcast(s1);
        if (item) {
            item->setText(msg);
            item->setForeground(code == 1 ? QColor("#198754") : QColor("#DC3545"));
            item->setFont(QFont("Segoe UI", 9, QFont::Bold));
        }
    }

    if (sn == cmbActiveProbe->currentData().toUInt()) {
        lblStatus->setText(msg);
        if(code == 0) {
            lblStatus->setStyleSheet("color: #DC3545; font-weight: bold; border: none;");
            lblIndicator->setStyleSheet("color: #DC3545; font-size: 16pt; border: none;");
        } else if(code == 1) {
            lblStatus->setStyleSheet("color: #198754; font-weight: bold; border: none;");
            lblIndicator->setStyleSheet("color: #198754; font-size: 16pt; border: none;");
        }
    }
}

void MainWindow::onUuid(uint32_t sn, const QString& uuid) {
    probeUuids[sn] = uuid;
    lastUuid[sn] = uuid;
    QJsonObject u1; u1["type"]="uuid"; u1["sn"]=QString::number(sn); u1["uuid"]=uuid; ipcBroadcast(u1);
    int row = probeRowMap.value(sn, -1);
    if (row >= 0) {
        QTableWidgetItem *item = probeTable->item(row, 3);
        if (item) {
            item->setText(uuid.isEmpty() ? "-" : uuid);
            item->setForeground(QColor("#D35400"));
            item->setFont(QFont("Consolas", 9));
        }
    }
    if (sn == cmbActiveProbe->currentData().toUInt()) txtUuid->setText(uuid);
}

void MainWindow::onFwVer(uint32_t sn, const QString& ver) {
    int row = probeRowMap.value(sn, -1);
    probeFwVers[sn] = ver;
    lastFwVer[sn] = ver;
    QJsonObject f1; f1["type"]="fwver"; f1["sn"]=QString::number(sn); f1["ver"]=ver; ipcBroadcast(f1);
    if (row >= 0) {
        QTableWidgetItem *item = probeTable->item(row, 4);
        if (item) {
            item->setText(ver);
            item->setFont(QFont("Segoe UI", 9, QFont::Bold));
        }
    }
    if (sn == cmbActiveProbe->currentData().toUInt()) lblVer->setText("FW: " + ver);
}

void MainWindow::onProgress(uint32_t sn, int pct, const QString& text) {
    int row = probeRowMap.value(sn, -1);
    QJsonObject p1; p1["type"]="progress"; p1["sn"]=QString::number(sn); p1["pct"]=pct; p1["text"]=text; ipcBroadcast(p1);
    if (row >= 0) {
        QProgressBar* pb = (QProgressBar*)probeTable->cellWidget(row, 6);
        if (pb) {
            pb->setValue(pct);
            pb->setFormat(QString("%1 (%2%)").arg(text).arg(pct));
        }
    }
    if(pct == 100 && text == "Flash Complete") saveLogToDb(probeUuids.value(sn), true);
}

void MainWindow::onRawJLinkLog(const QString& log) {
    if (consoleWindow && consoleWindow->isVisible()) {
        txtGlobalLog->append(QString("<span style='color:#569CD6;'>[RAW] %1</span>").arg(log.toHtmlEscaped()));
        txtGlobalLog->moveCursor(QTextCursor::End);
    }
}

void MainWindow::onLog(uint32_t sn, const QString& text) {
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    bool isDark = consoleWindow && consoleWindow->isVisible();

    for(QString line : lines) {
        line = line.trimmed();
        if(line.isEmpty()) continue;

        QString color = isDark ? "#D4D4D4" : "#333333";
        if (line.contains("ERROR", Qt::CaseInsensitive) || line.contains("Fail", Qt::CaseInsensitive) || line.contains("**", Qt::CaseInsensitive)) {
            color = isDark ? "#F44336" : "#DC3545";
        } else if (line.contains("WARN", Qt::CaseInsensitive)) {
            color = isDark ? "#FFCC00" : "#D35400";
        } else if (line.contains("INFO", Qt::CaseInsensitive) || line.contains("SUCCESS", Qt::CaseInsensitive) || line.contains("[DMA]", Qt::CaseInsensitive)) {
            color = isDark ? "#4CD964" : "#198754";
        } else if (line.contains("Downloaded", Qt::CaseInsensitive) || line.contains("verified", Qt::CaseInsensitive) || line.contains("wrote", Qt::CaseInsensitive)) {
            color = isDark ? "#5AC8FA" : "#0078D7";
        }

        QString prefix = (sn == 0) ? "[SYS]" : QString("[SN:%1]").arg(sn);
        txtGlobalLog->append(QString("<span style='color:%1;'><b>%2</b> %3</span>").arg(color).arg(prefix).arg(line.toHtmlEscaped()));
    }
    QJsonObject l1; l1["type"]="log"; l1["sn"]=QString::number(sn); l1["text"]=text; ipcBroadcast(l1);
    txtGlobalLog->moveCursor(QTextCursor::End);
}

void MainWindow::onMsg(uint32_t sn, const QString& title, const QString& text) {
    QJsonObject m1; m1["type"]="msg"; m1["sn"]=QString::number(sn); m1["title"]=title; m1["text"]=text; ipcBroadcast(m1);
    /* 统一走单一模态框: 重复消息只弹一个, 关闭后短时间内不再重复弹出 */
    showModalMsg(title, QString("[SN: %1] ").arg(sn) + text, title == "Error");
}

void MainWindow::showModalMsg(const QString& title, const QString& text, bool critical) {
    /* 队列形式弹窗: 同内容窗口只允许在当前显示 + 等待队列中存在一个, 多余的直接忽略;
       不同内容按 FIFO 排队, 关闭当前窗口后弹出下一个。 */
    QString key = title + QChar(1) + text;
    if (m_msgKeys.contains(key)) return;          /* 已显示或已在队列 -> 忽略 */
    m_msgKeys.insert(key);
    m_msgQueue.enqueue({title, text, critical, key});
    if (!modalMsgBox) showNextModalMsg();
}

void MainWindow::showNextModalMsg() {
    if (m_msgQueue.isEmpty() || modalMsgBox) return;
    MainWindow::ModalMsg item = m_msgQueue.dequeue();
    modalMsgBox = new QMessageBox(this);
    modalMsgBox->setWindowTitle(item.title);
    modalMsgBox->setText(item.text);
    modalMsgBox->setIcon(item.critical ? QMessageBox::Critical : QMessageBox::Information);
    modalMsgBox->setModal(true);
    connect(modalMsgBox, &QMessageBox::finished, this, [this](int){
        m_msgKeys.remove(modalMsgBoxKey);
        modalMsgBox->deleteLater();
        modalMsgBox = nullptr;
        modalMsgBoxKey.clear();
        showNextModalMsg();
    });
    modalMsgBoxKey = item.key;
    modalMsgBox->show();
}

void MainWindow::onAutoTestRes(uint32_t sn, bool success, const QString& msg) {
    QJsonObject a1; a1["type"]="autotest"; a1["sn"]=QString::number(sn); a1["ok"]=success; a1["msg"]=msg; ipcBroadcast(a1);
    if (sn != cmbActiveProbe->currentData().toUInt()) return;
    lblPassFail->setText(success ? "PASS" : "FAIL");
    lblPassFail->setStyleSheet(success ? "background-color: #198754; color: white; font-size: 20pt; font-weight: bold; border-radius: 4px;"
                                       : "background-color: #DC3545; color: white; font-size: 20pt; font-weight: bold; border-radius: 4px;");
    if(!success) showModalMsg("Auto Test Failed", msg, true);
}

void MainWindow::onConfigRead(uint32_t sn, const QVariantMap& cfg) {
    if (sn != cmbActiveProbe->currentData().toUInt()) return;
    QJsonObject c1; c1["type"]="config"; c1["sn"]=QString::number(sn); c1["fields"]=QJsonObject::fromVariantMap(cfg); ipcBroadcast(c1);
    uint32_t feat = cfg["feat"].toUInt();
    for(auto it = featureCheckboxes.begin(); it != featureCheckboxes.end(); ++it) {
        it.value()->setChecked(feat & (1 << it.key()));
    }
    for(auto key : cfgVars.keys()) {
        if(cfg.contains(key)) cfgVars[key]->setValue(cfg[key].toUInt());
    }
    updateAlsPreview();
    onLog(sn, "[INFO] UI configuration fully synchronized with device.");
}

void MainWindow::onPowerZTelemetry(double vbus, double ibus, double vbusAvg, double ibusAvg, double tempC) {
    double power = vbus * ibus;
    if (lePzV) lePzV->setText(QString::number(vbus, 'f', 6));
    if (lePzI) lePzI->setText(QString::number(ibus, 'f', 6));
    if (lePzP) lePzP->setText(QString::number(power, 'f', 6));
    if (lePzVAvg) lePzVAvg->setText(QString::number(vbusAvg, 'f', 6));
    if (lePzIAvg) lePzIAvg->setText(QString::number(ibusAvg, 'f', 6));
    if (lePzTemp) lePzTemp->setText(QString::number(tempC, 'f', 2));
    if (pzWorker) {
        if (lePzAvgV) lePzAvgV->setText(QString::number(pzWorker->avgV(), 'f', 6));
        if (lePzAvgI) lePzAvgI->setText(QString::number(pzWorker->avgI(), 'f', 6));
        if (lePzAvgP) lePzAvgP->setText(QString::number(pzWorker->avgP(), 'f', 6));
        if (lePzStatSec) lePzStatSec->setText(QString::number(pzWorker->statSec(), 'f', 1));
    }
    if (lblPzStatus && lblPzStatus->text() != "Power-Z: 已连接") {
        lblPzStatus->setText("Power-Z: 已连接");
        lblPzStatus->setStyleSheet("border: none; color: #198754; font-size: 8pt; font-weight: bold;");
    }
    QJsonObject pz;
    pz["type"] = "powerz";
    pz["vbus_v"] = vbus;
    pz["ibus_a"] = ibus;
    pz["vbus_avg_v"] = vbusAvg;
    pz["ibus_avg_a"] = ibusAvg;
    pz["power_w"] = power;
    pz["temp_c"] = tempC;
    if (pzWorker) {
        pz["avg_v"] = pzWorker->avgV();
        pz["avg_a"] = pzWorker->avgI();
        pz["avg_w"] = pzWorker->avgP();
        pz["stat_sec"] = pzWorker->statSec();
    }
    ipcBroadcast(pz);
}
void MainWindow::onTelemetry(uint32_t sn, const QVariantMap& data) {
    if (funcTest) funcTest->updateTelemetry(sn, data);
    if (sn != cmbActiveProbe->currentData().toUInt()) return;
    lastTelemetry[sn] = data;
    QJsonObject t1; t1["type"]="telemetry"; t1["sn"]=QString::number(sn); t1["fields"]=QJsonObject::fromVariantMap(data); ipcBroadcast(t1);

    QStringList states = {"OFF", "RUN", "LVP_CRIT", "FLASH", "TEST", "ALS_ERR"};
    QStringList sns = {"OK", "Sleep/Dis", "I2C Err"};
    int st = data["state"].toInt();
    int sen = data["sensor"].toInt();

    QString stText = (st >= 0 && st < states.size()) ? states[st] : "Unk";
    if ((st == 1 || st == 4) && data.contains("cfg_params"))   /* RUN/TEST: 显示 ALS 或手动 */
        stText += ((data["cfg_params"].toUInt() >> 8) & 1u) ? " ALS" : " MAN";
    monVars["state"]->setText(stText);
    /* 电压显示原始 ADC 值(不滤波/不平滑); 旧固件无 vbatt_raw 时回退 vbatt */
    monVars["vbatt"]->setText(data.contains("vbatt_raw") ? data["vbatt_raw"].toString() : data["vbatt"].toString());
    monVars["dyn_r"]->setText(QString::number(data["dyn_r"].toInt() / 1000.0, 'f', 1));
    monVars["level"]->setText("Gear " + data["level"].toString());

    uint32_t lv = data["l_v_drop"].toUInt(); monVars["l_v_drop"]->setText(lv <= 1000 ? QString::number(lv) : "-");
    uint32_t li = data["l_i_brt"].toUInt(); monVars["l_i_brt"]->setText(li <= 1000 ? QString::number(li) : "-");
    uint32_t lp = data["l_p_avg"].toUInt(); monVars["l_p_avg"]->setText(lp <= 1000 ? QString::number(lp) : "-");
    uint32_t lled = data["l_i_led"].toUInt(); monVars["l_i_led"]->setText(lled <= 1000 ? QString::number(lled) : "-");

    monVars["v_led"]->setText(data["v_led"].toString());
    monVars["brt"]->setText(data["brt"].toString() + " (Max:" + data["safe_brt"].toString() + ")");
    monVars["pwm"]->setText(data["pwm"].toString() + " / 2399");

    monVars["p_led"]->setText(QString::number(data["p_led"].toInt() / 1000.0, 'f', 1));
    /* hardware power: mW with 3 decimals, derived from current PWM;
       power reserve % = current duty (pwm 1199/2399 -> 50%) */
    double pwrMw = data["p_hw"].toInt() / 1000.0;
    uint32_t pwmRaw = data["pwm"].toUInt();
    double pwrPct = (pwmRaw >= 2399) ? 0.0 : (2399 - pwmRaw) / 2399.0 * 100.0;
    monVars["p_hw"]->setText(QString::number(pwrMw, 'f', 3) + " mW (" + QString::number(pwrPct, 'f', 1) + "%)");
    monVars["i_avg"]->setText(QString::number(data["i_avg"].toInt() / 1000.0, 'f', 1));
    monVars["i_peak"]->setText(QString::number(data["i_peak"].toInt() / 1000.0, 'f', 1));

    /* 平均电流说明: I_avg = I_peak x 实际PWM占空比; 占空比 = (2399-pwm)/2399 */
    uint32_t pwm = data["pwm"].toUInt();
    double duty = (pwm >= 2399) ? 0.0 : (2399 - pwm) / 2399.0;
    monVars["duty"]->setText(QString("%1%").arg(duty * 100.0, 0, 'f', 1));

    monVars["sensor"]->setText(sen < 3 ? sns[sen] : "Unk");
    monVars["err_cnt"]->setText(data["err_cnt"].toString());
    monVars["lux"]->setText(data["lux"].toString());
    monVars["lux_raw"]->setText(data["lux_raw"].toString());
    monVars["als_off"]->setText(QString::number((data["cfg_params"].toUInt() >> 16) & 0xFF));

    /* 键状态合并显示到操作按钮: 注入按住=红底 PRESSED+秒数, 注入未按住=OVR, 物理按下=琥珀底 PHY, 均无=默认
       (raw=1 表示物理按下, 由固件 mailbox 实时同步) */
    auto keyBtnUi = [this](QPushButton *b, const QString &label, int ovr, int raw, const QElapsedTimer &clk, bool held){
        if (!b) return;
        if (held) {
            b->setText(QString("%1 PRESSED %2s").arg(label).arg(clk.elapsed() / 1000.0, 0, 'f', 1));
            b->setStyleSheet("background:#C00000; color:#FFFFFF; font-weight:bold; border:1px solid #800000;");
        } else if (ovr) {
            b->setText(label + " OVR");
            b->setStyleSheet("background:#C00000; color:#FFFFFF; font-weight:bold; border:1px solid #800000;");
        } else if (raw) {
            b->setText(label + " PHY");
            b->setStyleSheet("background:#E67E22; color:#FFFFFF; font-weight:bold; border:1px solid #B85C0E;");
        } else {
            b->setText(label);
            b->setStyleSheet("");
        }
    };
    keyBtnUi(btnKeyMinus, "[-]", data["ovr_k_m"].toInt(), data["raw_k_m"].toInt(), keyClockMinus, keyMinusHeld);
    keyBtnUi(btnKeyPlus, "[+]", data["ovr_k_p"].toInt(), data["raw_k_p"].toInt(), keyClockPlus, keyPlusHeld);
    keyBtnUi(btnKeyBoth, "[+&&-]", data["ovr_k_m"].toInt() | data["ovr_k_p"].toInt(),
             data["raw_k_m"].toInt() & data["raw_k_p"].toInt(), keyClockBoth, keyBothHeld);

    monVars["nvm_dirty"]->setText(data["f_dirty"].toInt() ? "DIRTY" : "Clean");
    monVars["nvm_fail"]->setText(data["nvm_fail"].toString());
    monVars["inactivity"]->setText(QString::number(data["inactivity"].toInt()) + "s");
    monVars["nvm_seq"]->setText(data["nvm_seq"].toString());
    monVars["nvm_sector"]->setText(QString("0x%1").arg(data["nvm_sector"].toUInt(), 8, 16, QChar('0')).toUpper());
    monVars["nvm_slot"]->setText(data["nvm_slot"].toString());
    monVars["ovr_mode"]->setText(data["ovr_led_mode"].toString());
    monVars["cfg_params"]->setText(QString("0x%1").arg(data["cfg_params"].toUInt(), 8, 16, QChar('0')).toUpper());
    monVars["cmd_ack"]->setText(data["cmd_ack"].toString());
    monVars["fw_ver"]->setText(data["fw_ver"].toString());

    /* V4.3.4+ mailbox 尾部扩展: SYSOSCCFG 时钟与开机拒绝原因(旧固件无此字段则显示 "-") */
    uint32_t clkKhz = data.contains("sys_clk") ? data["sys_clk"].toUInt() : 0;
    monVars["sys_clk"]->setText((clkKhz == 24000 || clkKhz == 32000 || clkKhz == 4000) ? QString("%1MHz").arg(clkKhz / 1000) : "-");
    uint32_t stby = data.contains("stby_cnt") ? data["stby_cnt"].toUInt() : 0;
    monVars["stby_cnt"]->setText(stby ? QString::number(stby) + "x" : "-");
    uint32_t refuse = data.contains("boot_refuse") ? data["boot_refuse"].toUInt() : 0xFFFFFFFF;
    static const char *refuseNames[] = {"OK", "Voltage", "OffIntent", "NoAutoFlag"};
    monVars["boot_refuse"]->setText(refuse <= 3 ? refuseNames[refuse] : "-");

    QString flags = "";
    if (data["f_dim"].toInt()) flags += "[DIMMED] ";
    if (data["f_ovr"].toInt()) flags += "[OVR_LMT] ";
    if (data["f_dbg"].toInt()) flags += "[SWD_ON] ";
    if (data["f_dirty"].toInt()) flags += "[NVM_DIRTY] ";
    monVars["flags"]->setText(flags.isEmpty() ? "None" : flags.trimmed());

    /* fixed 10s time window: X = monotonic clock seconds, show only last 10s */
    const qreal WINDOW_S = 10.0;
    const qreal KEEP_S = 15.0;
    qreal xNow = plotClock.elapsed() / 1000.0;
    /* append + ?? replace ???????(??????, ???? remove(0) ?? QtCharts ????) */
    auto pushPoint = [&](QLineSeries *s, qreal y){
        s->append(xNow, y);
        const qreal cut = xNow - KEEP_S;
        if (s->count() && s->at(0).x() < cut) {
            QList<QPointF> pts;
            pts.reserve(s->count());
            for (int i = 0; i < s->count(); ++i) {
                const QPointF &p = s->at(i);
                if (p.x() >= cut) pts.append(p);
            }
            s->replace(pts);
        }
    };
    pushPoint(seriesVBatt, data.contains("vbatt_raw") ? data["vbatt_raw"].toUInt() : data["vbatt"].toUInt());
    pushPoint(seriesLux, data["lux"].toUInt());
    pushPoint(seriesPwm, data["pwm"].toUInt());
    pushPoint(seriesBrt, data["brt"].toUInt());
    qreal xLo = qMax<qreal>(0, xNow - WINDOW_S);
    axisX1->setRange(xLo, xNow);
    axisX2->setRange(xLo, xNow);
    axisX3->setRange(xLo, xNow);
    /* Y axis auto-scale by current window min/max + margin; ?????? raw ??????? */
    autoScaleAxis(axisYV, seriesVBatt, nullptr, 250.0);
    autoScaleAxis(axisYL, seriesLux, nullptr, 2000.0);
    autoScaleAxis(axisYP, seriesPwm, seriesBrt, 200.0);
}
void MainWindow::autoScaleAxis(QValueAxis *axis, QLineSeries *s1, QLineSeries *s2, qreal minSpan) {
    if (!axis || !s1 || s1->count() == 0) return;
    qreal mn = s1->at(0).y(), mx = mn;
    auto scan = [&](QLineSeries *s) {
        if (!s) return;
        for (int i = 0; i < s->count(); ++i) {
            qreal y = s->at(i).y();
            if (y < mn) mn = y;
            if (y > mx) mx = y;
        }
    };
    scan(s1); scan(s2);
    if (mx - mn < 1.0) { mn -= 1.0; mx += 1.0; }
    /* ????: ???????????? minSpan, ???????????? */
    if (minSpan > 0.0 && (mx - mn) < minSpan) {
        qreal mid = (mx + mn) / 2.0;
        qreal lo2 = mid - minSpan / 2.0, hi2 = mid + minSpan / 2.0;
        if (lo2 < 0.0) { lo2 = 0.0; hi2 = minSpan; }
        if (hi2 < mx) { hi2 = mx; lo2 = hi2 - minSpan; if (lo2 < 0.0) lo2 = 0.0; }
        mn = lo2; mx = hi2;
    }
    qreal pad = (mx - mn) * 0.08 + 0.5;
    axis->setRange(qMax<qreal>(0.0, mn - pad), mx + pad);
}

void MainWindow::onMemRead() {
    bool ok;
    uint32_t addr = txtMemAddr->text().toUInt(&ok, 16);
    if (!ok) { QMessageBox::warning(this, "Error", "Invalid Address Hex Format!"); return; }

    int sizeIdx = cmbMemSize->currentIndex();
    int size = (sizeIdx == 0) ? 4 : (sizeIdx == 1) ? 2 : 1;

    QVariantMap map; map["size"] = size;
    enqueueToActive(Command(CmdType::READ_MEM_ABS, addr, 0, "", map));
}

void MainWindow::onMemWrite() {
    bool ok1, ok2;
    uint32_t addr = txtMemAddr->text().toUInt(&ok1, 16);
    uint32_t val = txtMemVal->text().toUInt(&ok2, 16);
    if (!ok1 || !ok2) { QMessageBox::warning(this, "Error", "Invalid Hex Values!"); return; }

    int sizeIdx = cmbMemSize->currentIndex();
    int size = (sizeIdx == 0) ? 4 : (sizeIdx == 1) ? 2 : 1;

    QVariantMap map; map["size"] = size;
    enqueueToActive(Command(CmdType::WRITE_MEM_ABS, addr, val, "", map));
}

void MainWindow::onMemReadRes(uint32_t sn, uint32_t addr, uint32_t val, int size) {
    if (sn != cmbActiveProbe->currentData().toUInt()) return;
    QJsonObject r1; r1["type"]="memread"; r1["sn"]=QString::number(sn); r1["addr"]=QString("0x%1").arg(addr,8,16,QChar('0')); r1["val"]=QString("0x%1").arg(val,size*2,16,QChar('0')); r1["size"]=size; ipcBroadcast(r1);
    int chars = (size == 4) ? 8 : (size == 2) ? 4 : 2;
    txtMemVal->setText(QString("0x%1").arg(val, chars, 16, QChar('0')).toUpper());
}
