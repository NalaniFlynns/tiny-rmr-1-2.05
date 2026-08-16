#include "mainwindow.h"
#include "buildrunner.h"
#include "encryptworker.h"
#include "pwgen.h"
#include "fwsec_container.h"
#include "fwsec_webauthn.h"
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QProgressBar>
#include <QLabel>
#include <QRadioButton>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRandomGenerator>
#include <QClipboard>
#include <QGuiApplication>
#include <QDateTime>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimer>

static const char* APP_QSS = R"(
QWidget { background: #F3F4F6; color: #1F2328; font-size: 13px; }
QFrame#Card { background: #FFFFFF; border: 1px solid #E2E4E8; border-radius: 8px; }
QLabel#Title { font-size: 16px; font-weight: 700; color: #111827; }
QLabel#Section { font-size: 12px; font-weight: 600; color: #6B7280; }
QLabel#Hint { font-size: 12px; color: #6B7280; }
QLabel#Result { font-size: 12px; color: #374151; }
QLineEdit, QComboBox { background: #FFFFFF; border: 1px solid #D1D5DB; border-radius: 6px; padding: 5px 8px; selection-background-color: #2563EB; }
QLineEdit:focus, QComboBox:focus { border: 1px solid #2563EB; }
QPushButton { background: #FFFFFF; border: 1px solid #D1D5DB; border-radius: 6px; padding: 6px 14px; }
QPushButton:hover { background: #F9FAFB; border-color: #9CA3AF; }
QPushButton:pressed { background: #F3F4F6; }
QPushButton#Primary { background: #2563EB; color: #FFFFFF; border: none; font-weight: 600; }
QPushButton#Primary:hover { background: #1D4ED8; }
QPushButton#Primary:disabled { background: #93C5FD; }
QPushButton#Danger { color: #B91C1C; }
QProgressBar { background: #E5E7EB; border: none; border-radius: 4px; height: 8px; text-align: center; color: transparent; }
QProgressBar::chunk { background: #2563EB; border-radius: 4px; }
QTextEdit#Log { background: #111418; color: #E6E8EB; border: none; border-radius: 6px; font-family: Consolas, monospace; font-size: 12px; }
QRadioButton { spacing: 6px; }
QRadioButton::indicator { width: 14px; height: 14px; border-radius: 7px; border: 1px solid #9CA3AF; background: #FFFFFF; }
QRadioButton::indicator:checked { background: #2563EB; border: 4px solid #FFFFFF; outline: 1px solid #2563EB; }
QGroupBox { border: 1px solid #E2E4E8; border-radius: 6px; margin-top: 8px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #6B7280; font-size: 12px; }
)";

MainWindow::MainWindow(const QString& initialDir, QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("RMR 一键编译工具");
    resize(1080, 720);
    applyStyle();

    m_buildRunner = new BuildRunner(this);
    connect(m_buildRunner, &BuildRunner::logLine, this, [this](const QString& t, int k) { appendLog(t, k); });
    connect(m_buildRunner, &BuildRunner::progress, this, [this](int p, const QString& t) {
        m_barBuild->setValue(p);
        m_lblBuildStatus->setText(t);
        QJsonObject o; o["type"] = "progress"; o["pct"] = p; o["text"] = t; ipcBroadcast(o);
    });
    connect(m_buildRunner, &BuildRunner::finished, this, &MainWindow::onBuildFinished);

    QWidget* central = new QWidget(this);
    QHBoxLayout* root = new QHBoxLayout(central);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(16);
    root->addWidget(buildCard(), 11);
    root->addWidget(encryptCard(), 10);
    setCentralWidget(central);

    // Defaults: 优先使用命令行传入的工程目录, 否则回退默认仓库路径
    QString repoDir = initialDir;
    if (repoDir.isEmpty()) {
        QDir defaultRepo("C:/Users/Nalani/Documents/GitHub/tiny-rmr-1-2.05/firmware/RMR");
        if (defaultRepo.exists()) repoDir = defaultRepo.absolutePath();
    }
    QDir repo(repoDir);
    if (repo.exists() && QFile::exists(repo.filePath("app_config.h")))
        m_edProject->setText(repo.absolutePath());
    connect(m_edProject, &QLineEdit::editingFinished, this, &MainWindow::loadProjectMeta);
    loadProjectMeta();
    m_edGmake->setText(pickGmake());
    regeneratePassword();
    refreshCredentialList();
    onAuthChanged();
    autofillOutPath();
    setupIpc();
}

/* ================= 本地 IPC 调试接口 (127.0.0.1:17346, JSON 行协议, 与 RMRDebugger 同构) ================= */
void MainWindow::setupIpc()
{
    ipcServer = new QTcpServer(this);
    if (!ipcServer->listen(QHostAddress::LocalHost, 17346)) {
        appendLog(QString("[SYS] IPC 接口启动失败: %1").arg(ipcServer->errorString()), 2);
        return;
    }
    appendLog("[SYS] IPC 接口已启动: 127.0.0.1:17346 (JSON)", 0);
    connect(ipcServer, &QTcpServer::newConnection, this, [this](){
        while (QTcpSocket* s = ipcServer->nextPendingConnection()) {
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

void MainWindow::ipcSend(QTcpSocket* s, const QJsonObject& obj)
{
    if (!s) return;
    QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    s->write(line);
    s->flush();
}

void MainWindow::ipcBroadcast(const QJsonObject& obj)
{
    if (ipcClients.isEmpty()) return;
    QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    for (QTcpSocket* s : ipcClients) { s->write(line); s->flush(); }
}

QJsonArray MainWindow::credsJson()
{
    QJsonArray arr;
    for (const FidoCredential& c : m_creds) {
        QJsonObject o;
        o["name"] = c.name;
        o["rp_id"] = c.rpId;
        o["device"] = c.device;
        o["yubico_id"] = c.yubicoId;
        o["cred_id"] = QString::fromLatin1(c.credId.toBase64());
        arr.append(o);
    }
    return arr;
}

QJsonObject MainWindow::stateJson()
{
    QJsonObject st;
    st["type"] = "state";
    st["app"] = "RMRBuildTool";
    st["ts"] = QString::number(QDateTime::currentMSecsSinceEpoch());
    st["project_dir"] = m_edProject ? m_edProject->text() : QString();
    st["gmake"] = m_edGmake ? m_edGmake->text() : QString();
    st["version"] = m_edVersion ? m_edVersion->text() : QString();
    st["note"] = m_edNote ? m_edNote->text() : QString();
    st["all_variants"] = m_cmbVariant ? (m_cmbVariant->currentIndex() == 1) : false;
    st["building"] = m_buildRunner ? m_buildRunner->running() : false;
    st["build_progress"] = m_barBuild ? m_barBuild->value() : 0;
    st["build_status"] = m_lblBuildStatus ? m_lblBuildStatus->text() : QString();
    st["last_result"] = m_lblBuildResult ? m_lblBuildResult->text() : QString();
    st["hex_in"] = m_edHexIn ? m_edHexIn->text() : QString();
    st["hex_out"] = m_edHexOut ? m_edHexOut->text() : QString();
    st["auth"] = (m_radPwd && m_radPwd->isChecked()) ? 1 : ((m_radPasskey && m_radPasskey->isChecked()) ? 3 : 2);
    st["block_size"] = (qint64)(m_cmbBlock ? m_cmbBlock->currentData().toUInt() : 16384);
    st["encrypt_busy"] = (m_encThread != nullptr);
    st["creds"] = credsJson();
    return st;
}

void MainWindow::broadcastState()
{
    ipcBroadcast(stateJson());
}

void MainWindow::ipcSendHello(QTcpSocket* s)
{
    QJsonObject h = stateJson();
    h["type"] = "hello";
    ipcSend(s, h);
}

void MainWindow::handleIpcRead(QTcpSocket* s)
{
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
            QJsonObject r; r["type"] = "error"; r["msg"] = "bad json"; ipcSend(s, r);
            continue;
        }
        handleIpcCommand(s, doc.object());
    }
}

void MainWindow::handleIpcCommand(QTcpSocket* s, const QJsonObject& cmd)
{
    QString c = cmd["cmd"].toString();
    auto reply = [&](bool ok, const QString& msg = QString()){
        QJsonObject r; r["type"] = "reply"; r["cmd"] = c; r["ok"] = ok; r["msg"] = msg; ipcSend(s, r);
    };

    if (c == "ping") { reply(true, "pong"); return; }
    if (c == "state") { ipcSendHello(s); return; }
    if (c == "setdir") {
        QString path = cmd["path"].toString();
        if (path.isEmpty() || !QFile::exists(QDir(path).filePath("app_config.h"))) { reply(false, "目录无效: 需包含 app_config.h"); return; }
        m_edProject->setText(QDir(path).absolutePath());
        loadProjectMeta();
        reply(true, "工程目录: " + m_edProject->text());
        broadcastState();
        return;
    }
    if (c == "setgmake") {
        QString path = cmd["path"].toString();
        if (path.isEmpty() || !QFile::exists(path)) { reply(false, "gmake 路径不存在"); return; }
        m_edGmake->setText(path);
        reply(true, "gmake: " + path);
        broadcastState();
        return;
    }
    if (c == "setversion") { m_edVersion->setText(cmd["ver"].toString()); reply(true, "version set"); broadcastState(); return; }
    if (c == "setnote") { m_edNote->setText(cmd["note"].toString()); reply(true, "note set"); broadcastState(); return; }
    if (c == "variant") {
        m_cmbVariant->setCurrentIndex(cmd["all"].toVariant().toBool() ? 1 : 0);
        reply(true, "variant set");
        broadcastState();
        return;
    }
    if (c == "build") {
        if (m_buildRunner->running()) { reply(false, "构建进行中"); return; }
        if (cmd.contains("all")) m_cmbVariant->setCurrentIndex(cmd["all"].toVariant().toBool() ? 1 : 0);
        if (cmd.contains("version")) m_edVersion->setText(cmd["version"].toString());
        if (cmd.contains("note")) m_edNote->setText(cmd["note"].toString());
        QString dir = m_edProject->text().trimmed();
        QString gmake = m_edGmake->text().trimmed();
        if (dir.isEmpty() || !QFile::exists(QDir(dir).filePath("app_config.h"))) { reply(false, "未选择工程目录"); return; }
        if (gmake.isEmpty() || !QFile::exists(gmake)) { reply(false, "gmake 路径无效"); return; }
        m_log->clear();
        m_barBuild->setValue(0);
        m_lblBuildResult->clear();
        m_btnBuild->setEnabled(false);
        m_btnStop->setEnabled(true);
        m_buildRunner->startBuild(dir, m_cmbVariant->currentIndex() == 1, gmake, m_edVersion->text(), m_edNote->text());
        reply(true, "build started");
        broadcastState();
        return;
    }
    if (c == "stop") {
        if (!m_buildRunner->running()) { reply(false, "无进行中的构建"); return; }
        stopBuild();
        reply(true, "stop requested");
        broadcastState();
        return;
    }
    if (c == "genpwd") {
        regeneratePassword();
        QJsonObject r; r["type"] = "reply"; r["cmd"] = c; r["ok"] = true; r["msg"] = "password regenerated";
        r["password"] = m_edPassword ? m_edPassword->text() : QString();
        ipcSend(s, r);
        QJsonObject p; p["type"] = "password"; p["password"] = m_edPassword ? m_edPassword->text() : QString();
        ipcBroadcast(p);
        broadcastState();
        return;
    }
    if (c == "getpwd") {
        QJsonObject r; r["type"] = "reply"; r["cmd"] = c; r["ok"] = true;
        r["msg"] = m_edPassword ? m_edPassword->text() : QString();
        ipcSend(s, r);
        return;
    }
    if (c == "setpwd") {
        if (m_radPwd && m_radPwd->isChecked()) m_edPassword->setText(cmd["password"].toString());
        reply(true, "password set");
        broadcastState();
        return;
    }
    if (c == "setauth") {
        int a = cmd["auth"].toInt(1);
        if (a == 1) m_radPwd->setChecked(true);
        else if (a == 2) m_radFido->setChecked(true);
        else if (a == 3) m_radPasskey->setChecked(true);
        else { reply(false, "auth 取值: 1=密码 2=FIDO2 3=通行证"); return; }
        reply(true, "auth set");
        broadcastState();
        return;
    }
    if (c == "encrypt") {
        if (m_encThread) { reply(false, "加密进行中"); return; }
        if (cmd.contains("in")) { m_edHexIn->setText(cmd["in"].toString()); autofillOutPath(); }
        if (cmd.contains("out")) m_edHexOut->setText(cmd["out"].toString());
        if (cmd.contains("version")) m_edVersion->setText(cmd["version"].toString());
        if (cmd.contains("note")) m_edNote->setText(cmd["note"].toString());
        if (cmd.contains("blockSize")) {
            int idx = m_cmbBlock->findData(cmd["blockSize"].toInt());
            if (idx >= 0) m_cmbBlock->setCurrentIndex(idx);
        }
        if (cmd.contains("auth")) {
            int a = cmd["auth"].toInt();
            if (a == 1) m_radPwd->setChecked(true);
            else if (a == 2) m_radFido->setChecked(true);
            else if (a == 3) m_radPasskey->setChecked(true);
        }
        if (cmd.contains("password") && m_radPwd->isChecked())
            m_edPassword->setText(cmd["password"].toString());
        if (cmd.contains("credId")) {
            QByteArray want = QByteArray::fromBase64(cmd["credId"].toString().toLatin1());
            for (int i = 0; i < m_cmbCred->count(); ++i)
                if (m_cmbCred->itemData(i).toByteArray() == want) { m_cmbCred->setCurrentIndex(i); break; }
        }
        if (m_edHexIn->text().trimmed().isEmpty() || !QFile::exists(m_edHexIn->text().trimmed())) { reply(false, "请先选择要加密的 hex"); return; }
        if (m_edHexOut->text().trimmed().isEmpty()) autofillOutPath();
        EncryptJob job;
        QString err;
        if (!buildEncryptJobFromUi(job, err)) { reply(false, err); return; }
        runEncrypt(job);
        reply(true, "encrypt started");
        return;
    }
    if (c == "creds") {
        QJsonObject r; r["type"] = "reply"; r["cmd"] = c; r["ok"] = true; r["creds"] = credsJson();
        ipcSend(s, r);
        return;
    }
    if (c == "quit") {
        reply(true, "bye");
        QTimer::singleShot(150, this, &QWidget::close);
        return;
    }
    reply(false, "unknown cmd: " + c);
}
MainWindow::~MainWindow()
{
    if (m_encThread) {
        m_encThread->quit();
        m_encThread->wait(3000);
    }
}

void MainWindow::applyStyle()
{
    setStyleSheet(QString::fromUtf8(APP_QSS));
}

QString MainWindow::pickGmake()
{
    QStringList cands = {
        "C:/ti/ccs2100/ccs/utils/bin/gmake.exe",
        "C:/ti/ccs1240/ccs/utils/bin/gmake.exe",
        "C:/ti/ccs2000/ccs/utils/bin/gmake.exe",
    };
    for (const QString& c : cands)
        if (QFile::exists(c)) return c;
    return {};
}

QWidget* MainWindow::buildCard()
{
    QFrame* card = new QFrame(this);
    card->setObjectName("Card");
    QVBoxLayout* v = new QVBoxLayout(card);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(10);

    QLabel* title = new QLabel("一键编译", card);
    title->setObjectName("Title");
    v->addWidget(title);

    QFormLayout* form = new QFormLayout;
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(10);

    QWidget* rowProject = new QWidget(card);
    QHBoxLayout* hlp = new QHBoxLayout(rowProject); hlp->setContentsMargins(0,0,0,0); hlp->setSpacing(6);
    m_edProject = new QLineEdit(rowProject);
    QPushButton* btnBrowseProject = new QPushButton("浏览", rowProject);
    connect(btnBrowseProject, &QPushButton::clicked, this, &MainWindow::browseProjectDir);
    hlp->addWidget(m_edProject, 1); hlp->addWidget(btnBrowseProject);
    form->addRow("工程目录", rowProject);

    QWidget* rowGmake = new QWidget(card);
    QHBoxLayout* hlg = new QHBoxLayout(rowGmake); hlg->setContentsMargins(0,0,0,0); hlg->setSpacing(6);
    m_edGmake = new QLineEdit(rowGmake);
    QPushButton* btnBrowseGmake = new QPushButton("浏览", rowGmake);
    connect(btnBrowseGmake, &QPushButton::clicked, this, &MainWindow::browseGmake);
    hlg->addWidget(m_edGmake, 1); hlg->addWidget(btnBrowseGmake);
    form->addRow("gmake 路径", rowGmake);

    m_cmbVariant = new QComboBox(card);
    m_cmbVariant->addItem("默认变体 (DBGL)");
    m_cmbVariant->addItem("全部 6 变体 (DBG/DBGL/DIRECT/BATT/ECO_D/ECO_B)");
    form->addRow("构建范围", m_cmbVariant);

    m_edVersion = new QLineEdit(card);
    m_edVersion->setPlaceholderText("如 V4.4.0, 留空用工程默认");
    m_edVersion->setMaxLength(14);
    form->addRow("版本号", m_edVersion);

    m_edNote = new QLineEdit(card);
    m_edNote->setPlaceholderText("本次更新说明 (仅随加密固件嵌入, 解码时显示)");
    form->addRow("更新说明(仅加密)", m_edNote);

    v->addLayout(form);

    QWidget* rowBtn = new QWidget(card);
    QHBoxLayout* hlb = new QHBoxLayout(rowBtn); hlb->setContentsMargins(0,0,0,0); hlb->setSpacing(8);
    m_btnBuild = new QPushButton("开始构建", rowBtn); m_btnBuild->setObjectName("Primary");
    m_btnStop = new QPushButton("停止", rowBtn);
    m_btnStop->setEnabled(false);
    connect(m_btnBuild, &QPushButton::clicked, this, &MainWindow::startBuild);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::stopBuild);
    hlb->addWidget(m_btnBuild); hlb->addWidget(m_btnStop); hlb->addStretch(1);
    v->addWidget(rowBtn);

    m_barBuild = new QProgressBar(card);
    m_barBuild->setRange(0, 100);
    m_barBuild->setValue(0);
    v->addWidget(m_barBuild);

    m_lblBuildStatus = new QLabel("就绪", card);
    m_lblBuildStatus->setObjectName("Hint");
    v->addWidget(m_lblBuildStatus);

    m_log = new QTextEdit(card);
    m_log->setObjectName("Log");
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(280);
    v->addWidget(m_log, 1);

    m_lblBuildResult = new QLabel("", card);
    m_lblBuildResult->setObjectName("Result");
    m_lblBuildResult->setWordWrap(true);
    v->addWidget(m_lblBuildResult);

    return card;
}

QWidget* MainWindow::encryptCard()
{
    QFrame* card = new QFrame(this);
    card->setObjectName("Card");
    QVBoxLayout* v = new QVBoxLayout(card);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(10);

    QLabel* title = new QLabel("加密固件 (FWSEC1)", card);
    title->setObjectName("Title");
    v->addWidget(title);

    QFormLayout* form = new QFormLayout;
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(10);

    QWidget* rowIn = new QWidget(card);
    QHBoxLayout* hli = new QHBoxLayout(rowIn); hli->setContentsMargins(0,0,0,0); hli->setSpacing(6);
    m_edHexIn = new QLineEdit(rowIn);
    QPushButton* btnIn = new QPushButton("浏览", rowIn);
    connect(btnIn, &QPushButton::clicked, this, &MainWindow::browseHexIn);
    hli->addWidget(m_edHexIn, 1); hli->addWidget(btnIn);
    form->addRow("固件 hex", rowIn);

    QWidget* rowAuth = new QWidget(card);
    QHBoxLayout* hla = new QHBoxLayout(rowAuth); hla->setContentsMargins(0,0,0,0); hla->setSpacing(10);
    m_radPwd = new QRadioButton("自动强密码", rowAuth);
    m_radFido = new QRadioButton("FIDO2 安全密钥", rowAuth);
    m_radPasskey = new QRadioButton("手机通行证", rowAuth);
    m_radPwd->setChecked(true);
    hla->addWidget(m_radPwd); hla->addWidget(m_radFido); hla->addWidget(m_radPasskey); hla->addStretch(1);
    form->addRow("解锁方式", rowAuth);

    v->addLayout(form);

    m_authStack = new QStackedWidget(card);

    // Page 0: password
    QWidget* pagePwd = new QWidget(m_authStack);
    QVBoxLayout* lp = new QVBoxLayout(pagePwd); lp->setContentsMargins(0,0,0,0); lp->setSpacing(8);
    QWidget* rowPw = new QWidget(pagePwd);
    QHBoxLayout* hlpw = new QHBoxLayout(rowPw); hlpw->setContentsMargins(0,0,0,0); hlpw->setSpacing(6);
    m_edPassword = new QLineEdit(rowPw);
    m_edPassword->setReadOnly(true);
    QPushButton* btnRegen = new QPushButton("重新生成", rowPw);
    QPushButton* btnCopy = new QPushButton("复制", rowPw);
    QPushButton* btnSave = new QPushButton("保存到文件", rowPw);
    connect(btnRegen, &QPushButton::clicked, this, &MainWindow::regeneratePassword);
    connect(btnCopy, &QPushButton::clicked, this, &MainWindow::copyPassword);
    connect(btnSave, &QPushButton::clicked, this, &MainWindow::savePassword);
    hlpw->addWidget(m_edPassword, 1); hlpw->addWidget(btnRegen); hlpw->addWidget(btnCopy); hlpw->addWidget(btnSave);
    lp->addWidget(rowPw);
    QLabel* hintPwd = new QLabel("密码仅此一次展示，请立即复制或保存。丢失后无法解密固件。", pagePwd);
    hintPwd->setObjectName("Hint");
    hintPwd->setWordWrap(true);
    lp->addWidget(hintPwd);
    m_authStack->addWidget(pagePwd);

    // Page 1: fido / passkey
    QWidget* pageFido = new QWidget(m_authStack);
    QVBoxLayout* lf = new QVBoxLayout(pageFido); lf->setContentsMargins(0,0,0,0); lf->setSpacing(8);
    QWidget* rowCred = new QWidget(pageFido);
    QHBoxLayout* hlc = new QHBoxLayout(rowCred); hlc->setContentsMargins(0,0,0,0); hlc->setSpacing(6);
    m_cmbCred = new QComboBox(rowCred);
    QPushButton* btnNew = new QPushButton("新建凭据", rowCred);
    QPushButton* btnDel = new QPushButton("删除", rowCred);
    connect(btnNew, &QPushButton::clicked, this, &MainWindow::newCredential);
    connect(btnDel, &QPushButton::clicked, this, &MainWindow::deleteCredential);
    hlc->addWidget(m_cmbCred, 1); hlc->addWidget(btnNew); hlc->addWidget(btnDel);
    lf->addWidget(rowCred);
    m_lblAuthInfo = new QLabel("", pageFido);
    m_lblAuthInfo->setObjectName("Hint");
    m_lblAuthInfo->setWordWrap(true);
    lf->addWidget(m_lblAuthInfo);
    m_authStack->addWidget(pageFido);

    v->addWidget(m_authStack);

    QFormLayout* form2 = new QFormLayout;
    form2->setHorizontalSpacing(8);
    form2->setVerticalSpacing(10);
    m_cmbBlock = new QComboBox(card);
    m_cmbBlock->addItem("4 KB", 4096);
    m_cmbBlock->addItem("16 KB", 16384);
    m_cmbBlock->addItem("64 KB", 65536);
    m_cmbBlock->setCurrentIndex(1);
    form2->addRow("分块大小", m_cmbBlock);

    QWidget* rowOut = new QWidget(card);
    QHBoxLayout* hlo = new QHBoxLayout(rowOut); hlo->setContentsMargins(0,0,0,0); hlo->setSpacing(6);
    m_edHexOut = new QLineEdit(rowOut);
    QPushButton* btnOut = new QPushButton("浏览", rowOut);
    connect(btnOut, &QPushButton::clicked, this, &MainWindow::browseHexOut);
    hlo->addWidget(m_edHexOut, 1); hlo->addWidget(btnOut);
    form2->addRow("输出文件", rowOut);
    v->addLayout(form2);

    m_btnEncrypt = new QPushButton("生成加密固件", card);
    m_btnEncrypt->setObjectName("Primary");
    connect(m_btnEncrypt, &QPushButton::clicked, this, &MainWindow::startEncrypt);
    v->addWidget(m_btnEncrypt);

    m_barEncrypt = new QProgressBar(card);
    m_barEncrypt->setRange(0, 100);
    m_barEncrypt->setValue(0);
    v->addWidget(m_barEncrypt);

    m_lblEncryptResult = new QLabel("", card);
    m_lblEncryptResult->setObjectName("Result");
    m_lblEncryptResult->setWordWrap(true);
    v->addWidget(m_lblEncryptResult);
    v->addStretch(1);

    connect(m_radPwd, &QRadioButton::toggled, this, &MainWindow::onAuthChanged);
    connect(m_radFido, &QRadioButton::toggled, this, &MainWindow::onAuthChanged);
    connect(m_radPasskey, &QRadioButton::toggled, this, &MainWindow::onAuthChanged);
    connect(m_edHexIn, &QLineEdit::textChanged, this, &MainWindow::autofillOutPath);

    return card;
}

void MainWindow::browseProjectDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择 CCS 工程目录", m_edProject->text());
    if (!dir.isEmpty()) m_edProject->setText(dir);
}

void MainWindow::browseGmake()
{
    QString f = QFileDialog::getOpenFileName(this, "选择 gmake.exe", m_edGmake->text(), "gmake.exe");
    if (!f.isEmpty()) m_edGmake->setText(f);
}

void MainWindow::browseHexIn()
{
    QString f = QFileDialog::getOpenFileName(this, "选择固件 hex", m_edHexIn->text(), "固件 (*.hex)");
    if (!f.isEmpty()) m_edHexIn->setText(f);
}

void MainWindow::browseHexOut()
{
    QString f = QFileDialog::getSaveFileName(this, "保存加密固件", m_edHexOut->text(), "FWSEC1 加密固件 (*.fwsec)");
    if (!f.isEmpty()) m_edHexOut->setText(f);
}

void MainWindow::appendLog(const QString& text, int kind)
{
    QString color = kind == 1 ? "#4ADE80" : (kind == 2 ? "#F87171" : "#E6E8EB");
    QString esc = text.toHtmlEscaped();
    m_log->append(QString("<span style=\"color:%1\">%2</span>").arg(color, esc));
    QJsonObject o; o["type"] = "log"; o["kind"] = kind; o["text"] = text; ipcBroadcast(o);
}

void MainWindow::startBuild()
{
    QString dir = m_edProject->text().trimmed();
    QString gmake = m_edGmake->text().trimmed();
    if (dir.isEmpty() || !QFile::exists(QDir(dir).filePath("app_config.h"))) {
        QMessageBox::warning(this, "提示", "请选择包含 app_config.h 的 CCS 工程目录");
        return;
    }
    if (gmake.isEmpty() || !QFile::exists(gmake)) {
        QMessageBox::warning(this, "提示", "请选择 gmake.exe 路径");
        return;
    }
    bool all = m_cmbVariant->currentIndex() == 1;
    m_log->clear();
    m_barBuild->setValue(0);
    m_lblBuildResult->clear();
    m_btnBuild->setEnabled(false);
    m_btnStop->setEnabled(true);
    m_buildRunner->startBuild(dir, all, gmake, m_edVersion->text(), m_edNote->text());
    broadcastState();
}

void MainWindow::stopBuild()
{
    m_buildRunner->stop();
    m_btnBuild->setEnabled(true);
    m_btnStop->setEnabled(false);
    broadcastState();
}

void MainWindow::onBuildFinished(bool ok, const QStringList& hexFiles, const QString& error)
{
    m_btnBuild->setEnabled(true);
    m_btnStop->setEnabled(false);
    QJsonObject bf; bf["type"] = "build_finished"; bf["ok"] = ok; bf["error"] = error;
    QJsonArray hexArr; for (const QString& h : hexFiles) hexArr.append(h);
    bf["hex"] = hexArr; ipcBroadcast(bf); broadcastState();
    if (ok) {
        m_barBuild->setValue(100);
        m_lblBuildStatus->setText("构建完成");
        QStringList rel;
        for (const QString& h : hexFiles) rel << QDir(m_edProject->text()).relativeFilePath(h);
        m_lblBuildResult->setText("产物: " + rel.join(", "));
        if (!hexFiles.isEmpty()) {
            m_lastBuiltHex = hexFiles.last();
            m_edHexIn->setText(m_lastBuiltHex);
            autofillOutPath();
        }
        appendLog("构建成功", 1);
    } else {
        m_lblBuildStatus->setText("构建失败");
        if (!error.isEmpty()) m_lblBuildResult->setText(error);
        appendLog("构建失败: " + error, 2);
        QMessageBox::critical(this, "构建失败", error);
    }
}

void MainWindow::onAuthChanged()
{
    if (!m_authStack) return;
    if (m_radFido->isChecked()) {
        m_authStack->setCurrentIndex(1);
        m_lblAuthInfo->setText("需要插入支持 PRF / HMAC-Secret 的 FIDO2 安全密钥 (如 YubiKey 5、SoloKey)。"
                               "加密时密钥从硬件内动态派生，全程不落盘。");
    } else if (m_radPasskey->isChecked()) {
        m_authStack->setCurrentIndex(1);
        m_lblAuthInfo->setText("将调起 Windows Hello 或手机通行证 (跨设备)。"
                               "密钥派生自平台安全硬件，强度等同 FIDO2。");
    } else {
        m_authStack->setCurrentIndex(0);
    }
    refreshCredentialList();
}

void MainWindow::regeneratePassword()
{
    m_edPassword->setText(generateStrongPassword());
}

void MainWindow::copyPassword()
{
    QGuiApplication::clipboard()->setText(m_edPassword->text());
    m_lblEncryptResult->setText("密码已复制到剪贴板");
}

void MainWindow::savePassword()
{
    QString f = QFileDialog::getSaveFileName(this, "保存密码", "RMR_fwsec_password.txt", "文本 (*.txt)");
    if (f.isEmpty()) return;
    QFile file(f);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write("RMR FWSEC1 固件解密密码 (请妥善保管)\n\n");
        file.write(m_edPassword->text().toUtf8());
        file.write("\n");
        m_lblEncryptResult->setText("密码已保存: " + f);
    } else {
        QMessageBox::warning(this, "提示", "无法写入文件");
    }
}

void MainWindow::refreshCredentialList()
{
    QString err;
    if (!Keystore::load(m_creds, err)) {
        QMessageBox::warning(this, "提示", err);
        return;
    }
    int device = m_radPasskey->isChecked() ? 2 : 1;
    m_cmbCred->clear();
    for (const FidoCredential& c : m_creds) {
        if (c.device != device) continue;
        m_cmbCred->addItem(QString("%1 (%2)").arg(c.name, c.rpId), QVariant::fromValue(c.credId));
    }
    if (m_cmbCred->count() == 0)
        m_cmbCred->addItem("(暂无凭据，点击新建凭据)", QByteArray());
}

void MainWindow::newCredential()
{
    bool device2 = m_radPasskey->isChecked();
    QString typeName = device2 ? "手机通行证 / Windows Hello" : "FIDO2 安全密钥";
    bool ok = false;
    QString name = QInputDialog::getText(this, "新建凭据", "凭据名称:", QLineEdit::Normal, "RMR 固件密钥", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString rpId = QInputDialog::getText(this, "新建凭据", "RP ID:", QLineEdit::Normal, "rmr.local", &ok);
    if (!ok || rpId.trimmed().isEmpty()) return;

    std::vector<uint8_t> cid;
    std::string err;
    if (!fwsec::webauthn_make_credential((void*)winId(), rpId.trimmed().toStdString(),
                                         name.trimmed().toStdString(), device2 ? 2 : 1,
                                         device2, cid, err)) {
        QMessageBox::warning(this, "创建失败", QString::fromStdString(err));
        return;
    }
    FidoCredential c;
    c.name = name.trimmed();
    c.rpId = rpId.trimmed();
    c.device = device2 ? 2 : 1;
    c.credId = QByteArray((const char*)cid.data(), (int)cid.size());
    m_creds.push_back(c);
    QString serr;
    Keystore::save(m_creds, serr);
    refreshCredentialList();
    int idx = m_cmbCred->findData(QVariant::fromValue(c.credId));
    if (idx >= 0) m_cmbCred->setCurrentIndex(idx);
}

void MainWindow::deleteCredential()
{
    if (m_cmbCred->count() == 0) return;
    QByteArray id = m_cmbCred->currentData().toByteArray();
    if (id.isEmpty()) return;
    auto it = std::find_if(m_creds.begin(), m_creds.end(), [&](const FidoCredential& c) { return c.credId == id; });
    if (it == m_creds.end()) return;
    if (QMessageBox::question(this, "删除凭据", "确定删除凭据 " + it->name + " ?") != QMessageBox::Yes) return;
    m_creds.erase(it);
    QString err;
    Keystore::save(m_creds, err);
    refreshCredentialList();
}

void MainWindow::autofillOutPath()
{
    QString in = m_edHexIn->text().trimmed();
    if (in.isEmpty()) return;
    QFileInfo fi(in);
    if (fi.suffix().compare("fwsec", Qt::CaseInsensitive) == 0) return;
    m_edHexOut->setText(fi.absolutePath() + "/" + fi.completeBaseName() + ".fwsec");
}

void MainWindow::startEncrypt()
{
    QString in = m_edHexIn->text().trimmed();
    QString out = m_edHexOut->text().trimmed();
    if (in.isEmpty() || !QFile::exists(in)) {
        QMessageBox::warning(this, "提示", "请选择要加密的 hex 文件");
        return;
    }
    if (out.isEmpty()) autofillOutPath();
    out = m_edHexOut->text().trimmed();
    if (out.isEmpty()) { QMessageBox::warning(this, "提示", "请选择输出路径"); return; }

    EncryptJob job;
    QString err;
    if (!buildEncryptJobFromUi(job, err)) {
        QMessageBox::warning(this, "提示", err);
        return;
    }
    runEncrypt(job);
}

bool MainWindow::buildEncryptJobFromUi(EncryptJob& job, QString& err)
{
    job.inPath = m_edHexIn->text().trimmed();
    job.outPath = m_edHexOut->text().trimmed();
    job.blockSize = (quint32)m_cmbBlock->currentData().toUInt();
    job.fileName = QFileInfo(job.inPath).fileName();
    job.fileVersion = m_edVersion->text().trimmed();
    if (job.fileVersion.isEmpty()) job.fileVersion = currentVersion();
    job.updateNote = m_edNote->text().trimmed();
    job.buildMeta = QString("build=%1,alg=FWSEC1v2:MLKEM768+ChaCha20Poly1305")
                        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));

    if (m_radPwd->isChecked()) {
        if (m_edPassword->text().isEmpty()) { err = "密码为空"; return false; }
        job.authType = 1;
        job.password = m_edPassword->text();
        return true;
    }

    QByteArray id = m_cmbCred->currentData().toByteArray();
    if (id.isEmpty()) { err = "请先新建凭据"; return false; }
    const FidoCredential* cred = nullptr;
    for (const FidoCredential& c : m_creds)
        if (c.credId == id) { cred = &c; break; }
    if (!cred) { err = "凭据不存在"; return false; }

    // 16-byte container salt + 32-byte PRF salt -> hardware-derived secret
    quint8 salt16[16], prfSalt[32], secret[32];
    quint32 rnd[4];
    QRandomGenerator::system()->fillRange(rnd);
    memcpy(salt16, rnd, 16);
    fwsec::webauthn_prf_salt(salt16, prfSalt);
    std::string werr;
    if (!fwsec::webauthn_get_prf_secret((void*)winId(), cred->rpId.toStdString(),
                                        std::vector<uint8_t>(cred->credId.begin(), cred->credId.end()),
                                        (uint8_t)cred->device, prfSalt, secret, werr)) {
        err = QString::fromStdString(werr);
        return false;
    }
    job.authType = m_radPasskey->isChecked() ? 3 : 2;
    job.device = cred->device;
    job.rpId = cred->rpId;
    job.credId = cred->credId;
    if (!cred->yubicoId.isEmpty()) job.buildMeta += ",yubico=" + cred->yubicoId;
    job.salt = QByteArray((const char*)salt16, 16);
    job.fidoSecret = QByteArray((const char*)secret, 32);
    fwsec::secure_zero(secret, 32);
    return true;
}

void MainWindow::runEncrypt(const EncryptJob& job)
{
    if (m_encThread) return;  // 加密进行中, 调用方已校验
    m_btnEncrypt->setEnabled(false);
    m_barEncrypt->setValue(0);
    m_lblEncryptResult->setText("加密中 ...");

    m_encThread = new QThread(this);
    m_encWorker = new EncryptWorker;
    m_encWorker->moveToThread(m_encThread);
    connect(m_encThread, &QThread::finished, m_encWorker, &QObject::deleteLater);
    connect(m_encThread, &QThread::finished, m_encThread, &QObject::deleteLater);
    connect(m_encWorker, &EncryptWorker::progress, this, [this](qint64 done, qint64 total) {
        int pct = total > 0 ? int(100.0 * done / total) : 0;
        m_barEncrypt->setValue(pct);
        QJsonObject o; o["type"] = "encrypt_progress"; o["done"] = (double)done; o["total"] = (double)total; o["pct"] = pct;
        ipcBroadcast(o);
    });
    connect(m_encWorker, &EncryptWorker::finished, this, &MainWindow::onEncryptFinished);
    m_encThread->start();
    QMetaObject::invokeMethod(m_encWorker, "doEncrypt", Qt::QueuedConnection, Q_ARG(EncryptJob, job));
    broadcastState();
}

void MainWindow::onEncryptFinished(bool ok, const QString& error, const QString& summary)
{
    m_btnEncrypt->setEnabled(true);
    QJsonObject ef; ef["type"] = "encrypt_finished"; ef["ok"] = ok; ef["error"] = error; ef["summary"] = summary;
    ipcBroadcast(ef); broadcastState();
    if (ok) {
        m_barEncrypt->setValue(100);
        m_lblEncryptResult->setText(summary);
        QString oneLine = summary; oneLine.replace("\n", " | ");
        appendLog(oneLine, 1);
        if (m_radPwd->isChecked())
            QMessageBox::information(this, "加密完成", summary + "\n\n请妥善保存密码，丢失将无法解密。");
        else
            QMessageBox::information(this, "加密完成", summary + "\n\n解密时需使用同一安全密钥 / 通行证。");
    } else {
        m_lblEncryptResult->setText("加密失败: " + error);
        appendLog("加密失败: " + error, 2);
    }
    if (m_encThread) {
        m_encThread->quit();
        m_encThread = nullptr;
        m_encWorker = nullptr;
    }
}

QString MainWindow::currentVersion()
{
    QFile f(QDir(m_edProject->text()).filePath("app_config.h"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QString txt = QString::fromUtf8(f.readAll());
    QRegularExpression re("FW_VERSION_STR \"(V[0-9]+\\.[0-9]+\\.[0-9]+)");
    QRegularExpressionMatch m = re.match(txt);
    return m.hasMatch() ? m.captured(1) : QString();
}

void MainWindow::loadProjectMeta()
{
    QString dir = m_edProject->text().trimmed();
    if (dir.isEmpty() || !QFile::exists(QDir(dir).filePath("app_config.h"))) return;
    QString ver = currentVersion();
    if (!ver.isEmpty()) m_edVersion->setText(ver);
}