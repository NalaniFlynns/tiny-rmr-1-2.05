#pragma once
#include <QMainWindow>
#include <QTableWidget>
#include <QSqlDatabase>
#include <QProgressBar>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include <QTabWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QComboBox>
#include <QGroupBox>
#include <QMap>
#include <QTextEdit>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QSplitter>
#include <QDialog>
#include <QVBoxLayout>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QHash>
#include <QQueue>
#include <QSet>

#include "DebugWorkers.h"
#include "functest.h"
#include "PowerZWorker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

public slots:
    void onRawJLinkLog(const QString& log);

private slots:
    void scanProbes(bool isManual = false);
    void onActiveProbeChanged();
    void onSpeedChanged();
    void toggleConsoleWindow();

    void onStatus(uint32_t sn, int code, const QString& msg);
    void onUuid(uint32_t sn, const QString& uuid);
    void onFwVer(uint32_t sn, const QString& ver);
    void onTelemetry(uint32_t sn, const QVariantMap& data);
    void onProgress(uint32_t sn, int pct, const QString& text);
    void onLog(uint32_t sn, const QString& text);
    void onMsg(uint32_t sn, const QString& title, const QString& text);
    void onAutoTestRes(uint32_t sn, bool success, const QString& msg);
    void onConfigRead(uint32_t sn, const QVariantMap& cfg);
    void onPowerZTelemetry(double vbus, double ibus, double vbusAvg, double ibusAvg, double tempC);

    void onMemRead();
    void onMemWrite();
    void onMemReadRes(uint32_t sn, uint32_t addr, uint32_t val, int size);

    void selectFirmware();
    void triggerFlashAll();
    void triggerApplyConfig();
    void updateAls();
    void updateLed();
    void onLedModeChanged();
    void showDmCode();

private:
    void setupUI();
    void initDatabase();
    void addLogItem(int id, const QString& uuid, bool success);
    void saveLogToDb(const QString& uuid, bool success);
    QString parseHexVersion(const QString& path);

    void addProbeToUI(uint32_t sn, ProbeType type, bool useXdsAdapter = false);
    void removeProbeFromUI(uint32_t sn);
    void enqueueToActive(const Command& cmd);

    void setupIpc();
    void applyTheme();
    void setFwPath(const QString& path);
    void showModalMsg(const QString& title, const QString& text, bool critical);
    void showNextModalMsg();
    void ipcSend(QTcpSocket *s, const QJsonObject& obj);
    void ipcBroadcast(const QJsonObject& obj);
    void ipcSendHello(QTcpSocket *s);
    void handleIpcRead(QTcpSocket *s);
    void handleIpcCommand(QTcpSocket *s, const QJsonObject& cmd);

    QSqlDatabase db;
    int sessionCounter = 0;

    QMap<uint32_t, BaseWorker*> activeWorkers;
    QMap<uint32_t, int> probeRowMap;
    QMap<uint32_t, QString> probeUuids;
    QMap<uint32_t, QString> probeFwVers;
    QTimer *autoScanTimer;

    QSplitter *globalSplitter = nullptr;
    QSplitter *rightSplitter = nullptr;
    QSplitter *testSplitter = nullptr;
    QSplitter *colsSplitter = nullptr;
    QTimer *layoutSaveTimer = nullptr;
    QHash<QSplitter*, QList<double>> pendingRatios;
    void saveLayoutState();
    void restoreLayoutState();
    void applyPendingRatios();

    QTabWidget *tabs = nullptr;
    FuncTestPanel *funcTest = nullptr;
    QLabel *lblIndicator;
    QLabel *lblStatus;
    QComboBox *cmbActiveProbe;
    QComboBox *cmbSpeed;
    QLineEdit *txtUuid;
    QPushButton *btnDxf;
    QLabel *dmPreview = nullptr;
    QLabel *lblVer;

    QLineEdit *txtFwPath;
    QLineEdit *txtHexVer;
    QTableWidget *probeTable;

    QWidget *consoleContainer;
    QTextEdit *txtGlobalLog;
    QDialog *consoleWindow;
    QVBoxLayout *vConsoleLayout;
    QPushButton *btnDetachConsole;

    QPushButton *btnFlashAll;

    QMap<int, QCheckBox*> featureCheckboxes;
    QMap<QString, QSpinBox*> cfgVars;
    QMap<QString, QLineEdit*> monVars;

    QCheckBox *chkPoll;
    QCheckBox *chkOvrAls;
    QSpinBox *spinLux;
    QLabel *lblAlsMap;
    void updateAlsPreview();

    QLineEdit *txtMemAddr;
    QComboBox *cmbMemSize;
    QLineEdit *txtMemVal;

    QComboBox *cmbLedMode;
    QSpinBox *spinPollMs;
    QElapsedTimer keyClockPlus, keyClockMinus, keyClockBoth;
    QPushButton *btnKeyPlus = nullptr, *btnKeyMinus = nullptr, *btnKeyBoth = nullptr;
    bool keyPlusHeld = false, keyMinusHeld = false, keyBothHeld = false;
    QTimer *keyUiTimer = nullptr;
    QSpinBox *spinLed;
    QCheckBox *chkBlockPhysKeys;

    QLabel *lblPassFail;
    QTableWidget *logTable;

    QChart *chartVBatt;
    QChart *chartLux;
    QChart *chartPwmBrt;
    QLineSeries *seriesVBatt;
    QLineSeries *seriesLux;
    QLineSeries *seriesPwm;
    QLineSeries *seriesBrt;
    QValueAxis *axisX1;
    QValueAxis *axisX2;
    QValueAxis *axisX3;
    QValueAxis *axisYV;
    QValueAxis *axisYL;
    QValueAxis *axisYP;
    void autoScaleAxis(QValueAxis *axis, QLineSeries *s1, QLineSeries *s2, qreal minSpan);
    QElapsedTimer plotClock;
    QTimer *chartRefreshTimer = nullptr;
    QChartView *viewVBatt = nullptr;
    QChartView *viewLux = nullptr;
    QChartView *viewPwmBrt = nullptr;

    /* Power-Z KM003C 真实电压电流计 */
    PowerZWorker *pzWorker = nullptr;
    QLabel *lblPzStatus = nullptr;
    QLineEdit *lePzV = nullptr;
    QLineEdit *lePzI = nullptr;
    QLineEdit *lePzP = nullptr;
    QLineEdit *lePzTemp = nullptr;
    QLineEdit *lePzVAvg = nullptr;
    QLineEdit *lePzIAvg = nullptr;
    QLineEdit *lePzAvgV = nullptr;
    QLineEdit *lePzAvgI = nullptr;
    QLineEdit *lePzAvgP = nullptr;
    QLineEdit *lePzStatSec = nullptr;
    QPushButton *btnPzReset = nullptr;

    /* 本地 IPC 调试接口 (127.0.0.1:17345, JSON 行协议) */
    QTcpServer *ipcServer = nullptr;
    QList<QTcpSocket*> ipcClients;
    QHash<QTcpSocket*, QByteArray> ipcBuffers;
    QHash<uint32_t, QVariantMap> lastTelemetry;
    QHash<uint32_t, QString> lastUuid;
    QHash<uint32_t, QString> lastFwVer;
    QHash<uint32_t, int> lastStatusCode;
    QHash<uint32_t, QString> lastStatusMsg;

    struct ModalMsg { QString title; QString text; bool critical; QString key; };
    QMessageBox *modalMsgBox = nullptr;
    QString modalMsgBoxKey;
    QQueue<ModalMsg> m_msgQueue;
    QSet<QString> m_msgKeys;
};
