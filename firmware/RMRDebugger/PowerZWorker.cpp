#include "PowerZWorker.h"
#include <QDebug>
#include <QThread>
#include <cstring>
#include <setupapi.h>
#include <vector>

/* HID 设备接口 GUID */
static const GUID kHidGuid = {0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};

static constexpr int kReportLen = 65;            /* HID 报告长度(含报告ID) */
static constexpr int kPayloadOff = 9;            /* 报告内 ADC payload 偏移 */
static constexpr int kPayloadLen = 44;           /* AdcDataRaw 长度 */
static constexpr int kMaxFailures = 5;

PowerZWorker::PowerZWorker(QObject *parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, &PowerZWorker::poll);
}

PowerZWorker::~PowerZWorker() {
    stop();
}

bool PowerZWorker::start(int intervalMs) {
    m_timer->setInterval(intervalMs);
    bool ok = openDevice();
    if (!m_timer->isActive())
        m_timer->start();
    return ok;
}

void PowerZWorker::stop() {
    m_timer->stop();
    closeDevice();
}

void PowerZWorker::resetStats() {
    m_window.clear();
    m_winCount = 0;
    m_winVSum = m_winISum = m_winPSum = 0.0;
}

double PowerZWorker::statSec() const {
    if (m_window.size() < 2) return 0.0;
    return (m_window.back().ms - m_window.front().ms) / 1000.0;
}

static QString hidDevicePathFromSetupApi() {
    /* 枚举 HID 接口, 找 VID_5FC9&PID_0063 */
    HDEVINFO devs = SetupDiGetClassDevsW(&kHidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devs == INVALID_HANDLE_VALUE)
        return QString();
    QString result;
    for (DWORD idx = 0;; ++idx) {
        SP_DEVICE_INTERFACE_DATA did;
        ZeroMemory(&did, sizeof(did));
        did.cbSize = sizeof(did);
        if (!SetupDiEnumDeviceInterfaces(devs, nullptr, &kHidGuid, idx, &did))
            break;
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(devs, &did, nullptr, 0, &need, nullptr);
        if (need == 0 || need > 4096)
            continue;
        std::vector<BYTE> buf(need);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devs, &did, detail, need, nullptr, nullptr))
            continue;
        QString path = QString::fromWCharArray(detail->DevicePath);
        if (path.contains(QStringLiteral("vid_5fc9&pid_0063"), Qt::CaseInsensitive)) {
            result = path;
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(devs);
    return result;
}

bool PowerZWorker::openDevice() {
    if (m_handle != INVALID_HANDLE_VALUE)
        return true;

    m_devicePath = hidDevicePathFromSetupApi();
    if (m_devicePath.isEmpty())
        return false;

    m_handle = CreateFileW((LPCWSTR)m_devicePath.utf16(),
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) {
        m_devicePath.clear();
        return false;
    }
    if (!m_connected) {
        m_connected = true;
        resetStats();
        emit stateChanged(true, tr("Power-Z KM003C 已连接"));
    }
    return true;
}

void PowerZWorker::closeDevice() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
    m_devicePath.clear();
    m_connected = false;
}

void PowerZWorker::poll() {
    if (m_handle == INVALID_HANDLE_VALUE && !openDevice()) {
        if (m_failCount < 100000) m_failCount++;
        if (m_failCount == 1)
            emit stateChanged(false, tr("Power-Z KM003C 未找到 (VID_5FC9/PID_0063)"));
        return;
    }

    BYTE out[kReportLen];
    ZeroMemory(out, sizeof(out));
    out[0] = 0x00;                                   /* 报告ID */
    out[1] = 0x0C;                                   /* GetData */
    out[2] = m_tid++;
    out[3] = 0x02;                                   /* attr ADC(0x0001) << 1 */
    out[4] = 0x00;

    DWORD written = 0;
    if (!WriteFile(m_handle, out, kReportLen, &written, nullptr) || written != kReportLen) {
        if (++m_failCount >= kMaxFailures) {
            closeDevice();
            emit stateChanged(false, tr("Power-Z 通信失败, 等待重连"));
        }
        return;
    }

    BYTE inBuf[kReportLen];
    ZeroMemory(inBuf, sizeof(inBuf));
    DWORD readBytes = 0;
    if (!ReadFile(m_handle, inBuf, kReportLen, &readBytes, nullptr) || readBytes < kPayloadOff + kPayloadLen) {
        if (++m_failCount >= kMaxFailures) {
            closeDevice();
            emit stateChanged(false, tr("Power-Z 通信失败, 等待重连"));
        }
        return;
    }

    /* 响应: [reportID][主头4B 41 tid ..][扩展头4B][payload 44B] */
    if (inBuf[1] != 0x41) {  /* 不是 PutData, 丢弃 */
        m_failCount = 0;
        return;
    }

    const BYTE *p = inBuf + kPayloadOff;
    auto rdI32 = [&](int off) -> qint32 {
        qint32 v;
        memcpy(&v, p + off, 4);
        return v;
    };
    auto rdI16 = [&](int off) -> qint16 {
        qint16 v;
        memcpy(&v, p + off, 2);
        return v;
    };

    m_vbus = rdI32(0) / 1e6;
    m_ibus = rdI32(4) / 1e6;
    m_vbusAvg = rdI32(8) / 1e6;
    m_ibusAvg = rdI32(12) / 1e6;
    m_tempC = rdI16(24) / 128.0;
    m_power = m_vbus * m_ibus;

    /* 10s 滑动窗口平均: 压入新样本, 弹出超窗样本 */
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_window.push_back({nowMs, m_vbus, m_ibus, m_power});
    m_winVSum += m_vbus;
    m_winISum += m_ibus;
    m_winPSum += m_power;
    m_winCount = (quint32)m_window.size();
    while (m_window.size() > 1 && nowMs - m_window.front().ms > kStatWindowMs) {
        m_winVSum -= m_window.front().v;
        m_winISum -= m_window.front().i;
        m_winPSum -= m_window.front().p;
        m_window.pop_front();
        m_winCount = (quint32)m_window.size();
    }

    m_failCount = 0;
    emit telemetry(m_vbus, m_ibus, m_vbusAvg, m_ibusAvg, m_tempC);
}
