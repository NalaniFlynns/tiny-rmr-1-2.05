#pragma once
// FIDO2 / Windows WebAuthn PRF helper (Windows 10 1903+ webauthn.dll).
// The credential is created with the PRF extension; the 32-byte PRF output
// is derived inside the authenticator and never leaves the hardware.
#include <cstdint>
#include <string>
#include <vector>
#include "fwsec_util.h"

namespace fwsec {

// device: 0 = any, 1 = cross-platform (FIDO2 security key), 2 = platform (Windows Hello / passkey)
bool webauthn_available(std::string& error);
bool webauthn_make_credential(void* parent_hwnd, const std::string& rp_id,
                              const std::string& user_name, u8 device,
                              bool resident, std::vector<u8>& cred_id,
                              std::string& error);
bool webauthn_get_prf_secret(void* parent_hwnd, const std::string& rp_id,
                             const std::vector<u8>& cred_id, u8 device,
                             const u8 salt[32], u8 secret[32],
                             std::string& error);
std::string webauthn_error_text(long hr);

// Derive the 32-byte raw PRF salt from the 16-byte container salt.
void webauthn_prf_salt(const u8 salt16[16], u8 out[32]);

} // namespace fwsec