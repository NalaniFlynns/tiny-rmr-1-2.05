#include <stdio.h>
#include <string.h>
#include "argon2.h"
int main(void) {
    const char* pwd = "password";
    const char* salt = "somesalt";
    unsigned char hash[32];
    argon2_hash(2, 16, 1, pwd, strlen(pwd), salt, strlen(salt), hash, 32, NULL, 0, Argon2_id, ARGON2_VERSION_13);
    for (int i = 0; i < 32; i++) printf("%02x", hash[i]);
    printf("\n");
    return 0;
}
