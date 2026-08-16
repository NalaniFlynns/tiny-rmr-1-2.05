// fwsec_container_test.cpp
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "fwsec_container.h"
using namespace fwsec;

static int failures = 0;
static void check(bool ok, const char* name) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

int main() {
    // Build a pseudo-random plaintext of ~100 KB
    std::vector<u8> pt(100000);
    for (size_t i = 0; i < pt.size(); i++) pt[i] = (u8)(i * 31 + (i >> 8));
    FILE* f = fopen("pt_in.bin", "wb");
    fwrite(pt.data(), 1, pt.size(), f);
    fclose(f);

    std::string err;
    FwsecEncryptOptions opt;
    opt.auth_type = AUTH_PASSWORD;
    opt.password = "correct horse battery staple";
    opt.file_name = "RMR_BATT.fwsec";
    opt.file_version = "V4.4.0_DBGL";
    opt.update_note = "修复开机逻辑; 新增加密烧录";
    opt.build_meta = "build=2026-08-16 22:10,alg=MLKEM768+ChaCha20Poly1305+v2,yubico=YUBI-1234";
    bool ok = fwsec_encrypt_file("pt_in.bin", "ct.fwsec", opt, err);
    check(ok, "encrypt password");
    if (!ok) { printf("err: %s\n", err.c_str()); return 1; }

    FwsecFileInfo info;
    ok = fwsec_probe("ct.fwsec", info);
    check(ok && info.auth_type == AUTH_PASSWORD && info.file_name == "RMR_BATT.fwsec" &&
          info.total_size == pt.size() && info.blocks == 7 &&
          info.file_version == "V4.4.0_DBGL" && info.update_note == "修复开机逻辑; 新增加密烧录" &&
          info.build_meta == "build=2026-08-16 22:10,alg=MLKEM768+ChaCha20Poly1305+v2,yubico=YUBI-1234", "probe");
    if (!ok) { printf("probe err: %s\n", info.error.c_str()); return 1; }

    ok = fwsec_decrypt_file("ct.fwsec", "pt_out.bin", "correct horse battery staple", {}, nullptr, nullptr, err);
    check(ok, "decrypt password");
    if (!ok) { printf("err: %s\n", err.c_str()); return 1; }
    FILE* g = fopen("pt_out.bin", "rb");
    std::vector<u8> out(pt.size());
    size_t rd = fread(out.data(), 1, out.size(), g);
    fclose(g);
    check(rd == pt.size() && memcmp(out.data(), pt.data(), pt.size()) == 0, "plaintext match");

    // wrong password rejected
    ok = fwsec_decrypt_file("ct.fwsec", "pt_bad.bin", "wrong password", {}, nullptr, nullptr, err);
    check(!ok, "wrong password rejected");

    // tampered file rejected
    FILE* h = fopen("ct.fwsec", "rb+");
    fseek(h, 100, SEEK_SET);
    u8 x = 0xFF;
    fwrite(&x, 1, 1, h);
    fclose(h);
    ok = fwsec_decrypt_file("ct.fwsec", "pt_bad2.bin", "correct horse battery staple", {}, nullptr, nullptr, err);
    check(!ok, "tampered rejected");

    // fido path
    std::vector<u8> secret(32);
    for (int i = 0; i < 32; i++) secret[i] = (u8)i;
    FwsecEncryptOptions o2;
    o2.auth_type = AUTH_FIDO2;
    o2.fido_secret = secret;
    o2.device = 1;
    o2.cred_id = {0x11,0x22,0x33,0x44,0x55};
    o2.rp_id = "rmr.local";
    ok = fwsec_encrypt_file("pt_in.bin", "ct2.fwsec", o2, err);
    check(ok, "encrypt fido");
    FwsecFileInfo f2;
    ok = fwsec_probe("ct2.fwsec", f2);
    check(ok && f2.auth_type == AUTH_FIDO2 && f2.device == 1 && f2.rp_id == "rmr.local" &&
          f2.cred_id.size() == 5 && f2.cred_id[0] == 0x11 && f2.cred_id[4] == 0x55, "probe fido metadata");
    ok = fwsec_decrypt_file("ct2.fwsec", "pt_out2.bin", "", secret, nullptr, nullptr, err);
    check(ok, "decrypt fido");
    if (ok) {
        FILE* g2 = fopen("pt_out2.bin", "rb");
        std::vector<u8> out2(pt.size());
        size_t rd2 = fread(out2.data(), 1, out2.size(), g2);
        fclose(g2);
        check(rd2 == pt.size() && memcmp(out2.data(), pt.data(), pt.size()) == 0, "fido plaintext match");
    }

    printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures ? 1 : 0;
}
