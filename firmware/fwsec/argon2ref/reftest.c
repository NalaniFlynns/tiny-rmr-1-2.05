#include <stdio.h>
#include <string.h>
#include "argon2.h"
#include "core.h"
int main(void) {
    const char* pwd = "0123456789abcdef";
    const char* salt = "0123456789abcdef";
    unsigned char hash[32];
    uint32_t t_cost = 3, m_cost = 32, parallelism = 4;
    int rc = argon2_hash(t_cost, m_cost, parallelism, pwd, strlen(pwd),
                         salt, strlen(salt), hash, sizeof(hash), NULL, 0,
                         Argon2_id, ARGON2_VERSION_13);
    printf("rc=%d\n", rc);
    for (int i = 0; i < 32; i++) printf("%02x", hash[i]);
    printf("\n");
    return 0;
}
