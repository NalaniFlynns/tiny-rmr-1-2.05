#include <stdio.h>
#include <string.h>
#include "argon2.h"
static void run(const char* pwd, const char* salt, uint32_t t, uint32_t m, uint32_t p) {
    unsigned char hash[32];
    argon2_hash(t, m, p, pwd, strlen(pwd), salt, strlen(salt), hash, 32, NULL, 0, Argon2_id, ARGON2_VERSION_13);
    for (int i = 0; i < 32; i++) printf("%02x", hash[i]);
    printf("\n");
}
int main(void) {
    run("0123456789abcdef", "0123456789abcdef", 3, 32, 4);
    run("password", "somesalt", 2, 16, 1);
    return 0;
}
