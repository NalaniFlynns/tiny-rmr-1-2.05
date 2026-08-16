#ifndef FWSEC_MLKEM_H
#define FWSEC_MLKEM_H

// ML-KEM-768 wrapper (PQClean clean implementation, BCrypt RNG).
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "mlkem/api.h"

int PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair_derand(uint8_t *pk, uint8_t *sk, const uint8_t *coins);
int PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc_derand(uint8_t *ct, uint8_t *ss, const uint8_t *pk, const uint8_t *coins);
#ifdef __cplusplus
}
#endif

#define FWSEC_MLKEM_PK_SIZE    PQCLEAN_MLKEM768_CLEAN_CRYPTO_PUBLICKEYBYTES  // 1184
#define FWSEC_MLKEM_SK_SIZE    PQCLEAN_MLKEM768_CLEAN_CRYPTO_SECRETKEYBYTES // 2400
#define FWSEC_MLKEM_CT_SIZE    PQCLEAN_MLKEM768_CLEAN_CRYPTO_CIPHERTEXTBYTES // 1088
#define FWSEC_MLKEM_SS_SIZE    PQCLEAN_MLKEM768_CLEAN_CRYPTO_BYTES           // 32

#ifdef __cplusplus
extern "C" {
#endif

static inline int fwsec_mlkem_keypair(uint8_t *pk, uint8_t *sk) {
    return PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(pk, sk);
}
static inline int fwsec_mlkem_encaps(uint8_t *ct, uint8_t *ss, const uint8_t *pk) {
    return PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(ct, ss, pk);
}
static inline int fwsec_mlkem_decaps(uint8_t *ss, const uint8_t *ct, const uint8_t *sk) {
    return PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(ss, ct, sk);
}

#ifdef __cplusplus
}
#endif
#endif
