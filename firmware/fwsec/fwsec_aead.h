#pragma once
#include "fwsec_util.h"
namespace fwsec {
/* RFC 8439 */
void chacha20_block(const u8 key[32], u32 counter, const u8 nonce[12], u8 out[64]);
void chacha20_xor(const u8 key[32], u32 counter, const u8 nonce[12], const u8* in, u8* out, size_t len);
void poly1305(const u8 key[32], const u8* msg, size_t msglen, u8 tag[16]);
void aead_chacha20poly1305_encrypt(const u8 key[32], const u8 nonce[12],
                                   const u8* aad, size_t aadlen,
                                   const u8* in, size_t inlen, u8* out, u8 tag[16]);
bool aead_chacha20poly1305_decrypt(const u8 key[32], const u8 nonce[12],
                                   const u8* aad, size_t aadlen,
                                   const u8* in, size_t inlen, const u8 tag[16], u8* out);
}