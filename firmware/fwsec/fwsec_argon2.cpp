#include "fwsec_argon2.h"
#include "fwsec_blake2b.h"
namespace fwsec {

namespace {
constexpr u32 ARGON2_VERSION = 0x13;
constexpr u32 ARGON2_SYNC_POINTS = 4;
constexpr u32 ARGON2_ADDRESSES_IN_BLOCK = 128;
constexpr size_t BLOCK_WORDS = 128;      /* 1024 bytes / 8 */
constexpr u32 ARGON2_ID = 2;

struct Block { u64 v[BLOCK_WORDS]; };

static void store_block(u8 out[1024], const Block& b) {
    for (size_t i = 0; i < BLOCK_WORDS; i++) store64_le(out + i * 8, b.v[i]);
}
static void load_block(Block& b, const u8 in[1024]) {
    for (size_t i = 0; i < BLOCK_WORDS; i++) b.v[i] = load64_le(in + i * 8);
}
static void copy_block(Block& dst, const Block& src) { memcpy(dst.v, src.v, sizeof(Block)); }
static void xor_block(Block& dst, const Block& src) {
    for (size_t i = 0; i < BLOCK_WORDS; i++) dst.v[i] ^= src.v[i];
}
static void init_block(Block& b) { memset(b.v, 0, sizeof(Block)); }

static u64 fblamka(u64 x, u64 y) {
    const u64 m = 0xFFFFFFFFULL;
    const u64 xy = (x & m) * (y & m);
    return x + y + 2 * xy;
}

#define FW_G(a, b, c, d) do { \
    a = fblamka(a, b); d = rotr64(d ^ a, 32); c = fblamka(c, d); b = rotr64(b ^ c, 24); \
    a = fblamka(a, b); d = rotr64(d ^ a, 16); c = fblamka(c, d); b = rotr64(b ^ c, 63); \
} while (0)

#define FW_ROUND(v0,v1,v2,v3,v4,v5,v6,v7,v8,v9,v10,v11,v12,v13,v14,v15) do { \
    FW_G(v0,v4,v8,v12); FW_G(v1,v5,v9,v13); FW_G(v2,v6,v10,v14); FW_G(v3,v7,v11,v15); \
    FW_G(v0,v5,v10,v15); FW_G(v1,v6,v11,v12); FW_G(v2,v7,v8,v13); FW_G(v3,v4,v9,v14); \
} while (0)

static void fill_block(const Block& prev, const Block& ref, Block& next, bool with_xor) {
    Block r, tmp;
    copy_block(tmp, ref);
    xor_block(tmp, prev);
    copy_block(r, tmp);
    if (with_xor) xor_block(tmp, next);
    for (int i = 0; i < 8; i++) {
        FW_ROUND(r.v[16*i], r.v[16*i+1], r.v[16*i+2], r.v[16*i+3],
                 r.v[16*i+4], r.v[16*i+5], r.v[16*i+6], r.v[16*i+7],
                 r.v[16*i+8], r.v[16*i+9], r.v[16*i+10], r.v[16*i+11],
                 r.v[16*i+12], r.v[16*i+13], r.v[16*i+14], r.v[16*i+15]);
    }
    for (int i = 0; i < 8; i++) {
        FW_ROUND(r.v[2*i], r.v[2*i+1], r.v[2*i+16], r.v[2*i+17],
                 r.v[2*i+32], r.v[2*i+33], r.v[2*i+48], r.v[2*i+49],
                 r.v[2*i+64], r.v[2*i+65], r.v[2*i+80], r.v[2*i+81],
                 r.v[2*i+96], r.v[2*i+97], r.v[2*i+112], r.v[2*i+113]);
    }
    copy_block(next, tmp);
    xor_block(next, r);
}

static void blake2b_long(u8* out, u32 outlen, const u8* in, u32 inlen) {
    u8 outlen_bytes[4]; store32_le(outlen_bytes, outlen);
    if (outlen <= 64) {
        Blake2b st0; st0.init(outlen);
        st0.update(outlen_bytes, 4);
        st0.update(in, inlen);
        st0.final(out);
        return;
    }
    u8 b[64];
    Blake2b st; st.init(64);
    st.update(outlen_bytes, 4);
    st.update(in, inlen);
    st.final(b);
    memcpy(out, b, 32);
    out += 32;
    u32 toproduce = outlen - 32;
    while (toproduce > 64) {
        blake2b(b, 64, b, 64);
        memcpy(out, b, 32);
        out += 32;
        toproduce -= 32;
    }
    blake2b(b, toproduce, b, 64);
    memcpy(out, b, toproduce);
}

static void initial_hash(u8* blockhash, u32 lanes, u32 outlen, u32 m_cost, u32 t_cost, u32 type,
                         const u8* pwd, size_t pwdlen, const u8* salt, size_t saltlen) {
    Blake2b st; st.init(64);
    u8 v[4];
    auto put32 = [&](u32 x) { store32_le(v, x); st.update(v, 4); };
    put32(lanes); put32(outlen); put32(m_cost); put32(t_cost);
    put32(ARGON2_VERSION); put32(type); put32((u32)pwdlen);
    if (pwdlen) st.update(pwd, pwdlen);
    put32((u32)saltlen);
    if (saltlen) st.update(salt, saltlen);
    put32(0);  /* secret len */
    put32(0);  /* ad len */
    st.final(blockhash);
}

struct Instance {
    u32 lanes, outlen, m_cost, t_cost, type;
    u32 lane_length, segment_length, memory_blocks;
    Block* memory;
};

static u32 index_alpha(const Instance& inst, u32 pass, u32 slice, u32 index, u32 pseudo_rand, bool same_lane) {
    u32 reference_area_size;
    u64 relative_position;
    u32 start_position, absolute_position;
    if (0 == pass) {
        if (0 == slice) reference_area_size = index - 1;
        else if (same_lane) reference_area_size = slice * inst.segment_length + index - 1;
        else reference_area_size = slice * inst.segment_length + ((index == 0) ? (u32)-1 : 0);
    } else {
        if (same_lane) reference_area_size = inst.lane_length - inst.segment_length + index - 1;
        else reference_area_size = inst.lane_length - inst.segment_length + ((index == 0) ? (u32)-1 : 0);
    }
    relative_position = pseudo_rand;
    relative_position = relative_position * relative_position >> 32;
    relative_position = reference_area_size - 1 - (reference_area_size * relative_position >> 32);
    start_position = 0;
    if (0 != pass) start_position = (slice == ARGON2_SYNC_POINTS - 1) ? 0 : (slice + 1) * inst.segment_length;
    absolute_position = (start_position + (u32)relative_position) % inst.lane_length;
    return absolute_position;
}

static void next_addresses(Block& address, Block& input, const Block& zero) {
    input.v[6]++;
    fill_block(zero, input, address, false);
    fill_block(zero, address, address, false);
}

static void fill_segment(Instance& inst, u32 pass, u32 lane, u32 slice) {
    Block address, input, zero;
    bool data_independent = (inst.type == ARGON2_ID) && (pass == 0) && (slice < ARGON2_SYNC_POINTS / 2);
    if (data_independent) {
        init_block(zero);
        init_block(input);
        input.v[0] = pass; input.v[1] = lane; input.v[2] = slice;
        input.v[3] = inst.memory_blocks; input.v[4] = inst.t_cost; input.v[5] = inst.type;
    }
    u32 starting_index = 0;
    if (pass == 0 && slice == 0) {
        starting_index = 2;
        if (data_independent) next_addresses(address, input, zero);
    }
    u32 curr_offset = lane * inst.lane_length + slice * inst.segment_length + starting_index;
    u32 prev_offset = (curr_offset % inst.lane_length == 0) ? curr_offset + inst.lane_length - 1 : curr_offset - 1;
    for (u32 i = starting_index; i < inst.segment_length; i++, curr_offset++, prev_offset++) {
        if (curr_offset % inst.lane_length == 1) prev_offset = curr_offset - 1;
        u64 pseudo_rand;
        if (data_independent) {
            if (i % ARGON2_ADDRESSES_IN_BLOCK == 0) next_addresses(address, input, zero);
            pseudo_rand = address.v[i % ARGON2_ADDRESSES_IN_BLOCK];
        } else {
            pseudo_rand = inst.memory[prev_offset].v[0];
        }
        u32 ref_lane = (u32)(pseudo_rand >> 32) % inst.lanes;
        if (pass == 0 && slice == 0) ref_lane = lane;
        u32 ref_index = index_alpha(inst, pass, slice, i, (u32)pseudo_rand, ref_lane == lane);
        Block& ref = inst.memory[inst.lane_length * ref_lane + ref_index];
        Block& cur = inst.memory[curr_offset];
        fill_block(inst.memory[prev_offset], ref, cur, pass != 0);
    }
}

static bool argon2id_raw(u32 t_cost, u32 m_cost, u32 lanes,
                         const u8* pwd, size_t pwdlen, const u8* salt, size_t saltlen,
                         u8* out, size_t outlen) {
    if (m_cost < 8 * lanes) return false;
    if (outlen < 4) return false;
    Instance inst;
    inst.lanes = lanes; inst.outlen = (u32)outlen; inst.m_cost = m_cost;
    inst.t_cost = t_cost; inst.type = ARGON2_ID;
    inst.memory_blocks = m_cost;
    inst.segment_length = inst.memory_blocks / (lanes * ARGON2_SYNC_POINTS);
    inst.lane_length = inst.segment_length * ARGON2_SYNC_POINTS;
    inst.memory_blocks = inst.segment_length * lanes * ARGON2_SYNC_POINTS;
    inst.memory = new Block[inst.memory_blocks];
    if (!inst.memory) return false;
    u8 blockhash[64];
    initial_hash(blockhash, lanes, (u32)outlen, m_cost, t_cost, ARGON2_ID, pwd, pwdlen, salt, saltlen);
    for (u32 lane = 0; lane < lanes; lane++) {
        u8 seed[72];
        memcpy(seed, blockhash, 64);
        store32_le(seed + 64, 0); store32_le(seed + 68, lane);
        u8 bytes[1024];
        blake2b_long(bytes, 1024, seed, 72);
        load_block(inst.memory[lane * inst.lane_length], bytes);
        store32_le(seed + 64, 1);
        blake2b_long(bytes, 1024, seed, 72);
        load_block(inst.memory[lane * inst.lane_length + 1], bytes);
    }
    for (u32 pass = 0; pass < t_cost; pass++) {
        for (u32 slice = 0; slice < ARGON2_SYNC_POINTS; slice++) {
            for (u32 lane = 0; lane < lanes; lane++) {
                fill_segment(inst, pass, lane, slice);
            }
        }
    }
    Block finalblock;
    copy_block(finalblock, inst.memory[inst.lane_length - 1]);
    for (u32 lane = 1; lane < lanes; lane++) {
        xor_block(finalblock, inst.memory[lane * inst.lane_length + inst.lane_length - 1]);
    }
    u8 finalbytes[1024];
    store_block(finalbytes, finalblock);
    blake2b_long(out, (u32)outlen, finalbytes, 1024);
    secure_zero(blockhash, 64);
    delete[] inst.memory;
    return true;
}
} // namespace

bool argon2id_hash(u32 t_cost, u32 m_cost_kib, u32 parallelism,
                   const u8* pwd, size_t pwdlen,
                   const u8* salt, size_t saltlen,
                   u8* out, size_t outlen) {
    return argon2id_raw(t_cost, m_cost_kib, parallelism, pwd, pwdlen, salt, saltlen, out, outlen);
}
} // namespace fwsec