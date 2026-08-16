#include "fwsec_aead.h"
namespace fwsec {

/* ---------------- ChaCha20 ---------------- */
static u32 rotl32(u32 x, unsigned c) { return (x << c) | (x >> (32 - c)); }
static void qr(u32& a, u32& b, u32& c, u32& d) {
    a += b; d ^= a; d = rotl32(d, 16);
    c += d; b ^= c; b = rotl32(b, 12);
    a += b; d ^= a; d = rotl32(d, 8);
    c += d; b ^= c; b = rotl32(b, 7);
}
void chacha20_block(const u8 key[32], u32 counter, const u8 nonce[12], u8 out[64]) {
    u32 st[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        load32_le(key), load32_le(key+4), load32_le(key+8), load32_le(key+12),
        load32_le(key+16), load32_le(key+20), load32_le(key+24), load32_le(key+28),
        counter, load32_le(nonce), load32_le(nonce+4), load32_le(nonce+8)
    };
    u32 x[16];
    memcpy(x, st, sizeof(st));
    for (int i = 0; i < 10; i++) {
        qr(x[0],x[4],x[8],x[12]); qr(x[1],x[5],x[9],x[13]); qr(x[2],x[6],x[10],x[14]); qr(x[3],x[7],x[11],x[15]);
        qr(x[0],x[5],x[10],x[15]); qr(x[1],x[6],x[11],x[12]); qr(x[2],x[7],x[8],x[13]); qr(x[3],x[4],x[9],x[14]);
    }
    for (int i = 0; i < 16; i++) store32_le(out + i*4, x[i] + st[i]);
}
void chacha20_xor(const u8 key[32], u32 counter, const u8 nonce[12], const u8* in, u8* out, size_t len) {
    u8 blk[64];
    size_t pos = 0;
    while (pos < len) {
        chacha20_block(key, counter++, nonce, blk);
        size_t take = len - pos < 64 ? len - pos : 64;
        for (size_t i = 0; i < take; i++) out[pos + i] = in[pos + i] ^ blk[i];
        pos += take;
    }
    secure_zero(blk, 64);
}

/* ---------------- Poly1305 ---------------- */
void poly1305(const u8 key[32], const u8* msg, size_t msglen, u8 tag[16]) {
    u32 r0 = load32_le(key) & 0x3ffffff;
    u32 r1 = (load32_le(key + 3) >> 2) & 0x3ffff03;
    u32 r2 = (load32_le(key + 6) >> 4) & 0x3ffc0ff;
    u32 r3 = (load32_le(key + 9) >> 6) & 0x3f03fff;
    u32 r4 = (load32_le(key + 12) >> 8) & 0x00fffff;
    u32 h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    u64 d0, d1, d2, d3, d4;
    u32 c;
    u32 s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    size_t i = 0;
    while (msglen >= 16) {
        h0 += load32_le(msg) & 0x3ffffff;
        h1 += (load32_le(msg + 3) >> 2) & 0x3ffffff;
        h2 += (load32_le(msg + 6) >> 4) & 0x3ffffff;
        h3 += (load32_le(msg + 9) >> 6) & 0x3ffffff;
        h4 += (load32_le(msg + 12) >> 8) | (1 << 24);
        /* h *= r */
        d0 = (u64)h0*r0 + (u64)h1*s4 + (u64)h2*s3 + (u64)h3*s2 + (u64)h4*s1;
        d1 = (u64)h0*r1 + (u64)h1*r0 + (u64)h2*s4 + (u64)h3*s3 + (u64)h4*s2;
        d2 = (u64)h0*r2 + (u64)h1*r1 + (u64)h2*r0 + (u64)h3*s4 + (u64)h4*s3;
        d3 = (u64)h0*r3 + (u64)h1*r2 + (u64)h2*r1 + (u64)h3*r0 + (u64)h4*s4;
        d4 = (u64)h0*r4 + (u64)h1*r3 + (u64)h2*r2 + (u64)h3*r1 + (u64)h4*r0;
        h0 = d0 & 0x3ffffff; d1 += d0 >> 26;
        h1 = d1 & 0x3ffffff; d2 += d1 >> 26;
        h2 = d2 & 0x3ffffff; d3 += d2 >> 26;
        h3 = d3 & 0x3ffffff; d4 += d3 >> 26;
        h4 = d4 & 0x3ffffff; h0 += (d4 >> 26) * 5;
        h1 += h0 >> 26; h0 &= 0x3ffffff;
        msg += 16; msglen -= 16;
    }
    if (msglen) {
        u8 block[16] = {0};
        memcpy(block, msg, msglen);
        block[msglen] = 1;
        h0 += load32_le(block) & 0x3ffffff;
        h1 += (load32_le(block + 3) >> 2) & 0x3ffffff;
        h2 += (load32_le(block + 6) >> 4) & 0x3ffffff;
        h3 += (load32_le(block + 9) >> 6) & 0x3ffffff;
        h4 += (load32_le(block + 12) >> 8);
        /* h *= r */
        d0 = (u64)h0*r0 + (u64)h1*s4 + (u64)h2*s3 + (u64)h3*s2 + (u64)h4*s1;
        d1 = (u64)h0*r1 + (u64)h1*r0 + (u64)h2*s4 + (u64)h3*s3 + (u64)h4*s2;
        d2 = (u64)h0*r2 + (u64)h1*r1 + (u64)h2*r0 + (u64)h3*s4 + (u64)h4*s3;
        d3 = (u64)h0*r3 + (u64)h1*r2 + (u64)h2*r1 + (u64)h3*r0 + (u64)h4*s4;
        d4 = (u64)h0*r4 + (u64)h1*r3 + (u64)h2*r2 + (u64)h3*r1 + (u64)h4*r0;
        h0 = d0 & 0x3ffffff; d1 += d0 >> 26;
        h1 = d1 & 0x3ffffff; d2 += d1 >> 26;
        h2 = d2 & 0x3ffffff; d3 += d2 >> 26;
        h3 = d3 & 0x3ffffff; d4 += d3 >> 26;
        h4 = d4 & 0x3ffffff; h0 += (d4 >> 26) * 5;
        h1 += h0 >> 26; h0 &= 0x3ffffff;
    }
    /* fully carry h */
    c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
    /* compute h + -p */
    u32 g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    u32 g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    u32 g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    u32 g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    u32 g4 = h4 + c - (1 << 26);
    /* select h if h < p, or h + -p if h >= p */
    u32 mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;
    /* h = h % 2^128 */
    h0 = (h0 | (h1 << 26)) & 0xffffffff;
    h1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffff;
    h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffff;
    h3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffff;
    /* mac = (h + pad) % 2^128 */
    u64 f = (u64)h0 + load32_le(key + 16); h0 = (u32)f;
    f = (u64)h1 + load32_le(key + 20) + (f >> 32); h1 = (u32)f;
    f = (u64)h2 + load32_le(key + 24) + (f >> 32); h2 = (u32)f;
    f = (u64)h3 + load32_le(key + 28) + (f >> 32); h3 = (u32)f;
    store32_le(tag, h0); store32_le(tag + 4, h1);
    store32_le(tag + 8, h2); store32_le(tag + 12, h3);
}

/* ---------------- AEAD ---------------- */
void aead_chacha20poly1305_encrypt(const u8 key[32], const u8 nonce[12],
                                   const u8* aad, size_t aadlen,
                                   const u8* in, size_t inlen, u8* out, u8 tag[16]) {
    u8 polykey[64];
    chacha20_block(key, 0, nonce, polykey);
    u8 subkey[32];
    memcpy(subkey, polykey, 32);
    chacha20_xor(key, 1, nonce, in, out, inlen);
    u8 macbuf[16 + 8] = {0};
    size_t pos = 0;
    auto pad16 = [&](size_t n) {
        size_t rem = n % 16;
        if (rem) pos += 16 - rem;
    };
    if (aadlen) { memcpy(macbuf, aad, aadlen > 16 ? 16 : aadlen); pos = aadlen; }
    else pos = 0;
    pad16(aadlen);
    size_t start = pos;
    (void)start;
    /* stream MAC input: aad || pad || ct || pad || le64(aadlen) || le64(ctlen) */
    /* Build incrementally to avoid unbounded buffer: use a small chunked approach. */
    /* We implement with a simple streaming poly1305 via two passes is complex;
       instead accumulate lengths and use a heap buffer if needed. */
    size_t maclen = ((aadlen + 15) / 16) * 16 + ((inlen + 15) / 16) * 16 + 16;
    u8* mac = new u8[maclen];
    size_t mp = 0;
    if (aadlen) { memcpy(mac + mp, aad, aadlen); mp += aadlen; }
    size_t rem = aadlen % 16;
    if (rem) { memset(mac + mp, 0, 16 - rem); mp += 16 - rem; }
    if (inlen) { memcpy(mac + mp, out, inlen); mp += inlen; }
    rem = inlen % 16;
    if (rem) { memset(mac + mp, 0, 16 - rem); mp += 16 - rem; }
    u8 l[8]; store64_le(l, aadlen); memcpy(mac + mp, l, 8); mp += 8;
    store64_le(l, inlen); memcpy(mac + mp, l, 8); mp += 8;
    poly1305(subkey, mac, mp, tag);
    delete[] mac;
    secure_zero(subkey, 32); secure_zero(polykey, 64);
}
bool aead_chacha20poly1305_decrypt(const u8 key[32], const u8 nonce[12],
                                   const u8* aad, size_t aadlen,
                                   const u8* in, size_t inlen, const u8 tag[16], u8* out) {
    u8 polykey[64];
    chacha20_block(key, 0, nonce, polykey);
    u8 subkey[32];
    memcpy(subkey, polykey, 32);
    size_t maclen = ((aadlen + 15) / 16) * 16 + ((inlen + 15) / 16) * 16 + 16;
    u8* mac = new u8[maclen];
    size_t mp = 0;
    if (aadlen) { memcpy(mac + mp, aad, aadlen); mp += aadlen; }
    size_t rem = aadlen % 16;
    if (rem) { memset(mac + mp, 0, 16 - rem); mp += 16 - rem; }
    if (inlen) { memcpy(mac + mp, in, inlen); mp += inlen; }
    rem = inlen % 16;
    if (rem) { memset(mac + mp, 0, 16 - rem); mp += 16 - rem; }
    u8 l[8]; store64_le(l, aadlen); memcpy(mac + mp, l, 8); mp += 8;
    store64_le(l, inlen); memcpy(mac + mp, l, 8); mp += 8;
    u8 t[16];
    poly1305(subkey, mac, mp, t);
    delete[] mac;
    secure_zero(subkey, 32); secure_zero(polykey, 64);
    u8 diff = 0;
    for (int i = 0; i < 16; i++) diff |= t[i] ^ tag[i];
    if (diff) return false;
    chacha20_xor(key, 1, nonce, in, out, inlen);
    return true;
}
}

