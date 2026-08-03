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
#include <QComboBox>
#include <QGroupBox>
#include <QMap>
#include <QTextEdit>
#include <QResizeEvent>
#include <QDialog>
#include <QVBoxLayout>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include "DebugWorkers.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent *event) override;

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

    void onMemRead();
    void onMemWrite();
    void onMemReadRes(uint32_t sn, uint32_t addr, uint32_t val, int size);

    void selectFirmware();
    void triggerFlashAll();
    void triggerApplyConfig();
    void updateAls();
    void updateLed();
    void onLedModeChanged();
    void exportDxf();

private:
    void setupUI();
    void initDatabase();
    void addLogItem(int id, const QString& uuid, bool success);
    void saveLogToDb(const QString& uuid, bool success);
    QString parseHexVersion(const QString& path);

    void addProbeToUI(uint32_t sn, ProbeType type);
    void removeProbeFromUI(uint32_t sn);
    void enqueueToActive(const Command& cmd);

    QSqlDatabase db;
    int sessionCounter = 0;

    QMap<uint32_t, BaseWorker*> activeWorkers;
    QMap<uint32_t, int> probeRowMap;
    QMap<uint32_t, QString> probeUuids;
    QTimer *autoScanTimer;

    QLabel *lblIndicator;
    QLabel *lblStatus;
    QComboBox *cmbActiveProbe;
    QComboBox *cmbSpeed;
    QLineEdit *txtUuid;
    QPushButton *btnDxf;
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

    QLineEdit *txtMemAddr;
    QComboBox *cmbMemSize;
    QLineEdit *txtMemVal;

    QComboBox *cmbLedMode;
    QSpinBox *spinLed;
    QCheckBox *chkBlockPhysKeys;

    QLabel *lblPassFail;
    QTableWidget *logTable;

    QChart *chart;
    QLineSeries *seriesVBatt;
    QLineSeries *seriesBrt;
    QValueAxis *axisX;
    QValueAxis *axisY1;
    QValueAxis *axisY2;
    int plotTime = 0;
};