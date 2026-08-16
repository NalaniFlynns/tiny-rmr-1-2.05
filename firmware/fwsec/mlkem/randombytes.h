#ifndef FW_RANDOMBYTES_H
#define FW_RANDOMBYTES_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int randombytes(uint8_t *output, size_t n);
#ifdef __cplusplus
}
#endif
#endif