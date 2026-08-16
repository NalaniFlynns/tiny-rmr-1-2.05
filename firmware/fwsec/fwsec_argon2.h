#pragma once
#include "fwsec_util.h"
namespace fwsec {
/* Argon2id v1.3 (RFC 9106). Returns true on success. */
bool argon2id_hash(u32 t_cost, u32 m_cost_kib, u32 parallelism,
                   const u8* pwd, size_t pwdlen,
                   const u8* salt, size_t saltlen,
                   u8* out, size_t outlen);
}