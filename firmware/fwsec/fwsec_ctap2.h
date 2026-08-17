#pragma once
// Direct CTAP2/HID transport for FIDO2 security keys (bypasses Windows
// webauthn.dll, which refuses PRF/hmac-secret on non-resident credentials).
// Uses the classic hmac-secret extension with a non-resident credential:
// touch-only, no PIN required.
#include <cstdint>
#include <string>
#include <vector>
#include "fwsec_util.h"

namespace fwsec {

// Optional status callback, invoked while waiting for user presence.
typedef void (*Ctap2StatusCb)(const char* message, void* user);
void ctap2_set_status_cb(Ctap2StatusCb cb, void* user);

// Returns true if at least one FIDO2 HID security key is present.
bool ctap2_available(std::string& error);

// Creates a non-resident credential with the hmac-secret extension enabled.
// Requires user touch on the key.
bool ctap2_make_credential(const std::string& rp_id, const std::string& user_name,
                           std::vector<u8>& cred_id, std::vector<u8>& aaguid,
                           std::string& error);

// getAssertion with allowList + hmac-secret salt -> 32-byte output.
// Requires user touch on the key.
bool ctap2_get_hmac_secret(const std::string& rp_id, const std::vector<u8>& cred_id,
                           const u8 salt[32], u8 secret[32], std::string& error);

} // namespace fwsec
