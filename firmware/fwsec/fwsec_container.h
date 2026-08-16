#pragma once
// FWSEC1 container: post-quantum encrypted firmware container.
// Format: magic "FWSEC1" | ver u16 | auth u8 | resv u8 | hdr_len u32 |
//         salt[16] | argon2(t,m,p) | kem_ct[1088] | wrapped_kem_sk(2400+16) |
//         name_len u16 | name | blocks u64 | block_size u32 | total u64 |
//         block stream (per block: ct + tag16) | mac[32]
#include <cstdint>
#include <string>
#include <vector>
#include "fwsec_util.h"

namespace fwsec {

enum AuthType : u8 {
    AUTH_PASSWORD = 1,
    AUTH_FIDO2    = 2,
    AUTH_PASSKEY  = 3,
};

struct FwsecFileInfo {
    u16 version = 1;
    u8  auth_type = AUTH_PASSWORD;
    std::string file_name;
    u64 blocks = 0;
    u32 block_size = 0;
    u64 total_size = 0;
    u8  salt[16] = {0};
    u32 ar2_t = 0, ar2_m = 0, ar2_p = 0;
    u8  device = 0;                // FIDO2: 1=cross-platform key, 2=platform (Windows Hello)
    std::vector<u8> cred_id;       // FIDO2 credential id
    std::string rp_id;             // FIDO2 relying party id
    std::string file_version;      // firmware version string
    std::string update_note;       // firmware update note
    std::string build_meta;        // build metadata (date / crypto alg version / yubico id)
    bool ok = false;
    std::string error;
};

// Encryption options; exactly one unlock source must be set.
struct FwsecEncryptOptions {
    u8 auth_type = AUTH_PASSWORD;
    std::string password;            // for AUTH_PASSWORD
    std::vector<u8> fido_secret;     // 32 bytes for AUTH_FIDO2 / AUTH_PASSKEY
    u8 device = 0;                   // for AUTH_FIDO2 / AUTH_PASSKEY
    std::vector<u8> cred_id;         // for AUTH_FIDO2 / AUTH_PASSKEY
    std::string rp_id;               // for AUTH_FIDO2 / AUTH_PASSKEY
    u8 salt[16] = {0};               // optional pre-generated container salt
    bool use_salt = false;           // true: use salt above instead of random
    std::string file_version;        // firmware version (embedded in container)
    std::string update_note;         // update note (embedded in container)
    std::string build_meta;          // build metadata: date / alg version / yubico id
    std::string file_name = "RMR.hex";
    u32 block_size = 16384;
    u32 ar2_t = 3, ar2_m = 65536, ar2_p = 1;  // Argon2id 64 MiB
    bool (*progress)(void* ctx, u64 done, u64 total) = nullptr;
    void* progress_ctx = nullptr;
};

// Probe a container without unlocking it.
bool fwsec_probe(const std::string& path, FwsecFileInfo& info);

// Encrypt a plaintext file into a FWSEC1 container.
bool fwsec_encrypt_file(const std::string& in_path, const std::string& out_path,
                        const FwsecEncryptOptions& opt, std::string& error);

// Decrypt with a credential: password string (AUTH_PASSWORD) or
// 32-byte fido/passkey secret (AUTH_FIDO2/AUTH_PASSKEY).
bool fwsec_decrypt_file(const std::string& in_path, const std::string& out_path,
                        const std::string& password,
                        const std::vector<u8>& fido_secret,
                        bool (*progress)(void* ctx, u64 done, u64 total),
                        void* progress_ctx,
                        std::string& error);

} // namespace fwsec
