#include "MainWindow.h"
#include "DebugWorkers.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QSqlQuery>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QFrame>
#include <QGraphicsLayout>
#include <QLegendMarker>
#include <QSplitter>
#include <vector>
#include <QTimer>
#include <QIcon>
#include <QSet>
#include <QSerialPortInfo>

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
    setWindowTitle("RMR Factory Programmer");
    setWindowIcon(QIcon(":/logo.ico"));
    setMinimumSize(950, 600);
    resize(1450, 950);

    initDatabase();
    setupUI();

    autoScanTimer = new QTimer(this);
    connect(autoScanTimer, &QTimer::timeout, this, [this](){ scanProbes(false); });
    autoScanTimer->start(2000);

    QTimer::singleShot(500, this, [this](){ scanProbes(false); });
}

MainWindow::~MainWindow() {
    g_mainWindowContext = nullptr;
    for (auto w : activeWorkers) {
        w->disconnect(this);
        w->stop();
        w->wait(1000);
        w->deleteLater();
    }
    if (consoleWindow) consoleWindow->deleteLater();
    db.close();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    double scaleW = this->width() / 1450.0;
    double scaleH = this->height() / 950.0;
    double scale = std::min(scaleW, scaleH);
    scale = std::max(0.55, std::min(scale, 1.8));

    int pt_sm = std::max(7, (int)(8 * scale));
    int pt_md = std::max(8, (int)(9 * scale));
    int pt_lg = std::max(9, (int)(10 * scale));
    int pad_btn = std::max(4, (int)(6 * scale));

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
                      ).arg(pt_md).arg(pt_lg).arg(pad_btn);
    this->setStyleSheet(qss);
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

    lblIndicator = new QLabel("●");
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

    btnDxf = new QPushButton("DXF");
    connect(btnDxf, &QPushButton::clicked, this, &MainWindow::exportDxf);
    hl->addWidget(btnDxf);

    lblVer = new QLabel("FW: N/A");
    lblVer->setStyleSheet("font-weight: bold; border: none; padding-left: 5px;");
    hl->addWidget(lblVer);

    mainLayout->addWidget(headerFrame);

    QSplitter *mainSplitter = new QSplitter(Qt::Vertical);
    mainSplitter->setHandleWidth(8);
    mainSplitter->setChildrenCollapsible(false);

    QTabWidget *tabs = new QTabWidget();
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

    btnFlashAll = new QPushButton("▶ Flash ALL Connected");
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
    QHBoxLayout *hCols = new QHBoxLayout();

    // =============== 列 1: 基础命令和设置 ===============
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
    QStringList prmTexts = {"R_Base(mΩ):", "Series(mΩ):", "LED Vf(mV):", "Max I(uA):", "Max P(uW):", "ALS Min:", "LVP Crit:", "LVP Ext:", "Def Lvl:"};
    QStringList prmKeys = {"r_base", "r_series", "v_fw", "i_max", "p_batt", "als_min", "lvp_crit", "lvp_ext", "def_lvl"};
    int prmMax[] = {9999, 999999, 5000, 100000, 99999999, 1000, 5000, 5000, 8};
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

    // =============== 列 2: 数据监控 ===============
    QVBoxLayout *col2 = new QVBoxLayout();
    QGroupBox *grpMon = new QGroupBox("Real-time Telemetry Monitor");
    QVBoxLayout *vMon = new QVBoxLayout(grpMon);
    chkPoll = new QCheckBox("Enable Background Polling");
    chkPoll->setStyleSheet("color: #0078D7; font-weight: bold;");
    connect(chkPoll, &QCheckBox::toggled, this, &MainWindow::onActiveProbeChanged);
    vMon->addWidget(chkPoll);

    QGridLayout *gridMon = new QGridLayout();
    QStringList monTexts = {"State:", "VBATT(mV):", "Est R(mΩ):", "Level(0-8):", "Brt Target:", "V_LED(mV):", "V-Limit:", "I-Lim(Brt):", "P-Limit(W):", "I-Lim(LED):", "Est.P(mW):", "Est I(mA):", "Peak I(mA):", "HW PWM:", "I2C Sensor:", "I2C Err:", "Lux(Filt):", "Lux(RAW):", "Btn[-] Pin:", "Btn[+] Pin:", "Run Flags:"};
    QStringList monKeys = {"state", "vbatt", "dyn_r", "level", "brt", "v_led", "l_v_drop", "l_i_brt", "l_p_avg", "l_i_led", "p_led", "i_avg", "i_peak", "pwm", "sensor", "err_cnt", "lux", "lux_raw", "raw_k_m", "raw_k_p", "flags"};
    for (int i = 0; i < monKeys.size(); ++i) {
        QLabel *l = new QLabel(monTexts[i]);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QLineEdit *le = new QLineEdit("-");
        le->setReadOnly(true);
        le->setStyleSheet("color: #0078D7; font-weight: bold; border: none; background: transparent;");
        monVars[monKeys[i]] = le;
        gridMon->addWidget(l, i, 0);
        gridMon->addWidget(le, i, 1);
    }
    vMon->addLayout(gridMon);
    vMon->addStretch();
    col2->addWidget(grpMon, 1);

    // =============== 列 3: 接管覆盖及测试 ===============
    QVBoxLayout *col3 = new QVBoxLayout();
    QGroupBox *grpOvr = new QGroupBox("Hardware Overrides");
    QVBoxLayout *vOvr = new QVBoxLayout(grpOvr);

    chkBlockPhysKeys = new QCheckBox("Block Hardware Interferences (Ignore physical buttons)");
    chkBlockPhysKeys->setStyleSheet("color: #D35400; font-weight: bold;");
    connect(chkBlockPhysKeys, &QCheckBox::toggled, this, [this](bool checked) {
        enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_BLOCK_PHYS_KEYS, checked ? 1 : 0));
    });
    vOvr->addWidget(chkBlockPhysKeys);

    QHBoxLayout *lKey = new QHBoxLayout();
    lKey->addWidget(new QLabel("Software UI:"));
    QPushButton *btnPlus = new QPushButton("[+]");
    connect(btnPlus, &QPushButton::pressed, [this](){ enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 1)); });
    connect(btnPlus, &QPushButton::released, [this](){ enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_PLUS, 0)); });
    QPushButton *btnMinus = new QPushButton("[-]");
    connect(btnMinus, &QPushButton::pressed, [this](){ enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 1)); });
    connect(btnMinus, &QPushButton::released, [this](){ enqueueToActive(Command(CmdType::WRITE_8, OFS_OVR_KEY_MINUS, 0)); });
    lKey->addWidget(btnPlus); lKey->addWidget(btnMinus);
    vOvr->addLayout(lKey);

    QHBoxLayout *lAls = new QHBoxLayout();
    chkOvrAls = new QCheckBox("Fake Lux:");
    connect(chkOvrAls, &QCheckBox::toggled, this, &MainWindow::updateAls);
    spinLux = new QSpinBox();
    spinLux->setRange(0, 10000);
    connect(spinLux, &QSpinBox::valueChanged, this, &MainWindow::updateAls);
    lAls->addWidget(chkOvrAls); lAls->addWidget(spinLux);
    vOvr->addLayout(lAls);

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
    QPushButton *btnAt = new QPushButton("▶ START AUTO-TEST");
    btnAt->setObjectName("BtnPurple");
    connect(btnAt, &QPushButton::clicked, [this](){ enqueueToActive(Command(CmdType::AUTO_TEST)); });
    vAt->addWidget(btnAt);
    lblPassFail = new QLabel("READY");
    lblPassFail->setAlignment(Qt::AlignCenter);
    lblPassFail->setStyleSheet("font-size: 20pt; font-weight: bold; background-color: #6C757D; color: white; border-radius: 4px; min-height: 45px;");
    vAt->addWidget(lblPassFail);
    col3->addWidget(grpAt);
    col3->addStretch();

    hCols->addLayout(col1, 1);
    hCols->addLayout(col2, 1);
    hCols->addLayout(col3, 1);
    vTestMain->addLayout(hCols, 1);

    // =============== 曲线图 ===============
    chart = new QChart();
    chart->legend()->show();
    chart->legend()->setAlignment(Qt::AlignTop);
    chart->layout()->setContentsMargins(0, 0, 0, 0);

    seriesVBatt = new QLineSeries();
    seriesVBatt->setName("V_Batt (mV)");
    QPen penVBatt(Qt::red); penVBatt.setWidth(2);
    seriesVBatt->setPen(penVBatt);

    seriesBrt = new QLineSeries();
    seriesBrt->setName("Lux");
    QPen penLux(Qt::blue); penLux.setWidth(2);
    seriesBrt->setPen(penLux);

    chart->addSeries(seriesVBatt);
    chart->addSeries(seriesBrt);

    axisX = new QValueAxis(); axisX->setRange(0, 100); axisX->setLabelFormat("%d");
    axisY1 = new QValueAxis(); axisY1->setRange(2000, 4200); axisY1->setLinePenColor(Qt::red); axisY1->setLabelsColor(Qt::red);
    axisY2 = new QValueAxis(); axisY2->setRange(0, 10000); axisY2->setLinePenColor(Qt::blue); axisY2->setLabelsColor(Qt::blue);

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY1, Qt::AlignLeft);
    chart->addAxis(axisY2, Qt::AlignRight);
    seriesVBatt->attachAxis(axisX); seriesVBatt->attachAxis(axisY1);
    seriesBrt->attachAxis(axisX); seriesBrt->attachAxis(axisY2);

    QChartView *chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setStyleSheet("background: transparent; border: 1px solid #CCC; border-radius: 4px;");
    chartView->setMinimumHeight(120);
    vTestMain->addWidget(chartView, 1);

    tabs->addTab(tabTest, "2. Test & Calibration Mode");
    mainSplitter->addWidget(tabs);

    // =============== 终端日志控制台 ===============
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

    btnDetachConsole = new QPushButton("⧉ Detach");
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
    mainSplitter->addWidget(consoleContainer);

    mainSplitter->setSizes({600, 200});
    mainLayout->addWidget(mainSplitter, 1);

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

    globalLayout->addLayout(mainLayout, 8);
    globalLayout->addWidget(grpLog, 2);
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

    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        QString desc = info.description().toUpper();
        if (desc.contains("XDS110") || desc.contains("CMSIS-DAP")) {
            uint32_t sn = info.serialNumber().toUInt();
            if (sn == 0) sn = qHash(info.serialNumber());

            if (sn != 0) {
                currentScanned.insert(sn);
                if (!activeWorkers.contains(sn)) {
                    addProbeToUI(sn, ProbeType::XDS110);
                    newlyAdded++;
                }
            }
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

void MainWindow::addProbeToUI(uint32_t sn, ProbeType type) {
    BaseWorker *w = nullptr;
    QString typeStr = "";

    if (type == ProbeType::JLINK) {
        w = new JLinkWorker(sn, this);
        typeStr = "J-Link";
        onLog(sn, "[SYS] Engine assigned: SEGGER J-Link DLL.");
    } else {
        int ocdPort = 6666 + activeWorkers.size();
        w = new OpenOcdWorker(sn, ocdPort, this);
        typeStr = "XDS110/DAP";
        onLog(sn, "[SYS] Engine assigned: OpenOCD TCP Server.");
    }

    w->fwPath = txtFwPath->text();
    w->setSpeed(cmbSpeed->currentText().remove(" kHz").toInt());

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
    connect(chkAuto, &QCheckBox::toggled, w, [w](bool checked){ w->autoFlashEnabled = checked; });
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
    for (auto le : monVars.values()) le->setText("-");
    seriesVBatt->clear(); seriesBrt->clear(); plotTime = 0;

    if (activeWorkers.contains(activeSn)) {
        lblVer->setText("FW: " + probeUuids.value(activeSn, "N/A"));
        txtUuid->setText(probeUuids.value(activeSn, ""));
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

void MainWindow::exportDxf() {
    QMessageBox::information(this, "Notice", "C++ DXF generation requires external libdmtx library.");
}

void MainWindow::onStatus(uint32_t sn, int code, const QString& msg) {
    int row = probeRowMap.value(sn, -1);
    if (row >= 0) {
        QTableWidgetItem *item = probeTable->item(row, 2);
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
    txtGlobalLog->moveCursor(QTextCursor::End);
}

void MainWindow::onMsg(uint32_t sn, const QString& title, const QString& text) {
    if(title == "Error") QMessageBox::critical(this, title, QString("[SN: %1] ").arg(sn) + text);
    else QMessageBox::information(this, title, QString("[SN: %1] ").arg(sn) + text);
}

void MainWindow::onAutoTestRes(uint32_t sn, bool success, const QString& msg) {
    if (sn != cmbActiveProbe->currentData().toUInt()) return;
    lblPassFail->setText(success ? "PASS" : "FAIL");
    lblPassFail->setStyleSheet(success ? "background-color: #198754; color: white; font-size: 20pt; font-weight: bold; border-radius: 4px;"
                                       : "background-color: #DC3545; color: white; font-size: 20pt; font-weight: bold; border-radius: 4px;");
    if(!success) QMessageBox::critical(this, "Auto Test Failed", msg);
}

void MainWindow::onConfigRead(uint32_t sn, const QVariantMap& cfg) {
    if (sn != cmbActiveProbe->currentData().toUInt()) return;
    uint32_t feat = cfg["feat"].toUInt();
    for(auto it = featureCheckboxes.begin(); it != featureCheckboxes.end(); ++it) {
        it.value()->setChecked(feat & (1 << it.key()));
    }
    for(auto key : cfgVars.keys()) {
        if(cfg.contains(key)) cfgVars[key]->setValue(cfg[key].toUInt());
    }
    onLog(sn, "[INFO] UI configuration fully synchronized with device.");
}

void MainWindow::onTelemetry(uint32_t sn, const QVariantMap& data) {
    if (sn != cmbActiveProbe->currentData().toUInt()) return;

    QStringList states = {"OFF", "Unlocking", "Running", "LVP Crit", "FlashMode", "ALS Err", "TestMode"};
    QStringList sns = {"OK", "Sleep/Dis", "I2C Err"};
    int st = data["state"].toInt();
    int sen = data["sensor"].toInt();

    monVars["state"]->setText(st < states.size() ? states[st] : "Unk");
    monVars["vbatt"]->setText(data["vbatt"].toString());
    monVars["dyn_r"]->setText(QString::number(data["dyn_r"].toInt() / 1000.0, 'f', 1));
    monVars["level"]->setText("Gear " + data["level"].toString());

    uint32_t lv = data["l_v_drop"].toUInt(); monVars["l_v_drop"]->setText(lv <= 1000 ? QString::number(lv) : "-");
    uint32_t li = data["l_i_brt"].toUInt(); monVars["l_i_brt"]->setText(li <= 1000 ? QString::number(li) : "-");
    uint32_t lp = data["l_p_avg"].toUInt(); monVars["l_p_avg"]->setText(lp <= 1000 ? QString::number(lp) : "-");
    uint32_t lled = data["l_i_led"].toUInt(); monVars["l_i_led"]->setText(lled <= 1000 ? QString::number(lled) : "-");

    monVars["v_led"]->setText(data["v_led"].toString());
    monVars["brt"]->setText(data["brt"].toString() + " (Max:" + data["safe_brt"].toString() + ")");
    monVars["pwm"]->setText(data["pwm"].toString());

    monVars["p_led"]->setText(QString::number(data["p_led"].toInt() / 1000.0, 'f', 1));
    monVars["i_avg"]->setText(QString::number(data["i_avg"].toInt() / 1000.0, 'f', 1));
    monVars["i_peak"]->setText(QString::number(data["i_peak"].toInt() / 1000.0, 'f', 1));

    monVars["sensor"]->setText(sen < 3 ? sns[sen] : "Unk");
    monVars["err_cnt"]->setText(data["err_cnt"].toString());
    monVars["lux"]->setText(data["lux"].toString());
    monVars["lux_raw"]->setText(data["lux_raw"].toString());

    monVars["raw_k_m"]->setText(data["raw_k_m"].toInt() ? "Released (1)" : "PRESSED (0)");
    monVars["raw_k_p"]->setText(data["raw_k_p"].toInt() ? "Released (1)" : "PRESSED (0)");

    QString flags = "";
    if (data["f_dim"].toInt()) flags += "[DIMMED] ";
    if (data["f_ovr"].toInt()) flags += "[OVR_LMT] ";
    if (data["f_dbg"].toInt()) flags += "[SWD_ON] ";
    if (data["f_dirty"].toInt()) flags += "[NVM_DIRTY] ";
    monVars["flags"]->setText(flags.isEmpty() ? "None" : flags.trimmed());

    plotTime++;
    seriesVBatt->append(plotTime, data["vbatt"].toUInt());
    seriesBrt->append(plotTime, data["lux"].toUInt());
    if(plotTime > 100) axisX->setRange(plotTime - 100, plotTime);
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
    int chars = (size == 4) ? 8 : (size == 2) ? 4 : 2;
    txtMemVal->setText(QString("0x%1").arg(val, chars, 16, QChar('0')).toUpper());
}