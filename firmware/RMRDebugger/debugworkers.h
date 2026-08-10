#pragma once
#include <QThread>
#include <QLibrary>
#include <QMutex>
#include <queue>
#include <QVariantMap>
#include <QProcess>
#include <QTcpSocket>

constexpr uint32_t BASE_ADDR = 0x20000000;
#define OFS_VBATT_RAW_MV        0xB0
constexpr uint32_t ADDR_UUID = 0x41C40010;

// 銆愭牳蹇冩洿鏂般€戯細涓ユ牸瀵归綈鏂扮増 Test_Mailbox_t 鐨勫唴瀛樼粨鏋?
#define OFS_MAGIC               0x00
#define OFS_VERSION             0x04
#define OFS_HOST_VERSION        0x06
#define OFS_CMD                 0x08
#define OFS_CMD_ACK             0x0C
#define OFS_STATUS              0x10
#define OFS_VBATT_MV            0x14
#define OFS_EST_I_PEAK          0x18
#define OFS_EST_I_AVG           0x1C
#define OFS_EST_V_LED           0x20
#define OFS_EST_P_LED           0x24
#define OFS_DYN_R_MOHM          0x28
#define OFS_CURRENT_LVL         0x2C
#define OFS_CURRENT_BRT         0x2E
#define OFS_CURRENT_PWM         0x30
#define OFS_SAFE_BRT            0x32
#define OFS_LIMIT_I_LED         0x34
#define OFS_LIMIT_V_DROP        0x38
#define OFS_LIMIT_I_BRT         0x3C
#define OFS_LIMIT_P_AVG         0x40
#define OFS_ALS_RAW             0x44
#define OFS_ALS_FILT            0x48
#define OFS_ALS_ERR_CNT         0x4C
#define OFS_SENSOR_STATUS       0x4D
#define OFS_SYS_STATE           0x4E
#define OFS_RAW_KEY_MINUS       0x4F
#define OFS_RAW_KEY_PLUS        0x50
#define OFS_STATE_DIMMED        0x51
#define OFS_STATE_OVERSHOT      0x52
#define OFS_STATE_DEBUG         0x53
#define OFS_NVM_DIRTY           0x54
#define OFS_NVM_SAVE_FAIL_CNT   0x55
#define OFS_OVR_BLOCK_PHYS_KEYS 0x56
// 0x57 (Padding)
#define OFS_INACTIVITY_SEC      0x58
#define OFS_NVM_SEQ_ID          0x5C
#define OFS_NVM_SECTOR          0x60
#define OFS_NVM_SLOT            0x64
#define OFS_OVR_LED_MODE        0x68
#define OFS_OVR_KEY_MINUS       0x69
#define OFS_OVR_KEY_PLUS        0x6A
#define OFS_OVR_ALS_EN          0x6B
#define OFS_OVR_BRT_VAL         0x6C
#define OFS_OVR_PWM_VAL         0x6E
#define OFS_OVR_ALS_LUX         0x70
#define OFS_CFG_PARAMS          0x74
#define OFS_CFG_FEATURES        0x78
#define OFS_CFG_R_BASE          0x7C
#define OFS_CFG_R_SERIES        0x80
#define OFS_CFG_V_LED_FW        0x84
#define OFS_CFG_I_MAX_UA        0x88
#define OFS_CFG_BATT_P_UW       0x8C
#define OFS_CFG_ALS_MIN_BRT     0x90
#define OFS_CFG_LVP_CRIT        0x94
#define OFS_CFG_LVP_EXT         0x98
#define OFS_CFG_ALS_SQRT        0x9C
#define OFS_CFG_ALS_CAP_LOW     0x9D
#define OFS_CFG_ALS_CAP_HIGH    0x9E
#define OFS_FW_VER_STR          0xA0

enum class CmdType { FLASH, ENTER_TEST, WRITE_8, WRITE_16, WRITE_32, READ_CFG, SEND_SYS_CMD, AUTO_CALIBRATE, AUTO_TEST, READ_MEM_ABS, WRITE_MEM_ABS };

struct Command {
    CmdType type; uint32_t arg1; uint32_t arg2; QString strArg; QVariantMap mapArg;
    Command(CmdType t, uint32_t a1=0, uint32_t a2=0, const QString& s="", const QVariantMap& m=QVariantMap())
        : type(t), arg1(a1), arg2(a2), strArg(s), mapArg(m) {}
    Command() : type(CmdType::READ_CFG), arg1(0), arg2(0) {}
};

enum class ProbeType { JLINK, XDS110 };

class BaseWorker : public QThread {
    Q_OBJECT
public:
    explicit BaseWorker(uint32_t sn, ProbeType type, QObject *parent = nullptr)
        : QThread(parent), probeSN(sn), probeType(type), running(true), wasConnected(false), targetFlashedThisSession(false), currentSpeedKHz(100), speedNeedsUpdate(false) {}
    virtual ~BaseWorker() { running = false; wait(); }

    virtual void enqueueCommand(const Command& cmd) { QMutexLocker lock(&queueMutex); cmdQueue.push(cmd); }
    virtual void stop() { running = false; }
    virtual void setSpeed(int khz) { currentSpeedKHz = khz; speedNeedsUpdate = true; }

    uint32_t probeSN;
    ProbeType probeType;
    bool autoFlashEnabled = false;
    QString fwPath;
    bool enablePolling = false;
    int pollIntervalMs = 150;   /* 轮询间隔(ms), J-Link/OpenOCD 可持续会话可达 10-50Hz */

signals:
    void sigStatus(uint32_t sn, int code, const QString& msg);
    void sigUuid(uint32_t sn, const QString& uuid);
    void sigFwVer(uint32_t sn, const QString& ver);
    void sigProgress(uint32_t sn, int percent, const QString& text);
    void sigLog(uint32_t sn, const QString& text);
    void sigMsg(uint32_t sn, const QString& title, const QString& text);
    void sigConfigRead(uint32_t sn, const QVariantMap& cfg);
    void sigTelemetry(uint32_t sn, const QVariantMap& data);
    void sigAutoTestRes(uint32_t sn, bool success, const QString& msg);
    void sigMemReadRes(uint32_t sn, uint32_t addr, uint32_t val, int size);

protected:
    bool running;
    bool wasConnected;
    bool targetFlashedThisSession;
    int currentSpeedKHz;
    bool speedNeedsUpdate;
    std::queue<Command> cmdQueue;
    QMutex queueMutex;

    void processCommandGeneric(const Command& cmd);
    void pollTelemetryGeneric();
    QString readUuidGeneric();
    QString readFwVersionGeneric();
    bool syncSysCmdGeneric(uint32_t sys_cmd);

    virtual bool checkTargetConnected() = 0;
    virtual void write32(uint32_t ofs, uint32_t val) = 0;
    virtual void write16(uint32_t ofs, uint16_t val) = 0;
    virtual void write8(uint32_t ofs, uint8_t val) = 0;
    virtual uint32_t read32(uint32_t ofs) = 0;
    virtual uint16_t read16(uint32_t ofs) = 0;
    virtual uint8_t read8(uint32_t ofs) = 0;
    virtual void readBlock(uint32_t ofs, uint32_t byteCount, uint8_t* outData) = 0;
    virtual void executeFlash(const QString& path) = 0;
    virtual void triggerReset() = 0;

    virtual void writeAbs(uint32_t addr, uint32_t val, int size) = 0;
    virtual uint32_t readAbs(uint32_t addr, int size) = 0;
};

class JLinkWorker : public BaseWorker {
    Q_OBJECT
public:
    explicit JLinkWorker(uint32_t sn, QObject *parent = nullptr);
protected:
    void run() override;
    bool checkTargetConnected() override;
    void write32(uint32_t ofs, uint32_t val) override;
    void write16(uint32_t ofs, uint16_t val) override;
    void write8(uint32_t ofs, uint8_t val) override;
    uint32_t read32(uint32_t ofs) override;
    uint16_t read16(uint32_t ofs) override;
    uint8_t read8(uint32_t ofs) override;
    void readBlock(uint32_t ofs, uint32_t byteCount, uint8_t* outData) override;
    void executeFlash(const QString& path) override;
    void triggerReset() override;

    void writeAbs(uint32_t addr, uint32_t val, int size) override;
    uint32_t readAbs(uint32_t addr, int size) override;
private:
    typedef const char* (*JLINK_OpenFunc)(void);
    typedef void (*JLINK_EMU_SelectByUSBSNFunc)(uint32_t);
    typedef void (*JLINK_CloseFunc)(void);
    typedef int (*JLINK_ConnectFunc)(void);
    typedef int (*JLINK_IsConnectedFunc)(void);
    typedef void (*JLINK_ExecCommandFunc)(const char*, char*, int);
    typedef int (*JLINK_ReadMemFunc)(uint32_t, uint32_t, void*);
    typedef int (*JLINK_WriteMemFunc)(uint32_t, uint32_t, const void*);
    typedef void (*JLINK_ResetFunc)(void);
    typedef void (*JLINK_GoFunc)(void);

    QLibrary jlinkLib;
    JLINK_OpenFunc jlinkOpen;
    JLINK_EMU_SelectByUSBSNFunc jlinkSelectBySN;
    JLINK_CloseFunc jlinkClose;
    JLINK_ConnectFunc jlinkConnect;
    JLINK_IsConnectedFunc jlinkIsConnected;
    JLINK_ExecCommandFunc jlinkExec;
    JLINK_ReadMemFunc jlinkRead;
    JLINK_WriteMemFunc jlinkWrite;
    JLINK_ResetFunc jlinkReset;
    JLINK_GoFunc jlinkGo;
    bool initJLink();
};

class OpenOcdWorker : public BaseWorker {
    Q_OBJECT
public:
    explicit OpenOcdWorker(uint32_t sn, int port, bool useXds110Adapter, const QString& openocdPath, const QString& openocdScripts, QObject *parent = nullptr);
    ~OpenOcdWorker() override;
protected:
    bool useXds110 = false;
    QString ocdBinPath;
    QString ocdScriptsPath;
    void run() override;
    bool checkTargetConnected() override;
    void write32(uint32_t ofs, uint32_t val) override;
    void write16(uint32_t ofs, uint16_t val) override;
    void write8(uint32_t ofs, uint8_t val) override;
    uint32_t read32(uint32_t ofs) override;
    uint16_t read16(uint32_t ofs) override;
    uint8_t read8(uint32_t ofs) override;
    void readBlock(uint32_t ofs, uint32_t byteCount, uint8_t* outData) override;
    void executeFlash(const QString& path) override;
    void triggerReset() override;

    void writeAbs(uint32_t addr, uint32_t val, int size) override;
    uint32_t readAbs(uint32_t addr, int size) override;
private:
    QProcess *ocdProcess;
    QTcpSocket *tclSocket;
    int tclPort;
    QString ocdLogBuffer;

    QString sendTclCommand(const QString& cmd, int timeoutMs = 1000, bool muteLog = false);
    void pumpOpenOCDLogs(bool muteLog = false);
    void startOpenOCD();
    void stopOpenOCD();
};
