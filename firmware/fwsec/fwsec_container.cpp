#include "fwsec_container.h"
#include "fwsec_sha256.h"
#include "fwsec_aead.h"
#include "fwsec_argon2.h"
#include "fwsec_mlkem.h"
#include "mlkem/randombytes.h"
#include <cstdio>
#include <cstring>

namespace fwsec {

namespace {
constexpr u32 MAGIC_LEN = 6;
constexpr char MAGIC[6] = {'F','W','S','E','C','1'};
constexpr u32 HDR_FIXED = 6 + 2 + 1 + 1 + 4 + 16 + 12 + FWSEC_MLKEM_CT_SIZE + (FWSEC_MLKEM_SK_SIZE + 16) + 2;
constexpr size_t BLOCK_TAG = 16;
constexpr size_t FILE_MAC_LEN = 32;

static bool read_exact(FILE* f, void* buf, size_t n) { return fread(buf, 1, n, f) == n; }
static bool write_exact(FILE* f, const void* buf, size_t n) { return fwrite(buf, 1, n, f) == n; }

static void store_u16(u8* p, u16 v) { p[0]=(u8)v; p[1]=(u8)(v>>8); }
static void store_u32(u8* p, u32 v) { store32_le(p, v); }
static void store_u64(u8* p, u64 v) { store64_le(p, v); }
static u16 load_u16(const u8* p) { return (u16)(p[0] | (p[1]<<8)); }
static u32 load_u32(const u8* p) { return load32_le(p); }
static u64 load_u64(const u8* p) { return load64_le(p); }

// Derive the 32-byte credential unlock key.
static bool derive_unlock_key(const FwsecFileInfo& info, const std::string& password,
                              const std::vector<u8>& fido_secret, u8 out[32]) {
    if (info.auth_type == AUTH_PASSWORD) {
        return argon2id_hash(info.ar2_t, info.ar2_m, info.ar2_p,
                             (const u8*)password.data(), password.size(),
                             info.salt, sizeof(info.salt), out, 32);
    }
    if (fido_secret.size() == 32) {
        // HMAC-secret style: derive from the 32-byte secret with the file salt.
        hkdf_sha256(fido_secret.data(), 32, info.salt, sizeof(info.salt),
                    (const u8*)"FWSEC1-FIDO", 11, out, 32);
        return true;
    }
    return false;
}

static bool aead_wrap(const u8 key[32], const u8* plain, size_t plen,
                      const char* aad, u8* out, u8 tag[16]) {
    aead_chacha20poly1305_encrypt(key, (const u8*)"\0\0\0\0\0\0\0\0\0\0\0\0", (const u8*)aad, strlen(aad),
                                  plain, plen, out, tag);
    return true;
}
static bool aead_unwrap(const u8 key[32], const u8* in, size_t inlen,
                        const char* aad, const u8 tag[16], u8* out) {
    return aead_chacha20poly1305_decrypt(key, (const u8*)"\0\0\0\0\0\0\0\0\0\0\0\0", (const u8*)aad, strlen(aad),
                                         in, inlen, tag, out);
}

static bool parse_ext_blob(const std::vector<u8>& ext, FwsecFileInfo& info, std::string& err) {
    const u8* q = ext.data();
    size_t remain = ext.size();
    if (info.auth_type != AUTH_PASSWORD) {
        if (remain < 5) { err = "bad ext data"; return false; }
        info.device = *q++; remain -= 1;
        u16 clen = load_u16(q); q += 2; remain -= 2;
        if (clen > 512 || remain < clen) { err = "bad ext data"; return false; }
        info.cred_id.assign(q, q + clen); q += clen; remain -= clen;
        u16 rlen = load_u16(q); q += 2; remain -= 2;
        if (rlen > 256 || remain < rlen) { err = "bad ext data"; return false; }
        info.rp_id.assign((char*)q, rlen); q += rlen; remain -= rlen;
    }
    std::string* fields[3] = { &info.file_version, &info.update_note, &info.build_meta };
    for (auto* f : fields) {
        if (remain == 0) break;
        if (remain < 2) { err = "bad ext data"; return false; }
        u16 l = load_u16(q); q += 2; remain -= 2;
        if (l > 512 || remain < l) { err = "bad ext data"; return false; }
        f->assign((const char*)q, l); q += l; remain -= l;
    }
    return true;
}
} // namespace

bool fwsec_probe(const std::string& path, FwsecFileInfo& info) {
    info = FwsecFileInfo{};
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { info.error = "cannot open file"; return false; }
    u8 hdr[HDR_FIXED + 512];
    if (!read_exact(f, hdr, MAGIC_LEN)) { fclose(f); info.error = "truncated header"; return false; }
    if (memcmp(hdr, MAGIC, MAGIC_LEN) != 0) { fclose(f); info.error = "not a FWSEC1 file"; return false; }
    u8 rest[HDR_FIXED - MAGIC_LEN];
    if (!read_exact(f, rest, sizeof(rest))) { fclose(f); info.error = "truncated header"; return false; }
    const u8* p = rest;
    info.version = load_u16(p); p += 2;
    info.auth_type = *p++;
    u8 resv = *p++;
    (void)resv;
    u32 hdr_len = load_u32(p); p += 4;
    if ((info.version != 1 && info.version != 2) || hdr_len != HDR_FIXED) { fclose(f); info.error = "unsupported container version"; return false; }
    memcpy(info.salt, p, 16); p += 16;
    info.ar2_t = load_u32(p); p += 4;
    info.ar2_m = load_u32(p); p += 4;
    info.ar2_p = load_u32(p); p += 4;
    p += FWSEC_MLKEM_CT_SIZE;          // kem ct
    p += FWSEC_MLKEM_SK_SIZE + 16;     // wrapped sk + tag
    u16 nlen = load_u16(p); p += 2;
    if (nlen > 256) { fclose(f); info.error = "bad name length"; return false; }
    u8 namebuf[256];
    if (!read_exact(f, namebuf, nlen)) { fclose(f); info.error = "truncated name"; return false; }
    info.file_name.assign((char*)namebuf, nlen);
    size_t ext_len = 0;
    if (info.version == 2) {
        u8 extbuf[2];
        if (!read_exact(f, extbuf, 2)) { fclose(f); info.error = "truncated ext"; return false; }
        ext_len = (size_t)load_u16(extbuf);
        if (ext_len > 4096) { fclose(f); info.error = "bad ext length"; return false; }
        if (ext_len > 0) {
            std::vector<u8> ext(ext_len);
            if (!read_exact(f, ext.data(), ext.size())) { fclose(f); info.error = "truncated ext"; return false; }
            std::string e;
            if (!parse_ext_blob(ext, info, e)) { fclose(f); info.error = e; return false; }
        }
    }
    if (!read_exact(f, rest, 20)) { fclose(f); info.error = "truncated tail"; return false; }
    p = rest;
    info.blocks = load_u64(p); p += 8;
    info.block_size = load_u32(p); p += 4;
    info.total_size = load_u64(p);
    // consumed: HDR_FIXED + name + (v2: ext_len u16 + ext) + 20
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    long ext_bytes = (info.version == 2) ? (long)(2 + ext_len) : 0;
    long expect = (long)HDR_FIXED + nlen + ext_bytes + 20 + (long)info.total_size + (long)info.blocks * BLOCK_TAG + FILE_MAC_LEN;
    fclose(f);
    if (flen != expect) { info.error = "size mismatch (corrupt?)"; return false; }
    info.ok = true;
    return true;
}

bool fwsec_encrypt_file(const std::string& in_path, const std::string& out_path,
                        const FwsecEncryptOptions& opt, std::string& error) {
    FILE* in = fopen(in_path.c_str(), "rb");
    if (!in) { error = "cannot open input"; return false; }
    fseek(in, 0, SEEK_END);
    long fsize = ftell(in);
    fseek(in, 0, SEEK_SET);
    if (fsize <= 0) { fclose(in); error = "empty input"; return false; }

    // 1. Ephemeral ML-KEM keypair
    u8 kem_pk[FWSEC_MLKEM_PK_SIZE], kem_sk[FWSEC_MLKEM_SK_SIZE];
    u8 kem_ct[FWSEC_MLKEM_CT_SIZE], ss[FWSEC_MLKEM_SS_SIZE];
    if (fwsec_mlkem_keypair(kem_pk, kem_sk) != 0 ||
        fwsec_mlkem_encaps(kem_ct, ss, kem_pk) != 0) {
        fclose(in); error = "ML-KEM failure"; return false;
    }
    (void)kem_pk;

    // 2. Salt + unlock key
    FwsecFileInfo info;
    info.version = 2;
    info.auth_type = opt.auth_type;
    if (!opt.file_name.empty() && opt.file_name.size() <= 256) info.file_name = opt.file_name;
    else info.file_name = "RMR.hex";
    info.ar2_t = opt.ar2_t; info.ar2_m = opt.ar2_m; info.ar2_p = opt.ar2_p;
    info.block_size = opt.block_size;
    info.total_size = (u64)fsize;
    if (opt.use_salt) memcpy(info.salt, opt.salt, 16);
    else randombytes(info.salt, 16);

    u8 unlock[32];
    if (opt.auth_type == AUTH_PASSWORD) {
        if (!argon2id_hash(opt.ar2_t, opt.ar2_m, opt.ar2_p,
                           (const u8*)opt.password.data(), opt.password.size(),
                           info.salt, 16, unlock, 32)) {
            fclose(in); error = "Argon2id failure"; return false;
        }
    } else if (opt.fido_secret.size() == 32) {
        if (opt.cred_id.empty() || opt.rp_id.empty()) {
            fclose(in); error = "fido credential metadata missing"; return false;
        }
        if (opt.cred_id.size() > 512 || opt.rp_id.size() > 256) {
            fclose(in); error = "fido metadata too large"; return false;
        }
        if (opt.file_version.size() > 512 || opt.update_note.size() > 512 || opt.build_meta.size() > 512) {
            fclose(in); error = "firmware metadata too large"; return false;
        }
        hkdf_sha256(opt.fido_secret.data(), 32, info.salt, 16,
                    (const u8*)"FWSEC1-FIDO", 11, unlock, 32);
    } else {
        fclose(in); error = "no credential"; return false;
    }
    info.device = opt.device;
    info.cred_id = opt.cred_id;
    info.rp_id = opt.rp_id;

    // 3. Write header
    FILE* out = fopen(out_path.c_str(), "wb");
    if (!out) { fclose(in); error = "cannot open output"; return false; }
    u8 hdr[HDR_FIXED];
    memcpy(hdr, MAGIC, MAGIC_LEN);
    u8* p = hdr + MAGIC_LEN;
    store_u16(p, 2); p += 2;
    *p++ = (u8)opt.auth_type;
    *p++ = 0;
    store_u32(p, HDR_FIXED); p += 4;
    memcpy(p, info.salt, 16); p += 16;
    store_u32(p, info.ar2_t); p += 4;
    store_u32(p, info.ar2_m); p += 4;
    store_u32(p, info.ar2_p); p += 4;
    memcpy(p, kem_ct, FWSEC_MLKEM_CT_SIZE); p += FWSEC_MLKEM_CT_SIZE;
    {
        u8 wrapped[FWSEC_MLKEM_SK_SIZE], tag[16];
        aead_wrap(unlock, kem_sk, FWSEC_MLKEM_SK_SIZE, "FWSEC1-KEMSK", wrapped, tag);
        memcpy(p, wrapped, FWSEC_MLKEM_SK_SIZE); p += FWSEC_MLKEM_SK_SIZE;
        memcpy(p, tag, 16); p += 16;
    }
    store_u16(p, (u16)info.file_name.size()); p += 2;
    if (!write_exact(out, hdr, sizeof(hdr)) ||
        !write_exact(out, info.file_name.data(), info.file_name.size())) {
        fclose(in); fclose(out); error = "write failed"; return false;
    }
    // v2 auth extension blob: FIDO metadata (fido only) + version/note/meta (all)
    std::vector<u8> ext;
    if (info.version == 2) {
        if (info.auth_type != AUTH_PASSWORD) {
            ext.push_back(info.device);
            u8 lb[2]; store_u16(lb, (u16)info.cred_id.size());
            ext.insert(ext.end(), lb, lb + 2);
            ext.insert(ext.end(), info.cred_id.begin(), info.cred_id.end());
            store_u16(lb, (u16)info.rp_id.size());
            ext.insert(ext.end(), lb, lb + 2);
            ext.insert(ext.end(), info.rp_id.begin(), info.rp_id.end());
        }
        auto put = [&ext](const std::string& s) {
            u8 lb[2]; store_u16(lb, (u16)s.size());
            ext.insert(ext.end(), lb, lb + 2);
            ext.insert(ext.end(), s.begin(), s.end());
        };
        put(opt.file_version);
        put(opt.update_note);
        put(opt.build_meta);
    }
    u8 extlen[2];
    store_u16(extlen, (u16)ext.size());
    if (!write_exact(out, extlen, 2) || (!ext.empty() && !write_exact(out, ext.data(), ext.size()))) {
        fclose(in); fclose(out); error = "write failed"; return false;
    }
    u8 tail[20];
    p = tail;
    store_u64(p, 0); p += 8;  // blocks placeholder
    store_u32(p, info.block_size); p += 4;
    store_u64(p, info.total_size); p += 8;
    if (!write_exact(out, tail, sizeof(tail))) { fclose(in); fclose(out); error = "write failed"; return false; }

    // 4. Block encryption
    const u64 total = info.total_size;
    const u32 bs = info.block_size;
    const u64 blocks = (total + bs - 1) / bs;
    u8* buf = new u8[bs];
    u8* ctbuf = new u8[bs];
    u8 key[32], nonce[12], tag[16];
    u8 mac_ctx_key[32];
    hkdf_sha256(ss, 32, info.salt, 16, (const u8*)"FWSEC1-MAC", 10, mac_ctx_key, 32);
    Sha256 mac_acc; mac_acc.init();
    u8 blk_info[24];
    for (u64 i = 0; i < blocks; i++) {
        size_t n = (size_t)((i == blocks - 1 && total % bs) ? total % bs : bs);
        if (fread(buf, 1, n, in) != n) { delete[] buf; delete[] ctbuf; fclose(in); fclose(out); error = "read failed"; return false; }
        // per-block key + nonce
        u8 info1[20];
        memcpy(info1, "FWSEC1-BLK", 10);
        store_u64(info1 + 10, i);
        hkdf_sha256(ss, 32, info.salt, 16, info1, 18, key, 32);
        memcpy(info1, "FWSEC1-NON", 10);
        store_u64(info1 + 10, i);
        hkdf_sha256(ss, 32, info.salt, 16, info1, 18, nonce, 12);
        // aad = header prefix + block index
        store_u64(blk_info, i);
        aead_chacha20poly1305_encrypt(key, nonce, blk_info, 8, buf, n, ctbuf, tag);
        if (!write_exact(out, ctbuf, n) || !write_exact(out, tag, 16)) {
            delete[] buf; delete[] ctbuf; fclose(in); fclose(out); error = "write failed"; return false;
        }
        mac_acc.update(tag, 16);
        if (opt.progress && !opt.progress(opt.progress_ctx, i + 1, blocks)) {
            delete[] buf; delete[] ctbuf; fclose(in); fclose(out); error = "cancelled"; return false;
        }
        secure_zero(key, 32); secure_zero(nonce, 12);
    }
    // patch block count
    fseek(out, HDR_FIXED + (long)info.file_name.size() + 2 + (long)ext.size(), SEEK_SET);
    u8 bc[8];
    store_u64(bc, blocks);
    fwrite(bc, 1, 8, out);
    fseek(out, 0, SEEK_END);

    // 5. File MAC
    u8 mac[32];
    mac_acc.final(mac);
    u8 filemac[32];
    hmac_sha256(mac_ctx_key, 32, mac, 32, filemac);
    write_exact(out, filemac, 32);

    delete[] buf; delete[] ctbuf;
    secure_zero(ss, 32); secure_zero(unlock, 32); secure_zero(kem_sk, sizeof(kem_sk));
    secure_zero(mac_ctx_key, 32);
    fclose(in); fclose(out);
    return true;
}

bool fwsec_decrypt_file(const std::string& in_path, const std::string& out_path,
                        const std::string& password,
                        const std::vector<u8>& fido_secret,
                        bool (*progress)(void* ctx, u64 done, u64 total),
                        void* progress_ctx,
                        std::string& error) {
    FwsecFileInfo info;
    if (!fwsec_probe(in_path, info)) { error = info.error; return false; }
    FILE* f = fopen(in_path.c_str(), "rb");
    if (!f) { error = "cannot open input"; return false; }

    // Read full header
    u8 hdr[HDR_FIXED];
    if (!read_exact(f, hdr, sizeof(hdr))) { fclose(f); error = "truncated"; return false; }
    const u8* p = hdr + MAGIC_LEN + 8;
    memcpy(info.salt, p, 16); p += 16;
    info.ar2_t = load_u32(p); p += 4;
    info.ar2_m = load_u32(p); p += 4;
    info.ar2_p = load_u32(p); p += 4;
    const u8* kem_ct = p; p += FWSEC_MLKEM_CT_SIZE;
    const u8* wrapped_sk = p; const u8* wrap_tag = p + FWSEC_MLKEM_SK_SIZE; p += FWSEC_MLKEM_SK_SIZE + 16;
    u16 nlen = load_u16(p);
    if (!read_exact(f, hdr, nlen)) { fclose(f); error = "truncated name"; return false; }
    info.file_name.assign((char*)hdr, nlen);
    size_t ext_len = 0;
    if (info.version == 2) {
        u8 extlen[2];
        if (!read_exact(f, extlen, 2)) { fclose(f); error = "truncated ext"; return false; }
        ext_len = (size_t)load_u16(extlen);
        if (ext_len > 4096) { fclose(f); error = "bad ext"; return false; }
        if (ext_len > 0) {
            std::vector<u8> ext(ext_len);
            if (!read_exact(f, ext.data(), ext.size())) { fclose(f); error = "truncated ext"; return false; }
            std::string e;
            if (!parse_ext_blob(ext, info, e)) { fclose(f); error = e; return false; }
        }
    }
    size_t ext_bytes = (info.version == 2) ? (2 + ext_len) : 0;
    u8 tail[20];
    if (!read_exact(f, tail, 20)) { fclose(f); error = "truncated tail"; return false; }
    p = tail;
    info.blocks = load_u64(p); p += 8;
    info.block_size = load_u32(p); p += 4;
    info.total_size = load_u64(p);

    // Unlock KEM SK
    u8 unlock[32];
    if (!derive_unlock_key(info, password, fido_secret, unlock)) {
        fclose(f); error = "credential unavailable for this auth type"; return false;
    }
    u8 kem_sk[FWSEC_MLKEM_SK_SIZE];
    if (!aead_unwrap(unlock, wrapped_sk, FWSEC_MLKEM_SK_SIZE, "FWSEC1-KEMSK", wrap_tag, kem_sk)) {
        secure_zero(unlock, 32);
        fclose(f);
        error = "wrong password / credential";
        return false;
    }
    u8 ss[FWSEC_MLKEM_SS_SIZE];
    if (fwsec_mlkem_decaps(ss, kem_ct, kem_sk) != 0) {
        fclose(f); error = "KEM decapsulation failed"; return false;
    }

    // Verify file MAC first (read all block tags)
    u8 mac_ctx_key[32];
    hkdf_sha256(ss, 32, info.salt, 16, (const u8*)"FWSEC1-MAC", 10, mac_ctx_key, 32);
    Sha256 mac_acc; mac_acc.init();
    fseek(f, (long)(HDR_FIXED + nlen + ext_bytes + 20), SEEK_SET);
    u8 tagbuf[16];
    const u32 mac_bs = info.block_size;
    u64 mac_remaining = info.total_size;
    for (u64 i = 0; i < info.blocks; i++) {
        size_t n = (size_t)(mac_remaining > mac_bs ? mac_bs : mac_remaining);
        if (fseek(f, (long)n, SEEK_CUR) != 0) { fclose(f); error = "corrupt"; return false; }
        if (!read_exact(f, tagbuf, 16)) { fclose(f); error = "corrupt"; return false; }
        mac_acc.update(tagbuf, 16);
        mac_remaining -= n;
    }
    u8 mac[32], filemac[32];
    mac_acc.final(mac);
    if (!read_exact(f, filemac, 32)) { fclose(f); error = "corrupt"; return false; }
    u8 expect[32];
    hmac_sha256(mac_ctx_key, 32, mac, 32, expect);
    u8 diff = 0;
    for (int i = 0; i < 32; i++) diff |= expect[i] ^ filemac[i];
    if (diff) { fclose(f); error = "integrity check failed"; return false; }

    // Decrypt blocks
    FILE* out = fopen(out_path.c_str(), "wb");
    if (!out) { fclose(f); error = "cannot open output"; return false; }
    const u32 bs = info.block_size;
    u8* buf = new u8[bs];
    u8* ctbuf = new u8[bs];
    u8 key[32], nonce[12], blk_info[8];
    fseek(f, (long)(HDR_FIXED + nlen + ext_bytes + 20), SEEK_SET);
    u64 remaining = info.total_size;
    for (u64 i = 0; i < info.blocks; i++) {
        size_t n = (size_t)(remaining > bs ? bs : remaining);
        if (!read_exact(f, ctbuf, n) || !read_exact(f, tagbuf, 16)) {
            delete[] buf; delete[] ctbuf; fclose(f); fclose(out); error = "corrupt"; return false;
        }
        u8 info1[20];
        memcpy(info1, "FWSEC1-BLK", 10);
        store_u64(info1 + 10, i);
        hkdf_sha256(ss, 32, info.salt, 16, info1, 18, key, 32);
        memcpy(info1, "FWSEC1-NON", 10);
        store_u64(info1 + 10, i);
        hkdf_sha256(ss, 32, info.salt, 16, info1, 18, nonce, 12);
        store_u64(blk_info, i);
        if (!aead_chacha20poly1305_decrypt(key, nonce, blk_info, 8, ctbuf, n, tagbuf, buf)) {
            delete[] buf; delete[] ctbuf; fclose(f); fclose(out); error = "block decrypt failed"; return false;
        }
        if (fwrite(buf, 1, n, out) != n) {
            delete[] buf; delete[] ctbuf; fclose(f); fclose(out); error = "write failed"; return false;
        }
        remaining -= n;
        secure_zero(key, 32); secure_zero(nonce, 12);
        if (progress && !progress(progress_ctx, i + 1, info.blocks)) {
            delete[] buf; delete[] ctbuf; fclose(f); fclose(out); error = "cancelled"; return false;
        }
    }
    delete[] buf; delete[] ctbuf;
    secure_zero(ss, 32); secure_zero(unlock, 32); secure_zero(kem_sk, sizeof(kem_sk));
    secure_zero(mac_ctx_key, 32);
    fclose(f); fclose(out);
    return true;
}


static bool parse_ext_blob(const std::vector<u8>& ext, FwsecFileInfo& info, std::string& err) {
    const u8* q = ext.data();
    size_t remain = ext.size();
    if (info.auth_type != AUTH_PASSWORD) {
        if (remain < 5) { err = "bad ext data"; return false; }
        info.device = *q++; remain -= 1;
        u16 clen = load_u16(q); q += 2; remain -= 2;
        if (clen > 512 || remain < clen) { err = "bad ext data"; return false; }
        info.cred_id.assign(q, q + clen); q += clen; remain -= clen;
        u16 rlen = load_u16(q); q += 2; remain -= 2;
        if (rlen > 256 || remain < rlen) { err = "bad ext data"; return false; }
        info.rp_id.assign((char*)q, rlen); q += rlen; remain -= rlen;
    }
    std::string* fields[3] = { &info.file_version, &info.update_note, &info.build_meta };
    for (auto* f : fields) {
        if (remain == 0) break;
        if (remain < 2) { err = "bad ext data"; return false; }
        u16 l = load_u16(q); q += 2; remain -= 2;
        if (l > 512 || remain < l) { err = "bad ext data"; return false; }
        f->assign((const char*)q, l); q += l; remain -= l;
    }
    return true;
}
} // namespace fwsec
