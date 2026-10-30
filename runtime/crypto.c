// crypto.c -- what's about secrets (D249's step 2, D255): SHA-256 and
// SHA-512 (FIPS 180-4), HMAC on them (RFC 2104), a comparison whose time
// doesn't depend on where two strings differ, and random bytes from the
// system. Our own C, as D251 has it: a hash is a fixed algorithm, with no
// protocol around it, so there's nothing to patch.

#include "rt.h"

#include <stdlib.h>
#include <string.h>

#include "io.h"

// --- SHA-256 ------------------------------------------------------------------

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror32(uint32_t x, int n) { return x >> n | x << (32 - n); }

static void block256(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ w[i - 15] >> 3;
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

// --- SHA-512 ------------------------------------------------------------------

static const uint64_t K512[80] = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538,
    0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe,
    0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
    0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
    0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab,
    0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
    0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
    0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
    0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53,
    0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373,
    0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c,
    0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
    0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
    0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
};

static uint64_t ror64(uint64_t x, int n) { return x >> n | x << (64 - n); }

static void block512(uint64_t h[8], const uint8_t *p) {
    uint64_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = 0;
        for (int j = 0; j < 8; j++) w[i] = w[i] << 8 | p[8 * i + j];
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = ror64(w[i - 15], 1) ^ ror64(w[i - 15], 8) ^ w[i - 15] >> 7;
        uint64_t s1 = ror64(w[i - 2], 19) ^ ror64(w[i - 2], 61) ^ w[i - 2] >> 6;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 80; i++) {
        uint64_t t1 = k + (ror64(e, 14) ^ ror64(e, 18) ^ ror64(e, 41)) + ((e & f) ^ (~e & g)) + K512[i] + w[i];
        uint64_t t2 = (ror64(a, 28) ^ ror64(a, 34) ^ ror64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

// --- either, over the pieces of a message -------------------------------------
//
// A hash fed its message in pieces, so HMAC can hash a key's pad and then
// the message without joining them.

typedef struct {
    int      wide;                      // SHA-512: 128-byte blocks, 64-bit words
    uint32_t h32[8];
    uint64_t h64[8];
    uint8_t  block[128];
    size_t   used;                      // bytes in block
    uint64_t total;                     // bytes fed in all
} hash_t;

static const uint32_t IV256[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};
static const uint64_t IV512[8] = {
    0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
    0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179,
};

static size_t block_size(const hash_t *s) { return s->wide ? 128 : 64; }
static size_t digest_size(const hash_t *s) { return s->wide ? 64 : 32; }

static void start(hash_t *s, int wide) {
    memset(s, 0, sizeof *s);
    s->wide = wide;
    memcpy(s->h32, IV256, sizeof IV256);
    memcpy(s->h64, IV512, sizeof IV512);
}

static void feed(hash_t *s, const uint8_t *p, size_t len) {
    size_t bs = block_size(s);
    s->total += len;
    while (len > 0) {
        size_t n = bs - s->used < len ? bs - s->used : len;
        memcpy(s->block + s->used, p, n);
        s->used += n, p += n, len -= n;
        if (s->used == bs) {
            if (s->wide) block512(s->h64, s->block);
            else         block256(s->h32, s->block);
            s->used = 0;
        }
    }
}

// The padding, 0x80 then zeros, then the length in bits, big-endian, in
// the block's last 8 bytes (16 for SHA-512, whose top 8 stay 0 here).
static void finish(hash_t *s, uint8_t *out) {
    size_t   bs   = block_size(s);
    uint64_t bits = s->total * 8;
    uint8_t  pad[144] = { 0x80 };
    size_t   n    = (s->used < bs - (s->wide ? 16 : 8) ? bs : 2 * bs) - s->used;
    for (int i = 0; i < 8; i++) pad[n - 1 - i] = (uint8_t)(bits >> (8 * i));
    feed(s, pad, n);
    for (int i = 0; i < 8; i++) {
        if (s->wide) for (int j = 0; j < 8; j++) out[8 * i + j] = (uint8_t)(s->h64[i] >> (56 - 8 * j));
        else         for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(s->h32[i] >> (24 - 8 * j));
    }
}

static const uint8_t *bytes_of(rt_value_t s, const char *site) {
    if (!rt_is_string(s)) rt_fault(RT_FAULT_NOT_STRING, s, site);
    return (const uint8_t *)rt_string_bytes(s);
}

static rt_value_t digest(rt_value_t msg, int wide, const char *site) {
    const uint8_t *p = bytes_of(msg, site);
    hash_t         s;
    uint8_t        out[64];
    start(&s, wide);
    feed(&s, p, rt_string_len(msg));
    finish(&s, out);
    return rt_new_string((const char *)out, digest_size(&s), site);
}

rt_value_t rt_crypto_sha256(rt_value_t msg, const char *site) { return digest(msg, 0, site); }
rt_value_t rt_crypto_sha512(rt_value_t msg, const char *site) { return digest(msg, 1, site); }

// HMAC (RFC 2104): H((K ^ opad) || H((K ^ ipad) || msg)), K the key, or
// its hash if it's longer than a block, padded with zeros to one.
static rt_value_t hmac(rt_value_t key, rt_value_t msg, int wide, const char *site) {
    const uint8_t *k = bytes_of(key, site), *m = bytes_of(msg, site);
    size_t         klen = rt_string_len(key);
    hash_t         s;
    uint8_t        k0[128] = { 0 }, pad[128], inner[64];
    start(&s, wide);
    size_t bs = block_size(&s), ds = digest_size(&s);
    if (klen > bs) {
        feed(&s, k, klen);
        finish(&s, k0);
        start(&s, wide);
    } else {
        memcpy(k0, k, klen);
    }
    for (size_t i = 0; i < bs; i++) pad[i] = k0[i] ^ 0x36;
    feed(&s, pad, bs);
    feed(&s, m, rt_string_len(msg));
    finish(&s, inner);
    start(&s, wide);
    for (size_t i = 0; i < bs; i++) pad[i] = k0[i] ^ 0x5c;
    feed(&s, pad, bs);
    feed(&s, inner, ds);
    uint8_t out[64];
    finish(&s, out);
    return rt_new_string((const char *)out, ds, site);
}

rt_value_t rt_crypto_hmac_sha256(rt_value_t key, rt_value_t msg, const char *site) { return hmac(key, msg, 0, site); }
rt_value_t rt_crypto_hmac_sha512(rt_value_t key, rt_value_t msg, const char *site) { return hmac(key, msg, 1, site); }

// Two strings the same, in a time that depends on their length alone, not
// on where they differ, so checking a MAC tells an attacker nothing about
// how near a guess was. Lengths that differ say so at once: a MAC's length
// is no secret.
rt_value_t rt_crypto_equal(rt_value_t a, rt_value_t b, const char *site) {
    const volatile uint8_t *p = bytes_of(a, site), *q = bytes_of(b, site);
    size_t                  n = rt_string_len(a);
    if (n != rt_string_len(b)) return RT_FALSE;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= p[i] ^ q[i];
    return diff == 0 ? RT_TRUE : RT_FALSE;
}

// n bytes from the system's entropy, for keys and tokens: never rand/'s,
// which a seed makes the same every run (D222). Recorded, so a replay has
// the same ones. At most 1 MB a call.
rt_value_t rt_crypto_random_bytes(rt_value_t n, const char *site) {
    int64_t len = rt_need_int(n, site);
    if (len < 0 || len > 1 << 20) rt_fault(RT_FAULT_RANGE, n, site);
    uint8_t *buf = rt_must(malloc(len ? (size_t)len : 1));
    rt_io_random_bytes(buf, (size_t)len);
    rt_value_t s = rt_new_string((const char *)buf, (size_t)len, site);
    free(buf);
    return s;
}
