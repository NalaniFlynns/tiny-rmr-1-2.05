#include "DebugWorkers.h"
#include <QDebug>
#include <QFile>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QDir>
#include <exception>
#include <stdexcept>
#include <QElapsedTimer>

static QMutex g_jlinkInitMutex;

extern void GlobalJLinkLogHandler(const char* s);
typedef void (*JLinkLogCB)(const char*);
typedef void (*JLINK_SetLogFunc)(JLinkLogCB);

void BaseWorker::processCommandGeneric(const Command& cmd) {
    try {
        if (cmd.type == CmdType::FLASH) {
            emit sigProgress(probeSN, 5, "Preparing Flashing...");
            emit sigLog(probeSN, QString("[INFO] Sending firmware flash command at %1 kHz...").arg(currentSpeedKHz));

            executeFlash(cmd.strArg);

            emit sigProgress(probeSN, 100, "Flash Complete");
            emit sigLog(probeSN, "[SUCCESS] Firmware flashed and MCU restarted successfully.");
        }
        else if (cmd.type == CmdType::ENTER_TEST) {
            emit sigLog(probeSN, "[INFO] Unlocking Test Mode...");
            write32(OFS_MAGIC, 0x54455354);
            write16(OFS_HOST_VERSION, 0x0100);
            msleep(100);

            uint8_t strBuf[16] = {0};
            readBlock(OFS_FW_VER_STR, 16, strBuf);
            QString realVer = QString::fromUtf8((char*)strBuf).trimmed();

            if (realVer.isEmpty() || !realVer.startsWith("V", Qt::CaseInsensitive)) {
                uint16_t ver = read16(OFS_VERSION);
                realVer = QString("V%1.%2").arg(ver >> 8).arg(ver & 0xFF);
            }
            emit sigFwVer(probeSN, realVer);
            emit sigLog(probeSN, QString("[INFO] Test mode unlocked. FW Version: %1").arg(realVer));
        }
        else if (cmd.type == CmdType::WRITE_8) write8(cmd.arg1, cmd.arg2);
        else if (cmd.type == CmdType::WRITE_16) write16(cmd.arg1, cmd.arg2);
        else if (cmd.type == CmdType::WRITE_32) write32(cmd.arg1, cmd.arg2);
        else if (cmd.type == CmdType::READ_CFG) {
            uint8_t data[44] = {0};
            readBlock(OFS_CFG_PARAMS, 44, data);
            QVariantMap cfg;
            cfg["def_lvl"] = *(uint32_t*)(data + 0) & 0xFF;
            cfg["als_offset"] = (*(uint32_t*)(data + 0) >> 16) & 0xFF;
            cfg["feat"] = *(uint32_t*)(data + 4);
            cfg["r_base"] = *(uint32_t*)(data + 8);
            cfg["r_series"] = *(uint32_t*)(data + 12);
            cfg["v_fw"] = *(uint32_t*)(data + 16);
            cfg["i_max"] = *(uint32_t*)(data + 20);
            cfg["p_batt"] = *(uint32_t*)(data + 24);
            cfg["als_min"] = *(uint32_t*)(data + 28);
            cfg["lvp_crit"] = *(uint32_t*)(data + 32);
            cfg["lvp_ext"] = *(uint32_t*)(data + 36);
            cfg["als_sqrt"] = data[40];
            cfg["als_cap_low"] = data[41];
            cfg["als_cap_high"] = data[42];
            emit sigConfigRead(probeSN, cfg);
        }
        else if (cmd.type == CmdType::SEND_SYS_CMD) {
            uint32_t sys_cmd = cmd.arg1;
            if (sys_cmd == 3) {
                QVariantMap cfg = cmd.mapArg;
                if (cfg.isEmpty()) {
                    emit sigLog(probeSN, "[WARN] syscmd 3 empty map: keeping current config");
                } else {
                    /* 只写 map 中出现的字段, 空字段绝不写 0, 避免误清 NVM */
                    auto w32 = [&](uint32_t ofs, const char* key){
                        if (cfg.contains(key)) write32(ofs, cfg[key].toUInt());
                    };
                    auto w8 = [&](uint32_t ofs, const char* key){
                        if (cfg.contains(key)) write8(ofs, (uint8_t)(cfg[key].toUInt() & 0xFF));
                    };
                    w32(OFS_CFG_FEATURES, "feat");
                    w32(OFS_CFG_R_BASE, "r_base");
                    w32(OFS_CFG_R_SERIES, "r_series");
                    w32(OFS_CFG_V_LED_FW, "v_fw");
                    w32(OFS_CFG_I_MAX_UA, "i_max");
                    w32(OFS_CFG_BATT_P_UW, "p_batt");
                    w32(OFS_CFG_ALS_MIN_BRT, "als_min");
                    w32(OFS_CFG_LVP_CRIT, "lvp_crit");
                    w32(OFS_CFG_LVP_EXT, "lvp_ext");
                    w8(OFS_CFG_ALS_SQRT, "als_sqrt");
                    w8(OFS_CFG_ALS_CAP_LOW, "als_cap_low");
                    w8(OFS_CFG_ALS_CAP_HIGH, "als_cap_high");
                    /* cfg_params: 只改提供的位段, 保留其它高位(ALS bit8/offset bit16-23) */
                    if (cfg.contains("def_lvl") || cfg.contains("als_offset")) {
                        uint32_t current_params = read32(OFS_CFG_PARAMS);
                        if (cfg.contains("def_lvl"))
                            current_params = (current_params & 0xFFFFFF00) | (cfg["def_lvl"].toUInt() & 0xFF);
                        if (cfg.contains("als_offset")) {
                            uint32_t off = cfg["als_offset"].toUInt() & 0xFF;
                            if (off > 4) off = 2;   /* 5 档偏移 0..4, 非法回中档 */
                            current_params = (current_params & 0xFF00FFFF) | (off << 16);
                        }
                        write32(OFS_CFG_PARAMS, current_params);
                    }
                }
            }
            write32(OFS_CMD, sys_cmd);
            if(sys_cmd == 2) {
                emit sigLog(probeSN, "[INFO] Soft Reset Issued.");
                return;
            }
            if(syncSysCmdGeneric(sys_cmd)) emit sigLog(probeSN, QString("[INFO] System Command %1 Executed OK.").arg(sys_cmd));
            else emit sigMsg(probeSN, "Error", "Command Failed.");
        }
        else if (cmd.type == CmdType::AUTO_CALIBRATE) {
            emit sigLog(probeSN, "[INFO] Starting R_Base Auto Calibration...");
            emit sigProgress(probeSN, 10, "Reading Idle V_Batt...");
            msleep(200);
            uint32_t v1 = read32(OFS_VBATT_MV);

            emit sigProgress(probeSN, 40, "Forcing PWM Load...");
            write8(OFS_OVR_LED_MODE, 1);
            write16(OFS_OVR_PWM_VAL, 2400);
            msleep(1500);

            uint32_t v2 = read32(OFS_VBATT_MV);
            uint32_t i_avg = read32(OFS_EST_I_AVG);
            write8(OFS_OVR_LED_MODE, 0);

            if (v1 - v2 < 20) {
                emit sigLog(probeSN, "[ERROR] Calibration Failed! Voltage drop < 20mV.");
                emit sigMsg(probeSN, "Error", "Calibration Failed!\nVoltage drop < 20mV.\nAre you powering via Probe VCC?\nPlease use real battery.");
            } else {
                uint32_t r_base = (v1 - v2) * 1000000 / i_avg;
                r_base = std::max(10u, std::min(r_base, 1000u));
                write32(OFS_CFG_R_BASE, r_base);
                write32(OFS_CMD, 3); syncSysCmdGeneric(3);
                write32(OFS_CMD, 4); syncSysCmdGeneric(4);
                emit sigProgress(probeSN, 100, QString("Calibrated R = %1 mOhm").arg(r_base));
                emit sigLog(probeSN, QString("[INFO] Success! New R_Base: %1 mOhm saved to NVM.").arg(r_base));
            }
        }
        else if (cmd.type == CmdType::AUTO_TEST) {
            emit sigLog(probeSN, "[INFO] Starting Full Auto-Test Sequence...");
            emit sigProgress(probeSN, 10, "Factory Resetting...");
            write32(OFS_CMD, 5); syncSysCmdGeneric(5);
            msleep(500);

            emit sigProgress(probeSN, 30, "Testing PWM Load...");
            write8(OFS_OVR_LED_MODE, 1);
            write16(OFS_OVR_PWM_VAL, 1200);
            msleep(1000);
            uint32_t i_avg = read32(OFS_EST_I_AVG);
            if (i_avg < 1000) throw std::runtime_error("Load Fail. I_Avg too low.");

            emit sigProgress(probeSN, 50, "Testing ALS (Dark)...");
            write8(OFS_OVR_LED_MODE, 0);
            write8(OFS_OVR_ALS_EN, 1);
            write32(OFS_OVR_ALS_LUX, 0);
            msleep(1500);
            uint16_t pwm_dark = read16(OFS_CURRENT_PWM);

            emit sigProgress(probeSN, 70, "Testing ALS (Bright)...");
            /* ALS 曲线最低为 ALS_MIN_BRT(50), lux<=10000 时亮度被钳位在 50,
             * 必须注入足够大光照才能超出最低值:
             * lux=50000 -> lux_int=500, base=5*22=110 > 50, 可区分 dark/bright */
            write32(OFS_OVR_ALS_LUX, 50000);
            msleep(2000);
            uint16_t pwm_bright = read16(OFS_CURRENT_PWM);

            /* PWM 寄存器值越小越亮: 亮环境下 brt 更大 -> pwm 应更小 */
            if (pwm_bright >= pwm_dark) throw std::runtime_error("ALS Response Fail. Bright PWM should be < Dark PWM.");

            write8(OFS_OVR_ALS_EN, 0);
            emit sigProgress(probeSN, 100, "Pass");
            emit sigLog(probeSN, "[INFO] SEQUENCE PASS.");
            emit sigAutoTestRes(probeSN, true, "All Factory Tests Passed!");
        }
        else if (cmd.type == CmdType::WRITE_MEM_ABS) {
            int size = cmd.mapArg["size"].toInt();
            writeAbs(cmd.arg1, cmd.arg2, size);
            QString valStr = QString("%1").arg(cmd.arg2, size * 2, 16, QChar('0')).toUpper();
            QString addrStr = QString("%1").arg(cmd.arg1, 8, 16, QChar('0')).toUpper();
            emit sigLog(probeSN, QString("[DMA] Wrote 0x%1 to Absolute Addr 0x%2").arg(valStr).arg(addrStr));
        }
        else if (cmd.type == CmdType::READ_MEM_ABS) {
            int size = cmd.mapArg["size"].toInt();
            uint32_t val = readAbs(cmd.arg1, size);
            QString valStr = QString("%1").arg(val, size * 2, 16, QChar('0')).toUpper();
            QString addrStr = QString("%1").arg(cmd.arg1, 8, 16, QChar('0')).toUpper();
            emit sigLog(probeSN, QString("[DMA] Read 0x%1 from Absolute Addr 0x%2").arg(valStr).arg(addrStr));
            emit sigMemReadRes(probeSN, cmd.arg1, val, size);
        }
    } catch (std::exception& e) {
        write8(OFS_OVR_LED_MODE, 0);
        emit sigLog(probeSN, QString("[ERROR] Action Failed: %1").arg(e.what()));

        if (cmd.type == CmdType::FLASH) emit sigProgress(probeSN, 0, "Flash Failed");
        else if (cmd.type == CmdType::AUTO_TEST) emit sigAutoTestRes(probeSN, false, e.what());
        else emit sigMsg(probeSN, "Error", e.what());
    }
}

bool BaseWorker::syncSysCmdGeneric(uint32_t sys_cmd) {
    int timeout = 20;
    while(timeout--) { msleep(100); if (read32(OFS_CMD_ACK) == sys_cmd) return read32(OFS_STATUS) == 2; }
    return false;
}

QString BaseWorker::readUuidGeneric() {
    uint8_t data[16] = {0};
    readBlock(ADDR_UUID - BASE_ADDR, 16, data);
    QString uuid; for(int i=15; i>=0; --i) uuid += QString("%1").arg(data[i], 2, 16, QChar('0')).toUpper();
    return uuid;
}

QString BaseWorker::readFwVersionGeneric() {
    uint8_t strBuf[16] = {0};
    readBlock(OFS_FW_VER_STR, 16, strBuf);
    QString ver = QString::fromUtf8((char*)strBuf).trimmed();
    if (ver.isEmpty()) { uint16_t v = read16(OFS_VERSION); ver = QString("V%1.%2").arg(v >> 8).arg(v & 0xFF); }
    return ver;
}
void BaseWorker::pollTelemetryGeneric() {
    uint8_t data[0xB8] = {0};
    readBlock(0, 0xB8, data); QVariantMap map;
    map["magic"] = *(uint32_t*)(data + OFS_MAGIC);
    map["cmd"] = *(uint32_t*)(data + OFS_CMD);
    map["cmd_ack"] = *(uint32_t*)(data + OFS_CMD_ACK);
    map["status"] = *(uint32_t*)(data + OFS_STATUS);
    map["vbatt"] = *(uint32_t*)(data + OFS_VBATT_MV);
    map["vbatt_raw"] = *(uint32_t*)(data + OFS_VBATT_RAW_MV);
    map["p_hw"] = *(uint32_t*)(data + OFS_HW_POWER_UW);
    map["dyn_r"] = *(uint32_t*)(data + OFS_DYN_R_MOHM);
    map["level"] = *(uint16_t*)(data + OFS_CURRENT_LVL);
    map["brt"] = *(uint16_t*)(data + OFS_CURRENT_BRT);
    map["safe_brt"] = *(uint16_t*)(data + OFS_SAFE_BRT);
    map["v_led"] = *(uint32_t*)(data + OFS_EST_V_LED);
    map["l_v_drop"] = *(uint32_t*)(data + OFS_LIMIT_V_DROP);
    map["l_i_brt"] = *(uint32_t*)(data + OFS_LIMIT_I_BRT);
    map["l_p_avg"] = *(uint32_t*)(data + OFS_LIMIT_P_AVG);
    map["l_i_led"] = *(uint32_t*)(data + OFS_LIMIT_I_LED);
    map["p_led"] = *(uint32_t*)(data + OFS_EST_P_LED);
    map["i_avg"] = *(uint32_t*)(data + OFS_EST_I_AVG);
    map["i_peak"] = *(uint32_t*)(data + OFS_EST_I_PEAK);
    map["pwm"] = *(uint16_t*)(data + OFS_CURRENT_PWM);
    map["sensor"] = data[OFS_SENSOR_STATUS];
    map["err_cnt"] = data[OFS_ALS_ERR_CNT];
    map["lux"] = *(uint32_t*)(data + OFS_ALS_FILT);
    map["lux_raw"] = *(uint32_t*)(data + OFS_ALS_RAW);
    map["state"] = data[OFS_SYS_STATE];
    map["raw_k_m"] = data[OFS_RAW_KEY_MINUS];
    map["raw_k_p"] = data[OFS_RAW_KEY_PLUS];
    map["f_dim"] = data[OFS_STATE_DIMMED];
    map["f_ovr"] = data[OFS_STATE_OVERSHOT];
    map["f_dbg"] = data[OFS_STATE_DEBUG];
    map["f_dirty"] = data[OFS_NVM_DIRTY];
    map["nvm_fail"] = data[OFS_NVM_SAVE_FAIL_CNT];
    map["inactivity"] = *(uint32_t*)(data + OFS_INACTIVITY_SEC);
    map["nvm_seq"] = *(uint32_t*)(data + OFS_NVM_SEQ_ID);
    map["nvm_sector"] = *(uint32_t*)(data + OFS_NVM_SECTOR);
    map["nvm_slot"] = *(uint32_t*)(data + OFS_NVM_SLOT);
    map["ovr_led_mode"] = data[OFS_OVR_LED_MODE];
    map["ovr_k_m"] = data[OFS_OVR_KEY_MINUS];
    map["ovr_k_p"] = data[OFS_OVR_KEY_PLUS];
    map["ovr_als_en"] = data[OFS_OVR_ALS_EN];
    map["ovr_brt"] = *(uint16_t*)(data + OFS_OVR_BRT_VAL);
    map["ovr_pwm"] = *(uint16_t*)(data + OFS_OVR_PWM_VAL);
    map["ovr_lux"] = *(uint32_t*)(data + OFS_OVR_ALS_LUX);
    map["cfg_params"] = *(uint32_t*)(data + OFS_CFG_PARAMS);
    map["fw_ver"] = QString::fromUtf8((const char*)(data + OFS_FW_VER_STR)).trimmed();
    emit sigTelemetry(probeSN, map);
}
JLinkWorker::JLinkWorker(uint32_t sn, QObject *parent) : BaseWorker(sn, ProbeType::JLINK, parent) {}
bool JLinkWorker::initJLink() {
    QString baseDll = "JLink_x64.dll"; QString dllName = QString("JLink_x64_%1.dll").arg(probeSN);
    if (!QFile::exists(dllName)) QFile::copy(baseDll, dllName);
    jlinkLib.setFileName(dllName);
    if (!jlinkLib.load()) { jlinkLib.setFileName(baseDll); if (!jlinkLib.load()) return false; }
    auto setLog = (JLINK_SetLogFunc)jlinkLib.resolve("JLINKARM_SetLogHandler");
    auto setWarn = (JLINK_SetLogFunc)jlinkLib.resolve("JLINKARM_SetWarnOutHandler");
    auto setErr = (JLINK_SetLogFunc)jlinkLib.resolve("JLINKARM_SetErrorOutHandler");
    if (setLog) setLog(GlobalJLinkLogHandler);
    if (setWarn) setWarn(GlobalJLinkLogHandler);
    if (setErr) setErr(GlobalJLinkLogHandler);
    jlinkOpen = (JLINK_OpenFunc)jlinkLib.resolve("JLINKARM_Open");
    jlinkSelectBySN = (JLINK_EMU_SelectByUSBSNFunc)jlinkLib.resolve("JLINKARM_EMU_SelectByUSBSN");
    jlinkClose = (JLINK_CloseFunc)jlinkLib.resolve("JLINKARM_Close");
    jlinkConnect = (JLINK_ConnectFunc)jlinkLib.resolve("JLINKARM_Connect");
    jlinkIsConnected = (JLINK_IsConnectedFunc)jlinkLib.resolve("JLINKARM_IsOpen");
    jlinkExec = (JLINK_ExecCommandFunc)jlinkLib.resolve("JLINKARM_ExecCommand");
    jlinkRead = (JLINK_ReadMemFunc)jlinkLib.resolve("JLINKARM_ReadMem");
    jlinkWrite = (JLINK_WriteMemFunc)jlinkLib.resolve("JLINKARM_WriteMem");
    jlinkReset = (JLINK_ResetFunc)jlinkLib.resolve("JLINKARM_Reset");
    jlinkGo = (JLINK_GoFunc)jlinkLib.resolve("JLINKARM_Go");
    return jlinkOpen && jlinkExec;
}
bool JLinkWorker::checkTargetConnected() { return jlinkConnect() >= 0; }
void JLinkWorker::run() {
    if (!initJLink()) { emit sigStatus(probeSN, 0, "DLL Error!"); return; }
    while (running) {
        if (!jlinkIsConnected()) {
            if (probeSN != 0 && jlinkSelectBySN) jlinkSelectBySN(probeSN);
            const char* err = jlinkOpen();
            bool openSuccess = (err == nullptr || QString(err).isEmpty()) && jlinkIsConnected();
            if (openSuccess) {
                jlinkExec("SetBatchMode = 1", nullptr, 0); jlinkExec("Device = MSPM0C1104", nullptr, 0);
                jlinkExec("SelectInterface = SWD", nullptr, 0); jlinkExec(QString("Speed = %1").arg(currentSpeedKHz).toLocal8Bit().constData(), nullptr, 0);
                speedNeedsUpdate = false;
            }
            if (!openSuccess) { msleep(1000); continue; }
        }
        bool targetConnected = checkTargetConnected();
        if (targetConnected) {
            if (!wasConnected) {
                wasConnected = true; emit sigStatus(probeSN, 1, "Target Connected"); emit sigUuid(probeSN, readUuidGeneric());
                emit sigLog(probeSN, "[SYS] J-Link successfully connected to Target MCU.");
                if (autoFlashEnabled && !fwPath.isEmpty() && !targetFlashedThisSession) {
                    targetFlashedThisSession = true; enqueueCommand(Command(CmdType::FLASH, 0, 0, fwPath));
                }
            }
            if (speedNeedsUpdate) { jlinkExec(QString("Speed = %1").arg(currentSpeedKHz).toLocal8Bit().constData(), nullptr, 0); speedNeedsUpdate = false; }
        } else {
            if (wasConnected) { wasConnected = false; targetFlashedThisSession = false; emit sigStatus(probeSN, 0, "Awaiting Target..."); emit sigUuid(probeSN, "-"); emit sigFwVer(probeSN, "N/A"); }
        }
        Command cmd; bool hasCmd = false;
        { QMutexLocker lock(&queueMutex); if (!cmdQueue.empty()) { cmd = cmdQueue.front(); cmdQueue.pop(); hasCmd = true; } }
        if (hasCmd) processCommandGeneric(cmd);
        else if (targetConnected && enablePolling) { pollTelemetryGeneric(); msleep(qMax(20, pollIntervalMs)); }
        else msleep(100);
    }
    jlinkClose();
}
void JLinkWorker::write32(uint32_t ofs, uint32_t val) { jlinkWrite(BASE_ADDR + ofs, 4, &val); }
void JLinkWorker::write16(uint32_t ofs, uint16_t val) { jlinkWrite(BASE_ADDR + ofs, 2, &val); }
void JLinkWorker::write8(uint32_t ofs, uint8_t val) { jlinkWrite(BASE_ADDR + ofs, 1, &val); }
uint32_t JLinkWorker::read32(uint32_t ofs) { uint32_t val = 0; jlinkRead(BASE_ADDR + ofs, 4, &val); return val; }
uint16_t JLinkWorker::read16(uint32_t ofs) { uint16_t val = 0; jlinkRead(BASE_ADDR + ofs, 2, &val); return val; }
uint8_t JLinkWorker::read8(uint32_t ofs) { uint8_t val = 0; jlinkRead(BASE_ADDR + ofs, 1, &val); return val; }
void JLinkWorker::readBlock(uint32_t ofs, uint32_t byteCount, uint8_t* outData) { jlinkRead(BASE_ADDR + ofs, byteCount, outData); }
void JLinkWorker::writeAbs(uint32_t addr, uint32_t val, int size) { jlinkWrite(addr, size, &val); }
uint32_t JLinkWorker::readAbs(uint32_t addr, int size) { uint32_t val = 0; jlinkRead(addr, size, &val); return val; }
void JLinkWorker::executeFlash(const QString& path) {
    emit sigLog(probeSN, "[J-LINK] Erasing and Downloading...");
    jlinkExec(QString("Speed = %1").arg(currentSpeedKHz).toLocal8Bit().constData(), nullptr, 0);
    if (jlinkConnect() < 0) throw std::runtime_error("J-Link Native Error: Cannot connect to target MCU. Check wiring and power.");
    jlinkExec(QString("loadfile %1, 0x00000000").arg(path).toLocal8Bit().data(), nullptr, 0); jlinkReset(); jlinkGo();
}
void JLinkWorker::triggerReset() { jlinkReset(); jlinkGo(); }

OpenOcdWorker::OpenOcdWorker(uint32_t sn, int port, bool useXds110Adapter, const QString& openocdPath, const QString& openocdScripts, QObject *parent)
    : BaseWorker(sn, ProbeType::XDS110, parent), ocdProcess(nullptr), tclSocket(nullptr), tclPort(port) {
    useXds110 = useXds110Adapter;
    ocdBinPath = openocdPath;
    ocdScriptsPath = openocdScripts;
}
OpenOcdWorker::~OpenOcdWorker() { stopOpenOCD(); }
void OpenOcdWorker::startOpenOCD() {
    if (!ocdProcess) ocdProcess = new QProcess();
    if (ocdProcess->state() != QProcess::NotRunning) return;
    QString bin = ocdBinPath.isEmpty() ? "openocd.exe" : ocdBinPath;
    QStringList args;
    if (!ocdScriptsPath.isEmpty()) args << "-s" << ocdScriptsPath;
    if (useXds110) {
        /* TI XDS110: 原生 xds110 适配器驱动 (OpenOCD 0.12+ libusb) */
        args << "-f" << "interface/xds110.cfg";
        if (probeSN != 0 && probeSN != 0xFFFFFFFF && probeSN != 0x0451BEF3) args << "-c" << QString("adapter serial %1").arg(probeSN);  // 0x0451BEF3 为 VID+PID 合成SN(XDS110 NOSERIAL), 传了会连接失败
        args << "-f" << "target/ti_mspm0.cfg";
        args << "-c" << "transport select swd";
    } else {
        /* CMSIS-DAP / DAPLink 探针 */
        args << "-c" << "adapter driver cmsis-dap";
        if (probeSN != 0 && probeSN != 0xFFFFFFFF && probeSN != 0x0451BEF3) args << "-c" << QString("cmsis_dap_serial %1").arg(probeSN);
        args << "-c" << "transport select swd" << "-f" << "target/ti_mspm0.cfg";
    }
    args << "-c" << QString("adapter speed %1").arg(currentSpeedKHz)
         << "-c" << QString("tcl_port %1").arg(tclPort)
         << "-c" << "gdb_port disabled" << "-c" << "telnet_port disabled";
    ocdProcess->start(bin, args); ocdProcess->waitForStarted();
}
void OpenOcdWorker::stopOpenOCD() {
    if (tclSocket) { tclSocket->disconnectFromHost(); delete tclSocket; tclSocket = nullptr; }
    if (ocdProcess) { ocdProcess->kill(); ocdProcess->waitForFinished(); delete ocdProcess; ocdProcess = nullptr; }
}
void OpenOcdWorker::pumpOpenOCDLogs(bool muteLog) {
    if (ocdProcess && ocdProcess->state() == QProcess::Running) {
        QString out = QString::fromUtf8(ocdProcess->readAllStandardOutput()).trimmed(); if (!out.isEmpty() && !muteLog) emit sigLog(probeSN, out);
        QString err = QString::fromUtf8(ocdProcess->readAllStandardError()).trimmed(); if (!err.isEmpty() && !muteLog) emit sigLog(probeSN, err);
    }
}
QString OpenOcdWorker::sendTclCommand(const QString& cmd, int timeoutMs, bool muteLog) {
    if (!tclSocket || tclSocket->state() != QAbstractSocket::ConnectedState) return "";
    tclSocket->write((cmd + "\x1a").toUtf8()); tclSocket->waitForBytesWritten(200);
    QString res; QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeoutMs) { if (tclSocket->waitForReadyRead(50)) { res += tclSocket->readAll(); if (res.contains('\x1a')) break; } pumpOpenOCDLogs(muteLog); }
    res.remove('\x1a'); return res;
}
bool OpenOcdWorker::checkTargetConnected() {
    if (!tclSocket || tclSocket->state() != QAbstractSocket::ConnectedState) return false;
    QString res = sendTclCommand("mdw 0x20000000", 200, true);
    if (res.contains("Error", Qt::CaseInsensitive) || res.contains("Failed", Qt::CaseInsensitive) || res.isEmpty()) return false;
    return true;
}
void OpenOcdWorker::run() {
    tclSocket = new QTcpSocket(); bool probeConnected = false; bool initialized = false; int pollCounter = 0;
    while (running) {
        if (!probeConnected) {
            startOpenOCD(); tclSocket->connectToHost("127.0.0.1", tclPort);
            if (tclSocket->waitForConnected(1500)) {
                probeConnected = true; initialized = false;
                emit sigLog(probeSN, QString("[SYS] %1 debug service started on TCP:%2.").arg(useXds110 ? "XDS110(OpenOCD)" : "DAPLink(OpenOCD)").arg(tclPort));
            } else { emit sigStatus(probeSN, 0, "Awaiting Probe..."); msleep(1000); continue; }
        }
        if (!initialized) {
            QString res = sendTclCommand("init", 3000, true);
            /* init 的 TCL 返回体为空(Info 日志走 stderr), 用一次内存读确认目标已就绪 */
            bool initFailed = res.contains("Error", Qt::CaseInsensitive) || res.contains("Failed", Qt::CaseInsensitive);
            if (!initFailed) {
                QString probe = sendTclCommand("mdw 0x20000000", 800, true);
                initFailed = probe.contains("Error", Qt::CaseInsensitive) || probe.contains("Failed", Qt::CaseInsensitive) || probe.isEmpty();
            }
            initialized = !initFailed;
            if (initialized) { sendTclCommand(QString("adapter speed %1").arg(currentSpeedKHz), 500, true); emit sigLog(probeSN, "[SYS] OpenOCD target initialized (SWD)."); }
            else { emit sigStatus(probeSN, 0, "Target Unresponsive"); msleep(500); }
        }
        bool targetConnected = initialized;
        if (targetConnected) {
            if (!wasConnected) {
                wasConnected = true; emit sigStatus(probeSN, 1, "Target Connected"); emit sigUuid(probeSN, readUuidGeneric());
                emit sigFwVer(probeSN, readFwVersionGeneric());
                emit sigLog(probeSN, "[SYS] Target MCU connected via SWD.");
                if (autoFlashEnabled && !fwPath.isEmpty() && !targetFlashedThisSession) { targetFlashedThisSession = true; enqueueCommand(Command(CmdType::FLASH, 0, 0, fwPath)); }
            }
            pumpOpenOCDLogs(false);
        } else {
            if (wasConnected) { wasConnected = false; targetFlashedThisSession = false; emit sigStatus(probeSN, 0, "Awaiting Target..."); emit sigUuid(probeSN, "-"); emit sigFwVer(probeSN, "N/A"); }
            pumpOpenOCDLogs(true);
        }
        if (targetConnected && speedNeedsUpdate) { sendTclCommand(QString("adapter speed %1").arg(currentSpeedKHz), 500, false); speedNeedsUpdate = false; }
        Command cmd; bool hasCmd = false;
        { QMutexLocker lock(&queueMutex); if (!cmdQueue.empty()) { cmd = cmdQueue.front(); cmdQueue.pop(); hasCmd = true; } }
        if (hasCmd) { processCommandGeneric(cmd); pollCounter = 0; }
        else if (targetConnected && enablePolling) { pollTelemetryGeneric(); msleep(qMax(20, pollIntervalMs)); }
        else msleep(100);
        /* 周期性确认连接(每 ~5s 一次, 避免每次轮询多一次往返) */
        if (wasConnected && (++pollCounter >= 50) && enablePolling) {
            pollCounter = 0;
            if (!checkTargetConnected()) { wasConnected = false; targetFlashedThisSession = false; initialized = false; emit sigStatus(probeSN, 0, "Target Lost..."); emit sigUuid(probeSN, "-"); emit sigFwVer(probeSN, "N/A"); }
        }
    }
    stopOpenOCD();
}
void OpenOcdWorker::write32(uint32_t ofs, uint32_t val) { sendTclCommand(QString("mww 0x%1 0x%2").arg(BASE_ADDR + ofs, 8, 16, QChar('0')).arg(val, 8, 16, QChar('0'))); }
void OpenOcdWorker::write16(uint32_t ofs, uint16_t val) { sendTclCommand(QString("mwh 0x%1 0x%2").arg(BASE_ADDR + ofs, 8, 16, QChar('0')).arg(val, 4, 16, QChar('0'))); }
void OpenOcdWorker::write8(uint32_t ofs, uint8_t val) { sendTclCommand(QString("mwb 0x%1 0x%2").arg(BASE_ADDR + ofs, 8, 16, QChar('0')).arg(val, 2, 16, QChar('0'))); }
uint32_t OpenOcdWorker::read32(uint32_t ofs) { return sendTclCommand(QString("mdw 0x%1").arg(BASE_ADDR + ofs, 8, 16, QChar('0'))).split(":").last().trimmed().toUInt(nullptr, 16); }
uint16_t OpenOcdWorker::read16(uint32_t ofs) { return sendTclCommand(QString("mdh 0x%1").arg(BASE_ADDR + ofs, 8, 16, QChar('0'))).split(":").last().trimmed().toUShort(nullptr, 16); }
uint8_t OpenOcdWorker::read8(uint32_t ofs) { return sendTclCommand(QString("mdb 0x%1").arg(BASE_ADDR + ofs, 8, 16, QChar('0'))).split(":").last().trimmed().toUShort(nullptr, 16) & 0xFF; }
void OpenOcdWorker::writeAbs(uint32_t addr, uint32_t val, int size) {
    if (size == 4) sendTclCommand(QString("mww 0x%1 0x%2").arg(addr, 8, 16, QChar('0')).arg(val, 8, 16, QChar('0')));
    else if (size == 2) sendTclCommand(QString("mwh 0x%1 0x%2").arg(addr, 8, 16, QChar('0')).arg(val, 4, 16, QChar('0')));
    else sendTclCommand(QString("mwb 0x%1 0x%2").arg(addr, 8, 16, QChar('0')).arg(val, 2, 16, QChar('0')));
}
uint32_t OpenOcdWorker::readAbs(uint32_t addr, int size) {
    QString cmd = (size == 4) ? "mdw" : (size == 2) ? "mdh" : "mdb";
    QString res = sendTclCommand(QString("%1 0x%2").arg(cmd).arg(addr, 8, 16, QChar('0'))); return res.split(":").last().trimmed().toUInt(nullptr, 16);
}
void OpenOcdWorker::readBlock(uint32_t ofs, uint32_t byteCount, uint8_t* outData) {
    memset(outData, 0, byteCount); int wordCount = (byteCount + 3) / 4;
    QString res = sendTclCommand(QString("mdw 0x%1 %2").arg(BASE_ADDR + ofs, 8, 16, QChar('0')).arg(wordCount));
    if (res.contains("Error", Qt::CaseInsensitive)) return;
    QStringList lines = res.split('\n', Qt::SkipEmptyParts); int currentWordIdx = 0;
    for (const QString& line : lines) {
        int colonIdx = line.indexOf(':');
        if (colonIdx != -1) {
            QStringList words = line.mid(colonIdx + 1).trimmed().split(' ', Qt::SkipEmptyParts);
            for (const QString& wStr : words) {
                if (currentWordIdx * 4 >= byteCount) break;
                uint32_t val = wStr.toUInt(nullptr, 16);
                memcpy(outData + currentWordIdx * 4, &val, std::min((uint32_t)4, byteCount - currentWordIdx * 4));
                currentWordIdx++;
            }
        }
    }
}
void OpenOcdWorker::executeFlash(const QString& path) {
    QString safePath = path; safePath.replace("\\", "/");
    emit sigLog(probeSN, QString("[OPENOCD] Programming %1 ...").arg(safePath));
    QString res = sendTclCommand(QString("program %1 verify reset").arg(safePath), 30000, false);
    if (res.contains("Failed", Qt::CaseInsensitive) || res.contains("No target", Qt::CaseInsensitive)) {
        throw std::runtime_error("OpenOCD Error: Programming failed. Target missing or protected.");
    }
    if (!res.contains("Verified", Qt::CaseInsensitive)) {
        emit sigLog(probeSN, "[OPENOCD] Verify status unclear, checking flash bank...");
        sendTclCommand("reset halt", 2000, true);
        QString chk = sendTclCommand("flash verify_image " + safePath, 15000, false);
        sendTclCommand("reset run", 1000, true);
        if (chk.contains("error", Qt::CaseInsensitive) || chk.contains("failed", Qt::CaseInsensitive)) {
            throw std::runtime_error("OpenOCD Error: Verify failed after programming.");
        }
    }
    emit sigLog(probeSN, "[OPENOCD] Programming + Verify OK, target restarted.");
}
void OpenOcdWorker::triggerReset() { sendTclCommand("reset run"); }
