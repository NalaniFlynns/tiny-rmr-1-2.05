#pragma once
#include <QMainWindow>
#include <QThread>
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
class BuildRunner;
class EncryptWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
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
};