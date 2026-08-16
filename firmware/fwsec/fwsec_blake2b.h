#pragma once
#include "fwsec_util.h"
namespace fwsec {
struct Blake2b {
    u64 h[8]; u64 t[2]; u8 buf[128]; size_t buflen; size_t outlen;
    void init(size_t outlen_ = 64);
    void update(const void* data, size_t len);
    void final(u8* out);
};
void blake2b(const void* data, size_t len, u8* out, size_t outlen);
}