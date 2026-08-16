#include "randombytes.h"
#include <windows.h>
#include <bcrypt.h>
int randombytes(uint8_t *output, size_t n) {
    NTSTATUS st = BCryptGenRandom(NULL, output, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return (st == 0) ? 0 : -1;
}