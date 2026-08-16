#include "fwsec_sha256.h"
namespace fwsec {
static const u32 K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static const u32 H0[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};

void Sha256::init() {
    for (int i = 0; i < 8; i++) h[i] = H0[i];
    len = 0; buflen = 0;
}
static void compress(Sha256& s, const u8* block) {
    u32 w[64];
    for (int i = 0; i < 16; i++) w[i] = load32_be(block + i * 4);
    for (int i = 16; i < 64; i++) {
        u32 s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15] >> 3);
        u32 s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u32 a=s.h[0],b=s.h[1],c=s.h[2],d=s.h[3],e=s.h[4],f=s.h[5],g=s.h[6],hh=s.h[7];
    for (int i = 0; i < 64; i++) {
        u32 S1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = hh + S1 + ch + K[i] + w[i];
        u32 S0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    s.h[0]+=a; s.h[1]+=b; s.h[2]+=c; s.h[3]+=d; s.h[4]+=e; s.h[5]+=f; s.h[6]+=g; s.h[7]+=hh;
}
void Sha256::update(const void* data, size_t n) {
    const u8* p = (const u8*)data;
    len += n;
    if (buflen) {
        size_t need = 64 - buflen;
        if (n < need) { memcpy(buf + buflen, p, n); buflen += n; return; }
        memcpy(buf + buflen, p, need); compress(*this, buf); p += need; n -= need; buflen = 0;
    }
    while (n >= 64) { compress(*this, p); p += 64; n -= 64; }
    if (n) { memcpy(buf, p, n); buflen = n; }
}
void Sha256::final(u8 out[32]) {
    u64 bits = len << 3;
    u8 pad = 0x80;
    update(&pad, 1);
    u8 z = 0;
    while (buflen != 56) update(&z, 1);
    u8 lb[8]; store64_be(lb, bits);
    update(lb, 8);
    for (int i = 0; i < 8; i++) store32_be(out + i*4, h[i]);
}
void sha256(const void* data, size_t len, u8 out[32]) {
    Sha256 s; s.init(); s.update(data, len); s.final(out);
}
void hmac_sha256(const u8* key, size_t keylen, const u8* msg, size_t msglen, u8 out[32]) {
    u8 k[64];
    if (keylen > 64) { sha256(key, keylen, k); memset(k + 32, 0, 32); }
    else { memcpy(k, key, keylen); memset(k + keylen, 0, 64 - keylen); }
    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha256 s; s.init(); s.update(ipad, 64); s.update(msg, msglen); u8 ih[32]; s.final(ih);
    s.init(); s.update(opad, 64); s.update(ih, 32); s.final(out);
    secure_zero(k, 64); secure_zero(ipad, 64); secure_zero(opad, 64);
}
void hkdf_sha256(const u8* ikm, size_t ikmlen, const u8* salt, size_t saltlen,
                 const u8* info, size_t infolen, u8* okm, size_t okmlen) {
    u8 prk[32];
    hmac_sha256(salt ? salt : (const u8*)"", salt ? saltlen : 0, ikm, ikmlen, prk);
    u8 t[32 + 255]; size_t tlen = 0; u8 ctr = 1; size_t pos = 0;
    while (pos < okmlen) {
        u8* block = t + (ctr == 1 ? 0 : 32);
        size_t blen = (ctr == 1 ? 0 : 32);
        if (infolen) { memcpy(t + blen, info, infolen); blen += infolen; }
        t[blen++] = ctr;
        hmac_sha256(prk, 32, t, blen, t);
        size_t take = okmlen - pos < 32 ? okmlen - pos : 32;
        memcpy(okm + pos, t, take); pos += take; ctr++;
        (void)tlen;
    }
    secure_zero(prk, 32);
}
}
