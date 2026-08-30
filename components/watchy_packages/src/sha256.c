#include "watchy/package_crypto.h"

#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} sha256_state_t;

static const uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
};

static uint32_t rotate(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

static uint32_t load_be(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | bytes[3];
}

static void transform(sha256_state_t *sha, const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a,b,c,d,e,f,g,h;
    for (size_t i = 0u; i < 16u; ++i) words[i] = load_be(block + i * 4u);
    for (size_t i = 16u; i < 64u; ++i) {
        const uint32_t s0 = rotate(words[i-15u],7u)^rotate(words[i-15u],18u)^(words[i-15u]>>3u);
        const uint32_t s1 = rotate(words[i-2u],17u)^rotate(words[i-2u],19u)^(words[i-2u]>>10u);
        words[i] = words[i-16u] + s0 + words[i-7u] + s1;
    }
    a=sha->state[0]; b=sha->state[1]; c=sha->state[2]; d=sha->state[3];
    e=sha->state[4]; f=sha->state[5]; g=sha->state[6]; h=sha->state[7];
    for (size_t i = 0u; i < 64u; ++i) {
        const uint32_t s1=rotate(e,6u)^rotate(e,11u)^rotate(e,25u);
        const uint32_t choose=(e&f)^((~e)&g);
        const uint32_t t1=h+s1+choose+K[i]+words[i];
        const uint32_t s0=rotate(a,2u)^rotate(a,13u)^rotate(a,22u);
        const uint32_t majority=(a&b)^(a&c)^(b&c);
        const uint32_t t2=s0+majority;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    sha->state[0]+=a; sha->state[1]+=b; sha->state[2]+=c; sha->state[3]+=d;
    sha->state[4]+=e; sha->state[5]+=f; sha->state[6]+=g; sha->state[7]+=h;
}

static void update(sha256_state_t *sha, const uint8_t *bytes, size_t size) {
    sha->bytes += size;
    while (size != 0u) {
        const size_t take = size < sizeof(sha->block) - sha->used
                                ? size : sizeof(sha->block) - sha->used;
        memcpy(sha->block + sha->used, bytes, take);
        sha->used += take;
        bytes += take;
        size -= take;
        if (sha->used == sizeof(sha->block)) {
            transform(sha, sha->block);
            sha->used = 0u;
        }
    }
}

bool watchy_package_sha256_regions(void *context,
                                   const watchy_byte_region_t *regions,
                                   size_t region_count,
                                   uint8_t out_digest[WATCHY_PACKAGE_DIGEST_SIZE]) {
    sha256_state_t sha = {
        .state = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                  0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u},
    };
    uint64_t bits;
    (void)context;
    if (regions == NULL || out_digest == NULL) return false;
    for (size_t i = 0u; i < region_count; ++i) {
        if (regions[i].bytes == NULL && regions[i].size != 0u) return false;
        update(&sha, regions[i].bytes, regions[i].size);
    }
    bits = sha.bytes * 8u;
    sha.block[sha.used++] = 0x80u;
    if (sha.used > 56u) {
        memset(sha.block + sha.used, 0, 64u - sha.used);
        transform(&sha, sha.block);
        sha.used = 0u;
    }
    memset(sha.block + sha.used, 0, 56u - sha.used);
    for (size_t i = 0u; i < 8u; ++i) sha.block[63u-i] = (uint8_t)(bits >> (i*8u));
    transform(&sha, sha.block);
    for (size_t i = 0u; i < 8u; ++i) {
        out_digest[i*4u]=(uint8_t)(sha.state[i]>>24u);
        out_digest[i*4u+1u]=(uint8_t)(sha.state[i]>>16u);
        out_digest[i*4u+2u]=(uint8_t)(sha.state[i]>>8u);
        out_digest[i*4u+3u]=(uint8_t)sha.state[i];
    }
    return true;
}
