// Direct CTAP2 over HID (CTAPHID) transport with the hmac-secret extension.
// Windows webauthn.dll only exposes PRF/hmac-secret for resident credentials,
// which forces PIN/UV on hardware security keys; the direct transport lets us
// use non-resident credentials with touch-only presence checks.
#include "fwsec_ctap2.h"
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" {
#include <hidsdi.h>
#include <setupapi.h>
}
#include "fwsec_sha256.h"
#include "mlkem/randombytes.h"
#include <cstdio>
#include <cwchar>

namespace fwsec {

namespace {

// ---- CTAP HID constants (USB HID report = 64 bytes, first byte report id) ----
constexpr u32 CID_BROADCAST  = 0xFFFFFFFFu;
constexpr u8  CTAPHID_INIT    = 0x86;  // 0x80 | 0x06
constexpr u8  CTAPHID_CONT    = 0x80;  // continuation
constexpr u8  CTAPHID_CBOR    = 0x90;  // 0x80 | 0x10
constexpr u8  CTAPHID_KEEPALIVE = 0xBB;
constexpr u8  CTAPHID_ERROR   = 0xBF;
constexpr int TOUCH_TIMEOUT_MS = 30000;

// ---- touch status callback ----
Ctap2StatusCb g_cb = nullptr;
void* g_cb_user = nullptr;
void notify(const char* msg) { if (g_cb) g_cb(msg, g_cb_user); }

std::string err_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string ctap_error_text(u8 code) {
    switch (code) {
    case 0x00: return "OK";
    case 0x01: return "无效命令";
    case 0x02: return "参数无效";
    case 0x03: return "长度无效";
    case 0x04: return "序列错误";
    case 0x05: return "安全密钥超时";
    case 0x06: return "CBOR 解析错误";
    case 0x0A: return "请求被取消";
    case 0x0B: return "安全密钥内部错误";
    case 0x0C: return "不支持该扩展 (hmac-secret/PRF)";
    case 0x0D: return "CBOR 类型错误";
    case 0x11: return "操作被拒绝";
    case 0x12: return "未找到对应凭据 (凭据 ID 与密钥不匹配?)";
    case 0x14: return "缺少参数 (可能需要 PIN 或密钥不支持)";
    case 0x2B: return "需要 PIN 验证 (密钥策略要求)";
    case 0x2C: return "PIN 无效";
    case 0x2D: return "等待触碰超时, 请重试";
    case 0x2E: return "操作不允许 (凭据与密钥不匹配?)";
    case 0x2F: return "操作被拒绝 (PIN 策略或密钥限制)";
    case 0x31: return "未设置 PIN";
    default: {
        char b[32]; snprintf(b, sizeof(b), "CTAP2 错误 0x%02X", code);
        return b;
    }
    }
}

// ---- minimal CBOR writer ----
class CborWriter {
public:
    std::vector<u8> b;
    void head(int major, u64 arg) {
        if (arg < 24) {
            b.push_back((u8)((major << 5) | arg));
        } else if (arg <= 0xFF) {
            b.push_back((u8)((major << 5) | 24)); b.push_back((u8)arg);
        } else if (arg <= 0xFFFF) {
            b.push_back((u8)((major << 5) | 25));
            b.push_back((u8)(arg >> 8)); b.push_back((u8)arg);
        } else if (arg <= 0xFFFFFFFFu) {
            b.push_back((u8)((major << 5) | 26));
            for (int i = 3; i >= 0; i--) b.push_back((u8)(arg >> (8 * i)));
        } else {
            b.push_back((u8)((major << 5) | 27));
            for (int i = 7; i >= 0; i--) b.push_back((u8)(arg >> (8 * i)));
        }
    }
    void u(u64 v) { head(0, v); }
    void neg(u64 v) { head(1, v); }               // value = -1 - v
    void tstr(const std::string& s) { head(3, s.size()); b.insert(b.end(), s.begin(), s.end()); }
    void bstr(const u8* d, size_t n) { head(2, n); b.insert(b.end(), d, d + n); }
    void mapStart(size_t n) { head(5, n); }
    void arrStart(size_t n) { head(4, n); }
    void boolean(bool v) { b.push_back(v ? 0xF5 : 0xF4); }
};

// ---- minimal CBOR reader ----
class CborReader {
public:
    const u8* p;
    const u8* end;
    bool ok = true;
    explicit CborReader(const u8* d, size_t n) : p(d), end(d + n) {}
    int major() const { return lastMajor; }
    u64 arg() const { return lastArg; }
    bool readHead() {
        lastMajor = -1; lastArg = 0;
        if (p >= end) { ok = false; return false; }
        u8 b = *p++;
        lastMajor = b >> 5; lastArg = b & 0x1f;
        if (lastArg < 24) return true;
        size_t extra = 0;
        switch (lastArg) {
        case 24: extra = 1; break;
        case 25: extra = 2; break;
        case 26: extra = 4; break;
        case 27: extra = 8; break;
        default: ok = false; return false;
        }
        if ((size_t)(end - p) < extra) { ok = false; return false; }
        u64 v = 0;
        for (size_t i = 0; i < extra; i++) v = (v << 8) | *p++;
        lastArg = v;
        return true;
    }
    // Skip the item whose head was already read.
    bool skip() {
        switch (lastMajor) {
        case 0: case 1: case 7: return true;
        case 2: case 3:
            if ((size_t)(end - p) < lastArg) { ok = false; return false; }
            p += lastArg; return true;
        case 4: case 5:
            for (u64 i = 0; i < lastArg; i++) {
                if (!readHead()) return false;
                if (!skip()) return false;
            }
            return true;
        default: ok = false; return false;
        }
    }
private:
    int lastMajor = -1;
    u64 lastArg = 0;
};

// ---- HID device handle ----
class HidDev {
public:
    ~HidDev() { close(); }
    bool attach(HANDLE h, u32 inLen, u32 outLen) {
        close();
        h_ = h; inLen_ = inLen; outLen_ = outLen;
        return h_ != INVALID_HANDLE_VALUE;
    }
    bool valid() const { return h_ != INVALID_HANDLE_VALUE; }
    void close() {
        if (h_ != INVALID_HANDLE_VALUE) { CloseHandle(h_); h_ = INVALID_HANDLE_VALUE; }
    }
    u32 inLen() const { return inLen_; }
    u32 outLen() const { return outLen_; }

    bool sendRaw(const u8* buf, size_t n) {
        if (h_ == INVALID_HANDLE_VALUE || n < 8 || n > 256) return false;
        OVERLAPPED ov = {}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;
        DWORD wrote = 0;
        BOOL ok = WriteFile(h_, buf, (DWORD)n, nullptr, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
            ok = GetOverlappedResult(h_, &ov, &wrote, TRUE);
        else if (ok)
            wrote = (DWORD)n;
        CloseHandle(ov.hEvent);
        return ok && wrote == n;
    }

    // Reads one input report. On timeout returns false with timedOut=true.
    bool recvRaw(u8* buf, size_t n, int timeoutMs, bool& timedOut) {
        timedOut = false;
        if (h_ == INVALID_HANDLE_VALUE || n < 8 || n > 256) return false;
        OVERLAPPED ov = {}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return false;
        DWORD rd = 0;
        BOOL ok = ReadFile(h_, buf, (DWORD)n, nullptr, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, (DWORD)timeoutMs);
            if (wait == WAIT_TIMEOUT) {
                CancelIoEx(h_, &ov);
                WaitForSingleObject(ov.hEvent, 500);
                if (GetOverlappedResult(h_, &ov, &rd, FALSE) && rd == n) {
                    // Data raced in right at the timeout boundary: keep it.
                    CloseHandle(ov.hEvent);
                    return true;
                }
                timedOut = true;
                CloseHandle(ov.hEvent);
                return false;
            }
            ok = GetOverlappedResult(h_, &ov, &rd, FALSE);
        } else if (ok) {
            rd = (DWORD)n;
        }
        CloseHandle(ov.hEvent);
        return ok && rd == n;
    }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
    u32 inLen_ = 0, outLen_ = 0;
};

// ---- CTAP HID channel ----
class CtapHid {
public:
    ~CtapHid() { close(); }
    bool initialized() const { return dev_.valid() && cid_ != 0; }
    void close() { dev_.close(); cid_ = 0; }

    // Enumerate FIDO HID devices and init the first responsive one.
    bool openFirst(std::string& err) {
        GUID guid;
        HidD_GetHidGuid(&guid);
        HDEVINFO devs = SetupDiGetClassDevs(&guid, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devs == INVALID_HANDLE_VALUE) { err = "无法枚举 HID 设备"; return false; }
        std::wstring lastW;
        for (DWORD i = 0; ; i++) {
            SP_DEVICE_INTERFACE_DATA did = {};
            did.cbSize = sizeof(did);
            if (!SetupDiEnumDeviceInterfaces(devs, nullptr, &guid, i, &did)) break;
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetail(devs, &did, nullptr, 0, &need, nullptr);
            if (need == 0 || need > 8192) continue;
            std::vector<u8> buf(need);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(buf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            if (!SetupDiGetDeviceInterfaceDetail(devs, &did, detail, need, nullptr, nullptr)) continue;
            std::wstring path(detail->DevicePath);
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OVERLAPPED, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            PHIDP_PREPARSED_DATA ppd = nullptr;
            HIDP_CAPS caps = {};
            bool fido = false;
            if (HidD_GetPreparsedData(h, &ppd)) {
                if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS)
                    fido = (caps.UsagePage == 0xF1D0) && (caps.Usage == 0x01 || caps.Usage == 0x00);
                HidD_FreePreparsedData(ppd);
            }
            if (!fido || caps.InputReportByteLength < 16 || caps.OutputReportByteLength < 16) {
                CloseHandle(h);
                continue;
            }
            dev_.attach(h, caps.InputReportByteLength, caps.OutputReportByteLength);
            std::string ierr;
            if (init(ierr)) { lastW.clear(); break; }
            lastW = L"init failed: " + std::wstring(ierr.begin(), ierr.end());
            dev_.close();
        }
        SetupDiDestroyDeviceInfoList(devs);
        if (!initialized()) {
            err = "未找到可用的 FIDO2 安全密钥, 请确认已插入";
            if (!lastW.empty()) err += " (" + err_utf8(lastW) + ")";
            return false;
        }
        return true;
    }

    bool init(std::string& err) {
        u8 nonce[8];
        if (randombytes(nonce, sizeof(nonce)) != 0) { err = "随机数生成失败"; return false; }
        u8 pkt[64] = {0};
        store32_be(pkt + 1, CID_BROADCAST);
        pkt[5] = CTAPHID_INIT;
        pkt[6] = 0; pkt[7] = 8;
        memcpy(pkt + 8, nonce, 8);
        if (!dev_.sendRaw(pkt, dev_.outLen())) { err = "写入安全密钥失败"; return false; }
        for (int i = 0; i < 10; i++) {
            u8 rsp[64];
            bool timedOut = false;
            if (!dev_.recvRaw(rsp, dev_.inLen(), 1500, timedOut)) {
                if (timedOut) { err = "安全密钥无响应"; return false; }
                err = "读取安全密钥失败"; return false;
            }
            u8 t = rsp[5];
            if (t == CTAPHID_ERROR) { err = "安全密钥 INIT 错误"; return false; }
            if (t != CTAPHID_INIT) continue;
            if (memcmp(rsp + 8, nonce, 8) != 0) { err = "安全密钥 INIT 应答异常"; return false; }
            cid_ = load32_be(rsp + 1);
            return true;
        }
        err = "安全密钥响应超时";
        return false;
    }

    // Full CBOR transaction.
    bool cbor(const std::vector<u8>& req, std::vector<u8>& resp, std::string& err) {
        if (!initialized()) { err = "安全密钥未连接"; return false; }
        if (!sendFrames(CTAPHID_CBOR, req.data(), req.size(), err)) return false;
        return recvResponse(resp, err);
    }

private:
    bool sendFrames(u8 cmd, const u8* data, size_t len, std::string& err) {
        if (len > 7400) { err = "消息过长"; return false; }
        u8 pkt[64] = {0};
        store32_be(pkt + 1, cid_);
        pkt[5] = cmd;
        pkt[6] = (u8)(len >> 8); pkt[7] = (u8)(len & 0xff);
        size_t off = 0, take = len > 56 ? 56 : len;
        memcpy(pkt + 8, data, take); off += take;
        if (!dev_.sendRaw(pkt, dev_.outLen())) { err = "写入安全密钥失败"; return false; }
        u8 seq = 0;
        while (off < len) {
            u8 p2[64] = {0};
            store32_be(p2 + 1, cid_);
            p2[5] = seq++;
            take = len - off > 58 ? 58 : len - off;
            memcpy(p2 + 6, data + off, take); off += take;
            if (!dev_.sendRaw(p2, dev_.outLen())) { err = "写入安全密钥失败"; return false; }
        }
        return true;
    }

    bool recvResponse(std::vector<u8>& out, std::string& err) {
        u8 pkt[64];
        u16 bcnt = 0;
        size_t need = 0, got = 0;
        // First packet.
        for (;;) {
            bool timedOut = false;
            if (!dev_.recvRaw(pkt, dev_.inLen(), TOUCH_TIMEOUT_MS, timedOut)) {
                if (timedOut) { err = "等待安全密钥响应超时, 请重试"; return false; }
                err = "读取安全密钥失败"; return false;
            }
            u8 t = pkt[5];
            if (t == CTAPHID_KEEPALIVE) {
                if (pkt[8] == 0x02) notify("touch");
                continue;
            }
            if (t == CTAPHID_ERROR) { err = ctap_error_text(pkt[8]); return false; }
            if (t != CTAPHID_CBOR) { err = "安全密钥响应异常"; return false; }
            if (load32_be(pkt + 1) != cid_) { err = "安全密钥通道不匹配"; return false; }
            bcnt = (u16)((pkt[6] << 8) | pkt[7]);
            if (bcnt == 0) { err = "安全密钥响应为空"; return false; }
            u8 status = pkt[8];
            if (status != 0x00) { err = ctap_error_text(status); return false; }
            need = bcnt - 1;
            out.resize(need);
            size_t take = need > 55 ? 55 : need;
            memcpy(out.data(), pkt + 9, take);
            got = take;
            break;
        }
        // Continuation packets.
        u8 seq = 0;
        while (got < need) {
            bool timedOut = false;
            if (!dev_.recvRaw(pkt, dev_.inLen(), TOUCH_TIMEOUT_MS, timedOut)) {
                if (timedOut) { err = "等待安全密钥响应超时, 请重试"; return false; }
                err = "读取安全密钥失败"; return false;
            }
            u8 t = pkt[5];
            if (t == CTAPHID_KEEPALIVE) {
                if (pkt[8] == 0x02) notify("touch");
                continue;
            }
            if (t == CTAPHID_ERROR) { err = ctap_error_text(pkt[8]); return false; }
            if (t != CTAPHID_CONT) { err = "安全密钥响应帧错误"; return false; }
            if (load32_be(pkt + 1) != cid_) { err = "安全密钥通道不匹配"; return false; }
            if (pkt[6] != seq++) { err = "安全密钥响应序号错误"; return false; }
            size_t take = need - got > 58 ? 58 : need - got;
            memcpy(out.data() + got, pkt + 7, take);
            got += take;
        }
        return true;
    }

    HidDev dev_;
    u32 cid_ = 0;
};

// Deterministic 32-byte user id (mirrors the old Windows WebAuthn path).
void user_id_for(const std::string& rp_id, const std::string& user_name, u8 out[32]) {
    memset(out, 0, 32);
    for (size_t i = 0; i < rp_id.size() && i < 128; i++) out[i % 32] ^= (u8)(rp_id[i] + i * 3);
    for (size_t i = 0; i < user_name.size() && i < 128; i++) out[(i + 7) % 32] ^= (u8)(user_name[i] + i * 3);
}

const char* CLIENT_DATA_CREATE = "{\"type\":\"webauthn.create\",\"challenge\":\"rmr-fwsec\",\"origin\":\"https://rmr.local\"}";
const char* CLIENT_DATA_GET    = "{\"type\":\"webauthn.get\",\"challenge\":\"rmr-fwsec\",\"origin\":\"https://rmr.local\"}";

bool parse_attested_cred(const u8* ad, size_t adLen, std::vector<u8>& cred_id,
                         std::vector<u8>& aaguid, bool& hmacOk, std::string& err) {
    hmacOk = true;
    if (adLen < 37) { err = "认证数据过短"; return false; }
    const u8* q = ad;
    const u8* end = ad + adLen;
    q += 32;                 // rpIdHash
    u8 flags = *q++;
    q += 4;                  // signCount
    if (flags & 0x40) {      // attested credential data
        if (end - q < 18) { err = "认证数据异常"; return false; }
        aaguid.assign(q, q + 16); q += 16;
        u16 clen = (u16)((q[0] << 8) | q[1]); q += 2;
        if (clen == 0 || clen > 512 || end - q < clen) { err = "凭据 ID 异常"; return false; }
        cred_id.assign(q, q + clen); q += clen;
        CborReader ck(q, (size_t)(end - q));
        if (!ck.readHead() || !ck.skip()) { err = "COSE 公钥解析失败"; return false; }
        q = ck.p;
    } else {
        err = "未返回凭据数据 (可能被其他操作占用)";
        return false;
    }
    if ((flags & 0x80) && q < end) {  // extensions
        CborReader ex(q, (size_t)(end - q));
        if (!ex.readHead() || ex.major() != 5) { err = "扩展解析失败"; return false; }
        for (u64 i = 0; i < ex.arg(); i++) {
            if (!ex.readHead()) break;
            bool isHmac = (ex.major() == 3 && ex.arg() == 10 &&
                           (size_t)(ex.end - ex.p) >= 10 && memcmp(ex.p, "hmac-secret", 10) == 0);
            if (isHmac) {
                ex.p += 10;
                if (!ex.readHead() || !ex.skip()) break;
                hmacOk = (ex.major() == 7 && ex.arg() == 21);
                break;
            } else {
                if (!ex.skip()) break;
                if (!ex.readHead() || !ex.skip()) break;
            }
        }
    }
    return true;
}

bool parse_assertion_ext(const std::vector<u8>& resp, u8 secret[32], std::string& err) {
    CborReader r(resp.data(), resp.size());
    if (!r.readHead() || r.major() != 5) { err = "断言响应解析失败"; return false; }
    bool foundExt = false;
    for (u64 i = 0; i < r.arg(); i++) {
        if (!r.readHead()) { err = "断言响应解析失败"; return false; }
        if (r.major() == 0 && r.arg() == 5) {
            foundExt = true;
            if (!r.readHead() || r.major() != 5) { err = "扩展解析失败"; return false; }
            for (u64 j = 0; j < r.arg(); j++) {
                if (!r.readHead()) { err = "扩展解析失败"; return false; }
                bool isHmac = (r.major() == 3 && r.arg() == 10 &&
                               (size_t)(r.end - r.p) >= 10 && memcmp(r.p, "hmac-secret", 10) == 0);
                if (isHmac) {
                    r.p += 10;
                    if (!r.readHead() || r.major() != 5) { err = "hmac-secret 结果缺失"; return false; }
                    bool foundOut = false;
                    for (u64 k = 0; k < r.arg(); k++) {
                        if (!r.readHead()) { err = "hmac-secret 结果解析失败"; return false; }
                        bool isOut1 = (r.major() == 3 && r.arg() == 7 &&
                                      (size_t)(r.end - r.p) >= 7 && memcmp(r.p, "output1", 7) == 0);
                        if (isOut1) {
                            r.p += 7;
                            if (!r.readHead() || r.major() != 2 || r.arg() < 32 ||
                                (size_t)(r.end - r.p) < r.arg()) { err = "output1 数据异常"; return false; }
                            memcpy(secret, r.p, 32);
                            foundOut = true;
                            break;
                        } else {
                            if (!r.skip()) { err = "hmac-secret 结果解析失败"; return false; }
                            if (!r.readHead() || !r.skip()) { err = "hmac-secret 结果解析失败"; return false; }
                        }
                    }
                    if (!foundOut) { err = "安全密钥未返回 hmac-secret 输出"; return false; }
                    return true;
                } else {
                    if (!r.skip()) { err = "扩展解析失败"; return false; }
                    if (!r.readHead() || !r.skip()) { err = "扩展解析失败"; return false; }
                }
            }
            break;
        } else {
            if (!r.skip()) { err = "断言响应解析失败"; return false; }
            if (!r.readHead() || !r.skip()) { err = "断言响应解析失败"; return false; }
        }
    }
    if (!foundExt) { err = "安全密钥未返回 hmac-secret 扩展 (不支持?)"; return false; }
    err = "安全密钥未返回 hmac-secret 输出";
    return false;
}

} // namespace

void ctap2_set_status_cb(Ctap2StatusCb cb, void* user) { g_cb = cb; g_cb_user = user; }

bool ctap2_available(std::string& error) {
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO devs = SetupDiGetClassDevs(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devs == INVALID_HANDLE_VALUE) { error = "无法枚举 HID 设备"; return false; }
    bool found = false;
    for (DWORD i = 0; ; i++) {
        SP_DEVICE_INTERFACE_DATA did = {};
        did.cbSize = sizeof(did);
        if (!SetupDiEnumDeviceInterfaces(devs, nullptr, &guid, i, &did)) break;
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetail(devs, &did, nullptr, 0, &need, nullptr);
        if (need == 0 || need > 8192) continue;
        std::vector<u8> buf(need);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        if (!SetupDiGetDeviceInterfaceDetail(devs, &did, detail, need, nullptr, nullptr)) continue;
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        PHIDP_PREPARSED_DATA ppd = nullptr;
        if (HidD_GetPreparsedData(h, &ppd)) {
            HIDP_CAPS caps = {};
            if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS &&
                caps.UsagePage == 0xF1D0 && (caps.Usage == 0x01 || caps.Usage == 0x00))
                found = true;
            HidD_FreePreparsedData(ppd);
        }
        CloseHandle(h);
        if (found) break;
    }
    SetupDiDestroyDeviceInfoList(devs);
    if (!found) error = "未找到 FIDO2 安全密钥, 请插入后重试";
    return found;
}

bool ctap2_make_credential(const std::string& rp_id, const std::string& user_name,
                           std::vector<u8>& cred_id, std::vector<u8>& aaguid,
                           std::string& error) {
    if (rp_id.empty() || user_name.empty()) { error = "凭据信息缺失"; return false; }
    u8 user_id[32];
    user_id_for(rp_id, user_name, user_id);

    CborWriter w;
    w.mapStart(4);
    w.u(1); w.mapStart(2); w.tstr("id"); w.tstr(rp_id); w.tstr("name"); w.tstr(rp_id);
    w.u(2); w.mapStart(3); w.tstr("id"); w.bstr(user_id, 32);
    w.tstr("name"); w.tstr(user_name); w.tstr("displayName"); w.tstr(user_name);
    w.u(3); w.arrStart(1); w.mapStart(2); w.tstr("alg"); w.neg(6); w.tstr("type"); w.tstr("public-key");
    w.u(5); w.mapStart(1); w.tstr("hmac-secret"); w.boolean(true);
    w.u(6); w.mapStart(2); w.tstr("rk"); w.boolean(false); w.tstr("up"); w.boolean(true);

    CtapHid ch;
    if (!ch.openFirst(error)) return false;
    std::vector<u8> resp;
    if (!ch.cbor(w.b, resp, error)) { ch.close(); return false; }
    ch.close();

    CborReader r(resp.data(), resp.size());
    if (!r.readHead() || r.major() != 5) { error = "创建凭据响应解析失败"; return false; }
    bool found = false;
    bool hmacOk = true;
    for (u64 i = 0; i < r.arg(); i++) {
        if (!r.readHead()) { error = "创建凭据响应解析失败"; return false; }
        if (r.major() == 0 && r.arg() == 3) {
            if (!r.readHead() || r.major() != 2) { error = "认证数据缺失"; return false; }
            const u8* ad = r.p;
            size_t adLen = (size_t)r.arg();
            r.p += adLen;
            if (!parse_attested_cred(ad, adLen, cred_id, aaguid, hmacOk, error)) return false;
            found = true;
        } else {
            if (!r.skip()) { error = "创建凭据响应解析失败"; return false; }
            if (!r.readHead() || !r.skip()) { error = "创建凭据响应解析失败"; return false; }
        }
    }
    if (!found) { error = "创建凭据响应缺少凭据数据"; return false; }
    if (!hmacOk) { error = "安全密钥未启用 hmac-secret 扩展"; return false; }
    secure_zero(user_id, sizeof(user_id));
    return true;
}

bool ctap2_get_hmac_secret(const std::string& rp_id, const std::vector<u8>& cred_id,
                           const u8 salt[32], u8 secret[32], std::string& error) {
    if (rp_id.empty() || cred_id.empty()) { error = "凭据信息缺失"; return false; }
    if (cred_id.size() > 512) { error = "凭据 ID 异常"; return false; }

    u8 cdh[32];
    sha256(CLIENT_DATA_GET, strlen(CLIENT_DATA_GET), cdh);

    CborWriter w;
    w.mapStart(5);
    w.u(1); w.tstr(rp_id);
    w.u(2); w.bstr(cdh, 32);
    w.u(3); w.arrStart(1); w.mapStart(2); w.tstr("id"); w.bstr(cred_id.data(), cred_id.size());
    w.tstr("type"); w.tstr("public-key");
    w.u(5); w.mapStart(1); w.tstr("hmac-secret"); w.mapStart(1); w.tstr("salt1"); w.bstr(salt, 32);
    w.u(6); w.mapStart(1); w.tstr("up"); w.boolean(true);

    CtapHid ch;
    if (!ch.openFirst(error)) return false;
    std::vector<u8> resp;
    if (!ch.cbor(w.b, resp, error)) { ch.close(); return false; }
    ch.close();

    if (!parse_assertion_ext(resp, secret, error)) return false;
    return true;
}

} // namespace fwsec



