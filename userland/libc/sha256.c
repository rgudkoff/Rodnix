/*
 * sha256.c — minimal SHA-256 implementation for RodNIX userland.
 * Password storage format: $rdnx$<64 hex chars>
 */

#include <sha256.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define RR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_transform(sha256_ctx_t* ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4+0] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] <<  8) |
               ((uint32_t)block[i*4+3]);
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = RR(w[i-15], 7) ^ RR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = RR(w[i-2], 17) ^ RR(w[i-2], 19)  ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t S1  = RR(e, 6) ^ RR(e, 11) ^ RR(e, 25);
        uint32_t ch  = (e & f) ^ (~e & g);
        uint32_t tmp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0  = RR(a, 2) ^ RR(a, 13) ^ RR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t tmp2 = S0 + maj;

        h = g; g = f; f = e; e = d + tmp1;
        d = c; c = b; b = a; a = tmp1 + tmp2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t* ctx)
{
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->count = 0;
}

void sha256_update(sha256_ctx_t* ctx, const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    size_t used = (size_t)(ctx->count & 63);

    ctx->count += (uint64_t)len;

    if (used) {
        size_t need = 64 - used;
        if (len < need) {
            memcpy(ctx->buf + used, p, len);
            return;
        }
        memcpy(ctx->buf + used, p, need);
        sha256_transform(ctx, ctx->buf);
        p += need;
        len -= need;
    }

    while (len >= 64) {
        sha256_transform(ctx, p);
        p += 64;
        len -= 64;
    }

    if (len) {
        memcpy(ctx->buf, p, len);
    }
}

void sha256_final(sha256_ctx_t* ctx, uint8_t digest[SHA256_DIGEST_LEN])
{
    size_t used = (size_t)(ctx->count & 63);
    uint64_t bits = ctx->count * 8u;
    int i;

    ctx->buf[used++] = 0x80;
    if (used > 56) {
        memset(ctx->buf + used, 0, 64 - used);
        sha256_transform(ctx, ctx->buf);
        used = 0;
    }
    memset(ctx->buf + used, 0, 56 - used);

    /* big-endian bit count */
    ctx->buf[56] = (uint8_t)(bits >> 56);
    ctx->buf[57] = (uint8_t)(bits >> 48);
    ctx->buf[58] = (uint8_t)(bits >> 40);
    ctx->buf[59] = (uint8_t)(bits >> 32);
    ctx->buf[60] = (uint8_t)(bits >> 24);
    ctx->buf[61] = (uint8_t)(bits >> 16);
    ctx->buf[62] = (uint8_t)(bits >>  8);
    ctx->buf[63] = (uint8_t)(bits);
    sha256_transform(ctx, ctx->buf);

    for (i = 0; i < 8; i++) {
        digest[i*4+0] = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >>  8);
        digest[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}

void sha256(const void* data, size_t len, uint8_t digest[SHA256_DIGEST_LEN])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* --- Password helpers --- */

static const char hex_chars[] = "0123456789abcdef";

static void bytes_to_hex(const uint8_t* b, size_t blen, char* out)
{
    for (size_t i = 0; i < blen; i++) {
        out[i*2+0] = hex_chars[b[i] >> 4];
        out[i*2+1] = hex_chars[b[i] & 0xf];
    }
    out[blen*2] = '\0';
}

#define PW_PREFIX     "$rdnx$"
#define PW_PREFIX_LEN 6
/* stored field: "$rdnx$" + 64 hex chars = 70 chars + NUL */

void rdnx_pw_hash(const char* password, char* out, size_t outsz)
{
    uint8_t digest[SHA256_DIGEST_LEN];
    char hex[SHA256_DIGEST_LEN * 2 + 1];
    size_t needed = PW_PREFIX_LEN + SHA256_DIGEST_LEN * 2 + 1;

    if (!out || outsz < needed) return;

    sha256(password, strlen(password), digest);
    bytes_to_hex(digest, SHA256_DIGEST_LEN, hex);

    memcpy(out, PW_PREFIX, PW_PREFIX_LEN);
    memcpy(out + PW_PREFIX_LEN, hex, SHA256_DIGEST_LEN * 2 + 1);
}

int rdnx_pw_verify(const char* password, const char* stored)
{
    if (!password || !stored) return 0;

    /* Legacy: empty stored password means no password required. */
    if (stored[0] == '\0') return 1;

    /* Locked account markers */
    if ((stored[0] == '*' || stored[0] == '!') && stored[1] == '\0') return 0;

    /* Hashed password */
    if (memcmp(stored, PW_PREFIX, PW_PREFIX_LEN) == 0) {
        char computed[PW_PREFIX_LEN + SHA256_DIGEST_LEN * 2 + 1];
        rdnx_pw_hash(password, computed, sizeof(computed));
        /* Constant-time compare */
        const char* a = computed;
        const char* b = stored;
        int diff = 0;
        size_t len = PW_PREFIX_LEN + SHA256_DIGEST_LEN * 2;
        for (size_t i = 0; i < len; i++) {
            diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
        }
        return diff == 0 ? 1 : 0;
    }

    /* Plaintext fallback (for migration from old passwd files) */
    return (strcmp(password, stored) == 0) ? 1 : 0;
}
