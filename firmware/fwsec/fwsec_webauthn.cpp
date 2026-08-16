// FIDO2 / WebAuthn PRF helper. Dynamically loads webauthn.dll so the app
// still runs on systems without WebAuthn support.
#include "fwsec_webauthn.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include "webauthn_ms.h"

namespace fwsec {

namespace {

using pfnGetApiVersion   = DWORD (WINAPI*)();
using pfnMakeCredential  = HRESULT (WINAPI*)(HWND, PCWEBAUTHN_RP_ENTITY_INFORMATION,
                                              PCWEBAUTHN_USER_ENTITY_INFORMATION,
                                              PCWEBAUTHN_COSE_CREDENTIAL_PARAMETERS,
                                              PCWEBAUTHN_CLIENT_DATA,
                                              PCWEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS,
                                              PWEBAUTHN_CREDENTIAL_ATTESTATION*);
using pfnGetAssertion    = HRESULT (WINAPI*)(HWND, LPCWSTR,
                                              PCWEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS,
                                              PWEBAUTHN_ASSERTION*);
using pfnFreeAssertion   = void    (WINAPI*)(PWEBAUTHN_ASSERTION);
using pfnFreeAttestation = void    (WINAPI*)(PWEBAUTHN_CREDENTIAL_ATTESTATION);
using pfnGetErrorName    = LPCWSTR (WINAPI*)(HRESULT);

struct WaApi {
    HMODULE dll = nullptr;
    pfnGetApiVersion   apiVersion   = nullptr;
    pfnMakeCredential  makeCred     = nullptr;
    pfnGetAssertion    getAssertion = nullptr;
    pfnFreeAssertion   freeAssert   = nullptr;
    pfnFreeAttestation freeAttest   = nullptr;
    pfnGetErrorName    getErrorName = nullptr;
};

WaApi& api() {
    static WaApi a;
    if (!a.dll) {
        a.dll = LoadLibraryW(L"webauthn.dll");
        if (a.dll) {
            a.apiVersion   = (pfnGetApiVersion)GetProcAddress(a.dll, "WebAuthNGetApiVersionNumber");
            a.makeCred     = (pfnMakeCredential)GetProcAddress(a.dll, "WebAuthNAuthenticatorMakeCredential");
            a.getAssertion = (pfnGetAssertion)GetProcAddress(a.dll, "WebAuthNAuthenticatorGetAssertion");
            a.freeAssert   = (pfnFreeAssertion)GetProcAddress(a.dll, "WebAuthNFreeAssertion");
            a.freeAttest   = (pfnFreeAttestation)GetProcAddress(a.dll, "WebAuthNFreeCredentialAttestation");
            a.getErrorName = (pfnGetErrorName)GetProcAddress(a.dll, "WebAuthNGetErrorName");
        }
    }
    return a;
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string to_utf8(const wchar_t* w) {
    if (!w || !*w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n - 1, nullptr, nullptr);
    return s;
}

DWORD attachment_for(u8 device) {
    switch (device) {
        case 1:  return WEBAUTHN_AUTHENTICATOR_ATTACHMENT_CROSS_PLATFORM;
        case 2:  return WEBAUTHN_AUTHENTICATOR_ATTACHMENT_PLATFORM;
        default: return WEBAUTHN_AUTHENTICATOR_ATTACHMENT_ANY;
    }
}

std::string hr_text(long hr) {
    WaApi& a = api();
    if (a.getErrorName) {
        LPCWSTR name = a.getErrorName((HRESULT)hr);
        if (name && *name) return to_utf8(name);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)hr);
    return buf;
}

// Deterministic PRF salt: 32 bytes derived from the 16-byte container salt.
// (Raw hmac-secret salts must be exactly 32 bytes.)
void prf_salt_from(const u8 salt16[16], u8 out[32]) {
    for (int i = 0; i < 32; i++) {
        u8 c = (i < 16) ? salt16[i] : salt16[i - 16];
        out[i] = (u8)(c ^ (u8)(0xA5 + i * 7));
    }
    // Fold in a fixed domain separator so the container salt alone is not
    // directly usable as the PRF input.
    static const u8 dom[11] = {'F','W','S','E','C','1','-','P','R','F','1'};
    for (int i = 0; i < 32; i++) out[i] ^= dom[i % 11];
}

} // namespace

bool webauthn_available(std::string& error) {
    WaApi& a = api();
    if (!a.dll) { error = "webauthn.dll 不可用 (需要 Windows 10 1903 或更新版本)"; return false; }
    if (!a.apiVersion || !a.makeCred || !a.getAssertion || !a.freeAssert) {
        error = "webauthn.dll 版本过旧，缺少 PRF 所需 API"; return false;
    }
    return true;
}

std::string webauthn_error_text(long hr) { return hr_text(hr); }

bool webauthn_make_credential(void* parent_hwnd, const std::string& rp_id,
                              const std::string& user_name, u8 device,
                              bool resident, std::vector<u8>& cred_id,
                              std::string& error) {
    WaApi& a = api();
    if (!webauthn_available(error)) return false;
    if (rp_id.empty() || user_name.empty()) { error = "rp_id / user_name 不能为空"; return false; }
    std::wstring wRpid = to_wide(rp_id);
    std::wstring wUser = to_wide(user_name);

    WEBAUTHN_RP_ENTITY_INFORMATION rp = {};
    rp.dwVersion = WEBAUTHN_RP_ENTITY_INFORMATION_CURRENT_VERSION;
    rp.pwszId = wRpid.c_str();
    rp.pwszName = wRpid.c_str();

    // Stable user id derived from the rp_id + user name.
    u8 user_id[32] = {0};
    const char* src = rp_id.c_str();
    for (size_t i = 0; src[i] && i < 64; i++) user_id[i % 32] ^= (u8)(src[i] + i);
    const char* src2 = user_name.c_str();
    for (size_t i = 0; src2[i] && i < 64; i++) user_id[(i + 7) % 32] ^= (u8)(src2[i] + i * 3);

    WEBAUTHN_USER_ENTITY_INFORMATION user = {};
    user.dwVersion = WEBAUTHN_USER_ENTITY_INFORMATION_CURRENT_VERSION;
    user.cbId = 32;
    user.pbId = user_id;
    user.pwszName = wUser.c_str();
    user.pwszDisplayName = wUser.c_str();

    WEBAUTHN_COSE_CREDENTIAL_PARAMETER cose = {};
    cose.dwVersion = WEBAUTHN_COSE_CREDENTIAL_PARAMETER_CURRENT_VERSION;
    cose.pwszCredentialType = WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY;
    cose.lAlg = WEBAUTHN_COSE_ALGORITHM_ECDSA_P256_WITH_SHA256;
    WEBAUTHN_COSE_CREDENTIAL_PARAMETERS params = { 1, &cose };

    // Client data JSON (challenge/origin are informational for offline use).
    std::string cj = "{\"type\":\"webauthn.create\",\"challenge\":\"rmr-fwsec\",\"origin\":\"https://rmr.local\"}";
    WEBAUTHN_CLIENT_DATA cd = {};
    cd.dwVersion = WEBAUTHN_CLIENT_DATA_CURRENT_VERSION;
    cd.cbClientDataJSON = (DWORD)cj.size();
    cd.pbClientDataJSON = (PBYTE)cj.data();
    cd.pwszHashAlgId = WEBAUTHN_HASH_ALGORITHM_SHA_256;

    WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS opt = {};
    opt.dwVersion = WEBAUTHN_AUTHENTICATOR_MAKE_CREDENTIAL_OPTIONS_CURRENT_VERSION;
    opt.dwTimeoutMilliseconds = 60000;
    opt.dwAuthenticatorAttachment = attachment_for(device);
    opt.bRequireResidentKey = resident ? TRUE : FALSE;
    opt.dwUserVerificationRequirement = (device == 2)
        ? WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED
        : WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED;
    opt.bEnablePrf = TRUE;

    PWEBAUTHN_CREDENTIAL_ATTESTATION att = nullptr;
    HRESULT hr = a.makeCred((HWND)parent_hwnd, &rp, &user, &params, &cd, &opt, &att);
    if (FAILED(hr)) {
        error = "创建凭据失败: " + hr_text(hr);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) error += " (用户取消)";
        return false;
    }
    if (!att || !att->pbCredentialId || att->cbCredentialId == 0) {
        if (att) a.freeAttest(att);
        error = "凭据返回异常"; return false;
    }
    cred_id.assign(att->pbCredentialId, att->pbCredentialId + att->cbCredentialId);
    a.freeAttest(att);
    return true;
}

bool webauthn_get_prf_secret(void* parent_hwnd, const std::string& rp_id,
                             const std::vector<u8>& cred_id, u8 device,
                             const u8 salt[32], u8 secret[32],
                             std::string& error) {
    WaApi& a = api();
    if (!webauthn_available(error)) return false;
    if (rp_id.empty() || cred_id.empty()) { error = "凭据信息缺失"; return false; }
    if (cred_id.size() > 512) { error = "凭据 ID 异常"; return false; }
    std::wstring wRpid = to_wide(rp_id);

    WEBAUTHN_CREDENTIAL_EX cred = {};
    cred.dwVersion = WEBAUTHN_CREDENTIAL_EX_CURRENT_VERSION;
    cred.cbId = (DWORD)cred_id.size();
    cred.pbId = (PBYTE)cred_id.data();
    cred.pwszCredentialType = WEBAUTHN_CREDENTIAL_TYPE_PUBLIC_KEY;
    PWEBAUTHN_CREDENTIAL_EX creds[1] = { &cred };
    WEBAUTHN_CREDENTIAL_LIST credList = { 1, creds };

    WEBAUTHN_HMAC_SECRET_SALT hs = {};
    hs.cbFirst = 32; hs.pbFirst = (PBYTE)salt;
    hs.cbSecond = 32; hs.pbSecond = (PBYTE)salt;
    WEBAUTHN_HMAC_SECRET_SALT_VALUES saltValues = { &hs, 0, nullptr };

    WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS opt = {};
    opt.dwVersion = WEBAUTHN_AUTHENTICATOR_GET_ASSERTION_OPTIONS_CURRENT_VERSION;
    opt.dwTimeoutMilliseconds = 60000;
    opt.dwAuthenticatorAttachment = attachment_for(device);
    opt.dwUserVerificationRequirement = (device == 2)
        ? WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED
        : WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED;
    opt.dwFlags = WEBAUTHN_AUTHENTICATOR_HMAC_SECRET_VALUES_FLAG;
    opt.pAllowCredentialList = &credList;
    opt.pHmacSecretSaltValues = &saltValues;

    PWEBAUTHN_ASSERTION assert = nullptr;
    HRESULT hr = a.getAssertion((HWND)parent_hwnd, wRpid.c_str(), &opt, &assert);
    if (FAILED(hr)) {
        error = "认证失败: " + hr_text(hr);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) error += " (用户取消)";
        return false;
    }
    bool ok = false;
    if (assert && assert->pHmacSecret && assert->pHmacSecret->pbFirst &&
        assert->pHmacSecret->cbFirst >= 32) {
        memcpy(secret, assert->pHmacSecret->pbFirst, 32);
        ok = true;
    } else {
        error = "安全密钥不支持 PRF/HMAC-Secret (需要 FIDO2 CTAP 2.1 PRF 或 hmac-secret)";
    }
    if (assert) a.freeAssert(assert);
    return ok;
}

void webauthn_prf_salt(const u8 salt16[16], u8 out[32]) { prf_salt_from(salt16, out); }

} // namespace fwsec