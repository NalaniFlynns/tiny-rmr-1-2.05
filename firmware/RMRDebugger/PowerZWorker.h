#pragma once
#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <deque>
#include <QString>
#include <windows.h>

/* Power-Z KM003C (VID 0x5FC9 / PID 0x0063) 真实电压电流计接入。
   通过 Windows HID 接口(无需驱动)以 Basic Mode 轮询 ADC:
   发送 GetData(0x0C, attr=ADC) -> 收到 PutData(0x41) 响应,
   payload 为 44 字节 AdcDataRaw: vbus_uv/i32, ibus_ua/i32, vbus_avg_uv/i32,
   ibus_avg_ua/i32, raw avg x2, temp_raw/i16(1/128C), vcc1/2, vdp, vdm, vdd,
   rate, flags, ...  */

class PowerZWorker : public QObject {
    Q_OBJECT
public:
    explicit PowerZWorker(QObject *parent = nullptr);
    ~PowerZWorker() override;

    bool start(int intervalMs = 200);
    void stop();
    bool isConnected() const { return m_handle != INVALID_HANDLE_VALUE; }

    double vbus() const { return m_vbus; }        /* V, 瞬时 */
    double ibus() const { return m_ibus; }        /* A, 瞬时(带方向) */
    double vbusAvg() const { return m_vbusAvg; }  /* V, 平均 */
    double ibusAvg() const { return m_ibusAvg; }  /* A, 平均 */
    double power() const { return m_power; }      /* W = V*I */
    double tempC() const { return m_tempC; }

    /* 平均统计: 最近 10s 滑动窗口(200ms 采样, 约 50 样本) */
    void resetStats();
    double avgV() const { return m_winCount ? m_winVSum / m_winCount : 0.0; }
    double avgI() const { return m_winCount ? m_winISum / m_winCount : 0.0; }
    double avgP() const { return m_winCount ? m_winPSum / m_winCount : 0.0; }
    double statSec() const;

signals:
    void telemetry(double vbus, double ibus, double vbusAvg, double ibusAvg, double tempC);
    void stateChanged(bool connected, const QString &detail);

private slots:
    void poll();

private:
    bool openDevice();
    void closeDevice();

    QString m_devicePath;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    QTimer *m_timer = nullptr;
    quint8 m_tid = 0;
    int m_failCount = 0;
    bool m_connected = false;
    double m_vbus = 0.0, m_ibus = 0.0, m_vbusAvg = 0.0, m_ibusAvg = 0.0, m_tempC = 0.0, m_power = 0.0;
    struct PzSample { qint64 ms; double v, i, p; };
    std::deque<PzSample> m_window;
    static constexpr int kStatWindowMs = 10000;
    quint32 m_winCount = 0;
    double m_winVSum = 0.0, m_winISum = 0.0, m_winPSum = 0.0;
};
