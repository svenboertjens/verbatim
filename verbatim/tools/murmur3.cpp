#include <stdint.h>
#include <string.h>

/* MurmurHash3 by Austin Appleby (public domain) */

namespace murmur3 {

static inline uint64_t fmix64(uint64_t k)
{
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;

    return k;
}

static inline uint64_t rotl64(uint64_t x, int8_t r)
{
    return (x << r) | (x >> (64 - r));
}

uint64_t hash(const void *_str, const size_t len)
{
    const unsigned char *str = (const unsigned char *)_str;
    const unsigned char *max = str + len;

    uint64_t h1 = 0;
    uint64_t h2 = 0;

    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    //----------
    // body

    while (str + 16 <= max)
    {
        uint64_t k1, k2;
        memcpy(&k1, str, 8);
        memcpy(&k2, str + 8, 8);
        str += 16;

        k1 *= c1; k1 = rotl64(k1,31); k1 *= c2; h1 ^= k1;
        h1 = rotl64(h1,27); h1 += h2; h1 = h1*5+0x52dce729;
        k2 *= c2; k2 = rotl64(k2,33); k2 *= c1; h2 ^= k2;
        h2 = rotl64(h2,31); h2 += h1; h2 = h2*5+0x38495ab5;
    }

    //----------
    // tail

    uint64_t k1 = 0;
    uint64_t k2 = 0;

    switch(max - str)
    {
    case 15: k2 ^= ((uint64_t)str[14]) << 48;
    case 14: k2 ^= ((uint64_t)str[13]) << 40;
    case 13: k2 ^= ((uint64_t)str[12]) << 32;
    case 12: k2 ^= ((uint64_t)str[11]) << 24;
    case 11: k2 ^= ((uint64_t)str[10]) << 16;
    case 10: k2 ^= ((uint64_t)str[ 9]) << 8;
    case  9: k2 ^= ((uint64_t)str[ 8]) << 0;
             k2 *= c2; k2 = rotl64(k2,33); k2 *= c1; h2 ^= k2;

    case  8: k1 ^= ((uint64_t)str[ 7]) << 56;
    case  7: k1 ^= ((uint64_t)str[ 6]) << 48;
    case  6: k1 ^= ((uint64_t)str[ 5]) << 40;
    case  5: k1 ^= ((uint64_t)str[ 4]) << 32;
    case  4: k1 ^= ((uint64_t)str[ 3]) << 24;
    case  3: k1 ^= ((uint64_t)str[ 2]) << 16;
    case  2: k1 ^= ((uint64_t)str[ 1]) << 8;
    case  1: k1 ^= ((uint64_t)str[ 0]) << 0;
             k1 *= c1; k1 = rotl64(k1,31); k1 *= c2; h1 ^= k1;
    };

    //----------
    // finalization

    h1 ^= len; h2 ^= len;

    h1 += h2;
    h2 += h1;

    h1 = fmix64(h1);
    h2 = fmix64(h2);

    h1 += h2;
    return h1;
}

}

