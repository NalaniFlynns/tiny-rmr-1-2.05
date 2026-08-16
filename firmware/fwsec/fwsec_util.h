#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace fwsec {
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

inline u32 load32_le(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
inline u64 load64_le(const u8* p) {
    return (u64)load32_le(p) | ((u64)load32_le(p + 4) << 32);
}
inline u32 load32_be(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}
inline u64 load64_be(const u8* p) {
    return ((u64)load32_be(p) << 32) | load32_be(p + 4);
}
inline void store32_le(u8* p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}
inline void store64_le(u8* p, u64 v) {
    store32_le(p, (u32)v); store32_le(p + 4, (u32)(v >> 32));
}
inline void store32_be(u8* p, u32 v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}
inline void store64_be(u8* p, u64 v) {
    store32_be(p, (u32)(v >> 32)); store32_be(p + 4, (u32)v);
}
inline u64 rotr64(u64 x, unsigned c) { return (x >> c) | (x << (64 - c)); }
inline u32 rotr32(u32 x, unsigned c) { return (x >> c) | (x << (32 - c)); }

/* Best-effort secure zeroization (not optimized away in release builds). */
inline void secure_zero(void* p, size_t n) {
    volatile u8* vp = static_cast<volatile u8*>(p);
    while (n--) *vp++ = 0;
}
} // namespace fwsec
