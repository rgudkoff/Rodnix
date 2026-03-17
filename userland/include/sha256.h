#ifndef _RODNIX_SHA256_H
#define _RODNIX_SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_DIGEST_LEN  32
#define SHA256_BLOCK_LEN   64

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[SHA256_BLOCK_LEN];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t* ctx);
void sha256_update(sha256_ctx_t* ctx, const void* data, size_t len);
void sha256_final(sha256_ctx_t* ctx, uint8_t digest[SHA256_DIGEST_LEN]);
void sha256(const void* data, size_t len, uint8_t digest[SHA256_DIGEST_LEN]);

/*
 * Password hashing for /etc/passwd.
 * Format stored: $rdnx$<64-char hex of SHA-256(password)>
 * out must be at least 73 bytes.
 */
void rdnx_pw_hash(const char* password, char* out, size_t outsz);

/* Returns 1 if password matches stored hash, 0 otherwise. */
int rdnx_pw_verify(const char* password, const char* stored_hash);

#endif /* _RODNIX_SHA256_H */
