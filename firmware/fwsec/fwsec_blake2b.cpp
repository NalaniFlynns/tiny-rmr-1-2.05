#include "fwsec_blake2b.h"
namespace fwsec {
static const u64 IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};
static const u8 SIGMA[12][16] = {
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
    {11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},
    {7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
    {9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},
    {2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
    {12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},
    {13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
    {6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},
    {10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0},
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3}
};
static void G(Blake2b& s, int r, int i, u64& a, u64& b, u64& c, u64& d, const u64* m) {
    (void)s;
    a = a + b + m[SIGMA[r][2*i+0]]; d = rotr64(d ^ a, 32);
    c = c + d; b = rotr64(b ^ c, 24);
    a = a + b + m[SIGMA[r][2*i+1]]; d = rotr64(d ^ a, 16);
    c = c + d; b = rotr64(b ^ c, 63);
}
static void roundFn(Blake2b& s, const u8* block, bool last) {
    u64 m[16], v[16];
    for (int i = 0; i < 16; i++) m[i] = load64_le(block + i*8);
    for (int i = 0; i < 8; i++) v[i] = s.h[i];
    v[8] = IV[0]; v[9] = IV[1]; v[10] = IV[2]; v[11] = IV[3];
    v[12] = IV[4] ^ s.t[0]; v[13] = IV[5] ^ s.t[1];
    v[14] = IV[6] ^ (last ? ~0ULL : 0ULL); v[15] = IV[7];
    for (int r = 0; r < 12; r++) {
        G(s, r, 0, v[0],v[4],v[8],v[12], m);
        G(s, r, 1, v[1],v[5],v[9],v[13], m);
        G(s, r, 2, v[2],v[6],v[10],v[14], m);
        G(s, r, 3, v[3],v[7],v[11],v[15], m);
        G(s, r, 4, v[0],v[5],v[10],v[15], m);
        G(s, r, 5, v[1],v[6],v[11],v[12], m);
        G(s, r, 6, v[2],v[7],v[8],v[13], m);
        G(s, r, 7, v[3],v[4],v[9],v[14], m);
    }
    for (int i = 0; i < 8; i++) s.h[i] ^= v[i] ^ v[i+8];
}
static void incr(Blake2b& s, size_t n) {
    s.t[0] += n;
    if (s.t[0] < n) s.t[1]++;
}
void Blake2b::init(size_t outlen_) {
    outlen = outlen_;
    for (int i = 0; i < 8; i++) h[i] = IV[i];
    h[0] ^= 0x01010000ULL ^ (u64)outlen;
    t[0] = t[1] = 0; buflen = 0;
}
void Blake2b::update(const void* data, size_t n) {
    const u8* p = (const u8*)data;
    if (buflen) {
        size_t need = 128 - buflen;
        if (n < need) { memcpy(buf + buflen, p, n); buflen += n; return; }
        memcpy(buf + buflen, p, need); incr(*this, 128); roundFn(*this, buf, false);
        p += need; n -= need; buflen = 0;
    }
    while (n >= 128) { incr(*this, 128); roundFn(*this, p, false); p += 128; n -= 128; }
    if (n) { memcpy(buf, p, n); buflen = n; }
}
void Blake2b::final(u8* out) {
    incr(*this, buflen);
    u8 last[128];
    memcpy(last, buf, 128);
    memset(last + buflen, 0, 128 - buflen);
    roundFn(*this, last, true);
    for (int i = 0; i < (int)outlen / 8; i++) store64_le(out + i*8, h[i]);
}
void blake2b(const void* data, size_t len, u8* out, size_t outlen) {
    Blake2b b; b.init(outlen); b.update(data, len); b.final(out);
}
}
