// fwsec_test.cpp - self-test for fwsec primitives
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "fwsec_sha256.h"
#include "fwsec_blake2b.h"
#include "fwsec_aead.h"
#include "fwsec_argon2.h"
#include "fwsec_mlkem.h"

using namespace fwsec;

static int failures = 0;
static void check(bool ok, const char* name) {
    if (ok) { printf("PASS %s\n", name); }
    else { printf("FAIL %s\n", name); failures++; }
}
static std::string hex(const u8* d, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s; s.reserve(n*2);
    for (size_t i=0;i<n;i++){ s.push_back(H[d[i]>>4]); s.push_back(H[d[i]&15]); }
    return s;
}
static std::vector<u8> unhex(const char* s) {
    std::vector<u8> v; size_t n = strlen(s);
    auto cv = [](char c)->u8 { return (c<='9')?(u8)(c-'0'):(u8)(c-'a'+10); };
    for (size_t i=0;i+1<n;i+=2) v.push_back((u8)((cv(s[i])<<4)|cv(s[i+1])));
    return v;
}

int main() {
    // SHA-256: "abc"
    {
        u8 out[32];
        sha256("abc", 3, out);
        check(hex(out,32)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
    }
    // SHA-256 empty
    {
        u8 out[32];
        sha256("", 0, out);
        check(hex(out,32)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
    }
    // HMAC-SHA256 RFC 4231 test case 2 (key 20 bytes 0x0b, data "Hi There")
    {
        u8 key[20]; memset(key,0x0b,20);
        u8 out[32];
        hmac_sha256(key,20,(const u8*)"Hi There",8,out);
        check(hex(out,32)=="b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "hmac-sha256 rfc4231#2");
    }
    // BLAKE2b-512 "abc"
    {
        u8 out[64];
        blake2b("abc",3,out,64);
        check(hex(out,64)=="ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923", "blake2b-512 abc");
    }
    // BLAKE2b-256 "abc"
    {
        u8 out[32];
        blake2b("abc",3,out,32);
        check(hex(out,32)=="bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319", "blake2b-256 abc");
    }
    // ChaCha20-Poly1305 RFC 8439
    {
        auto key = unhex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
        auto nonce = unhex("070000004041424344454647");
        auto aad = unhex("50515253c0c1c2c3c4c5c6c7");
        const char* pt = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
        u8 ct[114], tag[16], rt[114];
        aead_chacha20poly1305_encrypt(key.data(), nonce.data(), aad.data(), aad.size(),
                                      (const u8*)pt, strlen(pt), ct, tag);
        bool enc_ok = hex(tag,16)=="1ae10b594f09e26a7e902ecbd0600691";
        check(enc_ok, "chacha20poly1305 rfc8439 tag");
        bool dec_ok = aead_chacha20poly1305_decrypt(key.data(), nonce.data(), aad.data(), aad.size(),
                                                     ct, strlen(pt), tag, rt);
        check(dec_ok && memcmp(rt, pt, strlen(pt))==0, "chacha20poly1305 roundtrip");
        // tamper
        u8 badtag[16]; memcpy(badtag,tag,16); badtag[0]^=1;
        bool rej = !aead_chacha20poly1305_decrypt(key.data(), nonce.data(), aad.data(), aad.size(),
                                                   ct, strlen(pt), badtag, rt);
        check(rej, "chacha20poly1305 tamper rejected");
    }
    // Argon2id RFC 9106 4.2 (t=3, m=32, p=4)
    {
        u8 out[32];
        bool ok = argon2id_hash(3, 32, 4, (const u8*)"0123456789abcdef",16, (const u8*)"0123456789abcdef",16, out,32);
        check(ok && hex(out,32)=="2c0d0fe10ccf34de321dcb6d63dd5c4b724d4d805efa66a34c646753c12e131a", "argon2id t3m32p4 (official ref)");
    }
    // Argon2id RFC 9106 4.2 (t=2, m=16, p=1)
    {
        u8 out[32];
        bool ok = argon2id_hash(2, 16, 1, (const u8*)"password",8, (const u8*)"somesalt",8, out,32);
        check(ok && hex(out,32)=="058202c0723cd88c24408ccac1cbf828dee63bcf3843a150ea364a1e0b4e1ff8", "argon2id t2m16p1 (official ref)");
    }
    // ML-KEM deterministic cross-check (fixed seeds, verified against Python cryptography)
    {
        u8 coins[64], coins2[32], pk[FWSEC_MLKEM_PK_SIZE], sk[FWSEC_MLKEM_SK_SIZE];
        u8 ct[FWSEC_MLKEM_CT_SIZE], ss[FWSEC_MLKEM_SS_SIZE];
        for (int i = 0; i < 64; i++) coins[i] = (u8)(i * 7 + 1);
        for (int i = 0; i < 32; i++) coins2[i] = (u8)(0xA0 + i);
        check(PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair_derand(pk, sk, coins) == 0, "mlkem keypair_derand");
        check(PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc_derand(ct, ss, pk, coins2) == 0, "mlkem enc_derand");
        u8 ss2[FWSEC_MLKEM_SS_SIZE];
        check(PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(ss2, ct, sk) == 0 && memcmp(ss, ss2, 32) == 0, "mlkem derand decaps match");
        FILE* f = fopen("mlkem_cross.bin", "wb");
        if (f) { fwrite(pk, 1, sizeof(pk), f); fwrite(sk, 1, sizeof(sk), f); fwrite(ct, 1, sizeof(ct), f); fwrite(ss, 1, sizeof(ss), f); fclose(f); }
    }
    // ML-KEM roundtrip
    {
        u8 pk[FWSEC_MLKEM_PK_SIZE], sk[FWSEC_MLKEM_SK_SIZE];
        u8 ct[FWSEC_MLKEM_CT_SIZE], ss1[FWSEC_MLKEM_SS_SIZE], ss2[FWSEC_MLKEM_SS_SIZE];
        check(fwsec_mlkem_keypair(pk,sk)==0, "mlkem keypair");
        check(fwsec_mlkem_encaps(ct, ss1, pk)==0, "mlkem encaps");
        check(fwsec_mlkem_decaps(ss2, ct, sk)==0 && memcmp(ss1,ss2,32)==0, "mlkem decaps match");
        printf("MLKEM pk=%zu ct=%zu ss=%s\n", sizeof(pk), sizeof(ct), hex(ss1,8).c_str());
        // dump for python cross-check
        FILE* f = fopen("mlkem_cross.bin","wb");
        if (f) { fwrite(pk,1,sizeof(pk),f); fwrite(sk,1,sizeof(sk),f); fwrite(ct,1,sizeof(ct),f); fwrite(ss1,1,sizeof(ss1),f); fclose(f); }
    }
    printf(failures==0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures==0?0:1;
}
