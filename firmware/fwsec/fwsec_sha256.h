#pragma once
#include "fwsec_util.h"
namespace fwsec {
struct Sha256 {
    u32 h[8]; u64 len; u8 buf[64]; size_t buflen;
    void init();
    void update(const void* data, size_t len);
    void final(u8 out[32]);
};
void sha256(const void* data, size_t len, u8 out[32]);
void hmac_sha256(const u8* key, size_t keylen, const u8* msg, size_t msglen, u8 out[32]);
void hkdf_sha256(const u8* ikm, size_t ikmlen, const u8* salt, size_t saltlen,
                 const u8* info, size_t infolen, u8* okm, size_t okmlen);
}