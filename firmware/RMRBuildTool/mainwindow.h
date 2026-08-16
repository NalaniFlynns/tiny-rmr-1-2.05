#pragma once
#include <QMainWindow>
#include <QThread>
#include <QList>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include "keystore.h"

class QLineEdit;
class QComboBox;
class QPushButton;
class QTextEdit;
class QProgressBar;
class QLabel;
class QRadioButton;
class QStackedWidget;
class QTcpServer;
class QTcpSocket;
class BuildRunner;
class EncryptWorker;
struct EncryptJob;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString& initialDir = QString(), QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void browseProjectDir();
    void browseGmake();
    void browseHexIn();
    void browseHexOut();
    void startBuild();
    void stopBuild();
    void onBuildFinished(bool ok, const QStringList& hexFiles, const QString& error);
    void onAuthChanged();
    void regeneratePassword();
    void copyPassword();
    void savePassword();
    void newCredential();
    void deleteCredential();
    void refreshCredentialList();
    void startEncrypt();
    void onEncryptFinished(bool ok, const QString& error, const QString& summary);
    void autofillOutPath();
    void loadProjectMeta();
    QString currentVersion();

private:
    QWidget* buildCard();
    QWidget* encryptCard();
    void applyStyle();
    void appendLog(const QString& text, int kind);
    QString pickGmake();

    /* ---- FWSEC1 加密流程拆分: UI 组装任务 / 执行任务, 供 IPC 复用 ---- */
    bool buildEncryptJobFromUi(EncryptJob& job, QString& err);
    void runEncrypt(const EncryptJob& job);

    /* ---- 本地 IPC 调试接口 (127.0.0.1:17346, JSON 行协议, 与 RMRDebugger 同构) ---- */
    void setupIpc();
    void ipcSend(QTcpSocket* s, const QJsonObject& obj);
    void ipcBroadcast(const QJsonObject& obj);
    void ipcSendHello(QTcpSocket* s);
    void handleIpcRead(QTcpSocket* s);
    void handleIpcCommand(QTcpSocket* s, const QJsonObject& cmd);
    QJsonObject stateJson();
    void broadcastState();
    QJsonArray credsJson();

    QLineEdit* m_edProject = nullptr;
    QLineEdit* m_edGmake = nullptr;
    QLineEdit* m_edVersion = nullptr;
    QLineEdit* m_edNote = nullptr;
    QComboBox* m_cmbVariant = nullptr;
    QPushButton* m_btnBuild = nullptr;
    QPushButton* m_btnStop = nullptr;
    QProgressBar* m_barBuild = nullptr;
    QLabel* m_lblBuildStatus = nullptr;
    QTextEdit* m_log = nullptr;
    QLabel* m_lblBuildResult = nullptr;

    QLineEdit* m_edHexIn = nullptr;
    QRadioButton* m_radPwd = nullptr;
    QRadioButton* m_radFido = nullptr;
    QRadioButton* m_radPasskey = nullptr;
    QStackedWidget* m_authStack = nullptr;
    QLineEdit* m_edPassword = nullptr;
    QComboBox* m_cmbCred = nullptr;
    QLabel* m_lblAuthInfo = nullptr;
    QComboBox* m_cmbBlock = nullptr;
    QLineEdit* m_edHexOut = nullptr;
    QPushButton* m_btnEncrypt = nullptr;
    QProgressBar* m_barEncrypt = nullptr;
    QLabel* m_lblEncryptResult = nullptr;

    BuildRunner* m_buildRunner = nullptr;
    QThread* m_encThread = nullptr;
    EncryptWorker* m_encWorker = nullptr;
    std::vector<FidoCredential> m_creds;
    QString m_lastBuiltHex;

    /* 本地 IPC 调试接口 (127.0.0.1:17346, JSON 行协议) */
    QTcpServer* ipcServer = nullptr;
    QList<QTcpSocket*> ipcClients;
    QHash<QTcpSocket*, QByteArray> ipcBuffers;
};