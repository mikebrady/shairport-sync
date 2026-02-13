#include <stdint.h>
#include <stdbool.h>
#include <sodium.h>

#include "pair.h"

#define RETURN_ERROR(s, m) \
  do { handle->status = (s); handle->errmsg = (m); goto error; } while(0)


struct SRPUser;
struct SRPVerifier;

struct pair_client_setup_context
{
  struct SRPUser *user;

  char *pin;
  char device_id[PAIR_AP_DEVICE_ID_LEN_MAX];

  pair_cb add_cb;
  void *add_cb_arg;

  uint8_t public_key[crypto_sign_PUBLICKEYBYTES];
  uint8_t private_key[crypto_sign_SECRETKEYBYTES];

  const uint8_t *pkA;
  int pkA_len;

  uint8_t *pkB;
  uint64_t pkB_len;

  const uint8_t *M1;
  int M1_len;

  uint8_t *M2;
  uint64_t M2_len;

  uint8_t *salt;
  uint64_t salt_len;

  // We don't actually use the server's epk and authtag for anything
  uint8_t *epk;
  uint64_t epk_len;
  uint8_t *authtag;
  uint64_t authtag_len;
};

struct pair_server_setup_context
{
  struct SRPVerifier *verifier;

  char *pin;
  char device_id[PAIR_AP_DEVICE_ID_LEN_MAX];

  pair_cb add_cb;
  void *add_cb_arg;

  uint8_t public_key[crypto_sign_PUBLICKEYBYTES];
  uint8_t private_key[crypto_sign_SECRETKEYBYTES];

  bool is_transient;

  uint8_t *pkA;
  uint64_t pkA_len;

  uint8_t *pkB;
  int pkB_len;

  uint8_t *b;
  int b_len;

  uint8_t *M1;
  uint64_t M1_len;

  const uint8_t *M2;
  int M2_len;

  uint8_t *v;
  int v_len;

  uint8_t *salt;
  int salt_len;
};

enum pair_status
{
  PAIR_STATUS_IN_PROGRESS,
  PAIR_STATUS_COMPLETED,
  PAIR_STATUS_AUTH_FAILED,
  PAIR_STATUS_INVALID,
};

struct pair_setup_context
{
  struct pair_definition *type;

  enum pair_status status;
  const char *errmsg;

  struct pair_result result;
  char result_str[256]; // Holds the hex string version of the keys that pair_verify_new() needs

  // Hex-formatet concatenation of public + private, 0-terminated
  char auth_key[2 * (crypto_sign_PUBLICKEYBYTES + crypto_sign_SECRETKEYBYTES) + 1];

  union pair_setup_union
  {
    struct pair_client_setup_context client;
    struct pair_server_setup_context server;
  } sctx;
};

struct pair_client_verify_context
{
  char device_id[PAIR_AP_DEVICE_ID_LEN_MAX];

  // These are the keys that were registered with the server in pair-setup
  uint8_t client_public_key[crypto_sign_PUBLICKEYBYTES]; // 32
  uint8_t client_private_key[crypto_sign_SECRETKEYBYTES]; // 64

  bool verify_server_signature;
  uint8_t server_fruit_public_key[64]; // Not sure why it has this length in fruit mode
  uint8_t server_public_key[crypto_sign_PUBLICKEYBYTES]; // 32

  // For establishing the shared secret for encrypted communication
  uint8_t client_eph_public_key[crypto_box_PUBLICKEYBYTES]; // 32
  uint8_t client_eph_private_key[crypto_box_SECRETKEYBYTES]; // 32

  uint8_t server_eph_public_key[crypto_box_PUBLICKEYBYTES]; // 32

  uint8_t shared_secret[crypto_scalarmult_BYTES]; // 32
};

struct pair_server_verify_context
{
  char device_id[PAIR_AP_DEVICE_ID_LEN_MAX];

  // Same keys as used for pair-setup, derived from device_id
  uint8_t server_public_key[crypto_sign_PUBLICKEYBYTES]; // 32
  uint8_t server_private_key[crypto_sign_SECRETKEYBYTES]; // 64

  bool verify_client_signature;
  pair_cb get_cb;
  void *get_cb_arg;

  // For establishing the shared secret for encrypted communication
  uint8_t server_eph_public_key[crypto_box_PUBLICKEYBYTES]; // 32
  uint8_t server_eph_private_key[crypto_box_SECRETKEYBYTES]; // 32

  uint8_t client_eph_public_key[crypto_box_PUBLICKEYBYTES]; // 32

  uint8_t shared_secret[crypto_scalarmult_BYTES]; // 32
};

struct pair_verify_context
{
  struct pair_definition *type;

  enum pair_status status;
  const char *errmsg;

  struct pair_result result;

  union pair_verify_union
  {
    struct pair_client_verify_context client;
    struct pair_server_verify_context server;
  } vctx;
};

struct pair_cipher_context
{
  struct pair_definition *type;

  uint8_t encryption_key[32];
  uint8_t decryption_key[32];

  uint64_t encryption_counter;
  uint64_t decryption_counter;

  // For rollback
  uint64_t encryption_counter_prev;
  uint64_t decryption_counter_prev;

  const char *errmsg;
};

struct pair_definition
{
  int (*pair_setup_new)(struct pair_setup_context *sctx, const char *pin, pair_cb add_cb, void *cb_arg, const char *device_id);
  void (*pair_setup_free)(struct pair_setup_context *sctx);
  int (*pair_setup_result)(struct pair_setup_context *sctx);

  uint8_t *(*pair_setup_request1)(size_t *len, struct pair_setup_context *sctx);
  uint8_t *(*pair_setup_request2)(size_t *len, struct pair_setup_context *sctx);
  uint8_t *(*pair_setup_request3)(size_t *len, struct pair_setup_context *sctx);

  int (*pair_setup_response1)(struct pair_setup_context *sctx, const uint8_t *in, size_t in_len);
  int (*pair_setup_response2)(struct pair_setup_context *sctx, const uint8_t *in, size_t in_len);
  int (*pair_setup_response3)(struct pair_setup_context *sctx, const uint8_t *in, size_t in_len);

  int (*pair_verify_new)(struct pair_verify_context *vctx, const char *client_setup_keys, pair_cb cb, void *cb_arg, const char *device_id);
  void (*pair_verify_free)(struct pair_verify_context *vctx);
  int (*pair_verify_result)(struct pair_verify_context *vctx);

  uint8_t *(*pair_verify_request1)(size_t *len, struct pair_verify_context *vctx);
  uint8_t *(*pair_verify_request2)(size_t *len, struct pair_verify_context *vctx);

  int (*pair_verify_response1)(struct pair_verify_context *vctx, const uint8_t *in, size_t in_len);
  int (*pair_verify_response2)(struct pair_verify_context *vctx, const uint8_t *in, size_t in_len);

  int (*pair_add)(uint8_t **out, size_t *out_len, pair_cb cb, void *cb_arg, const uint8_t *in, size_t in_len);
  int (*pair_remove)(uint8_t **out, size_t *out_len, pair_cb cb, void *cb_arg, const uint8_t *in, size_t in_len);
  int (*pair_list)(uint8_t **out, size_t *out_len, pair_list_cb cb, void *cb_arg, const uint8_t *in, size_t in_len);

  struct pair_cipher_context *(*pair_cipher_new)(struct pair_definition *type, int channel, const uint8_t *shared_secret, size_t shared_secret_len, const char *dynamic_salt_suffix);
  void (*pair_cipher_free)(struct pair_cipher_context *cctx);

  ssize_t (*pair_encrypt)(uint8_t **ciphertext, size_t *ciphertext_len, const uint8_t *plaintext, size_t plaintext_len, struct pair_cipher_context *cctx);
  ssize_t (*pair_decrypt)(uint8_t **plaintext, size_t *plaintext_len, const uint8_t *ciphertext, size_t ciphertext_len, struct pair_cipher_context *cctx);

  int (*pair_state_get)(const char **errmsg, const uint8_t *in, size_t in_len);
  void (*pair_public_key_get)(uint8_t server_public_key[32], const char *device_id);
};


/* ----------------------------- INITIALIZATION ---------------------------- */

bool
is_initialized(void);


/* -------------------- GCRYPT AND OPENSSL COMPABILITY --------------------- */
/*                   partly borrowed from ffmpeg (rtmpdh.c)                  */

#if CONFIG_GCRYPT
#include <gcrypt.h>
#define SHA512_DIGEST_LENGTH 64
#define bnum_new(bn)                                            \
    do {                                                        \
        if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) { \
            if (!gcry_check_version("1.5.4"))                   \
                abort();                                        \
            gcry_control(GCRYCTL_DISABLE_SECMEM, 0);            \
            gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);   \
        }                                                       \
        bn = gcry_mpi_new(1);                                   \
    } while (0)
#define bnum_free(bn)                 gcry_mpi_release(bn)
#define bnum_num_bytes(bn)            (gcry_mpi_get_nbits(bn) + 7) / 8
#define bnum_is_zero(bn)              (gcry_mpi_cmp_ui(bn, (unsigned long)0) == 0)
#define bnum_bn2bin(bn, buf, len)     gcry_mpi_print(GCRYMPI_FMT_USG, buf, len, NULL, bn)
#define bnum_bin2bn(bn, buf, len)     gcry_mpi_scan(&bn, GCRYMPI_FMT_USG, buf, len, NULL)
#define bnum_hex2bn(bn, buf)          gcry_mpi_scan(&bn, GCRYMPI_FMT_HEX, buf, 0, 0)
#define bnum_random(bn, num_bits)     gcry_mpi_randomize(bn, num_bits, GCRY_WEAK_RANDOM)
#define bnum_add(bn, a, b)            gcry_mpi_add(bn, a, b)
#define bnum_sub(bn, a, b)            gcry_mpi_sub(bn, a, b)
#define bnum_mul(bn, a, b)            gcry_mpi_mul(bn, a, b)
#define bnum_mod(bn, a, b)            gcry_mpi_mod(bn, a, b)
typedef gcry_mpi_t bnum;
__attribute__((unused)) static void bnum_modexp(bnum bn, bnum y, bnum q, bnum p)
{
  gcry_mpi_powm(bn, y, q, p);
}
__attribute__((unused)) static void bnum_modadd(bnum bn, bnum a, bnum b, bnum m)
{
  gcry_mpi_addm(bn, a, b, m);
}
#elif CONFIG_OPENSSL
#include <openssl/crypto.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#define bnum_new(bn)                  bn = BN_new()
#define bnum_free(bn)                 BN_free(bn)
#define bnum_num_bytes(bn)            BN_num_bytes(bn)
#define bnum_is_zero(bn)              BN_is_zero(bn)
#define bnum_bn2bin(bn, buf, len)     BN_bn2bin(bn, buf)
#define bnum_bin2bn(bn, buf, len)     bn = BN_bin2bn(buf, len, 0)
#define bnum_hex2bn(bn, buf)          BN_hex2bn(&bn, buf)
#define bnum_random(bn, num_bits)     BN_rand(bn, num_bits, 0, 0)
#define bnum_add(bn, a, b)            BN_add(bn, a, b)
#define bnum_sub(bn, a, b)            BN_sub(bn, a, b)
typedef BIGNUM* bnum;
__attribute__((unused)) static void bnum_mul(bnum bn, bnum a, bnum b)
{
  // No error handling
  BN_CTX *ctx = BN_CTX_new();
  BN_mul(bn, a, b, ctx);
  BN_CTX_free(ctx);
}
__attribute__((unused)) static void bnum_mod(bnum bn, bnum a, bnum b)
{
  // No error handling
  BN_CTX *ctx = BN_CTX_new();
  BN_mod(bn, a, b, ctx);
  BN_CTX_free(ctx);
}
__attribute__((unused)) static void bnum_modexp(bnum bn, bnum y, bnum q, bnum p)
{
  // No error handling
  BN_CTX *ctx = BN_CTX_new();
  BN_mod_exp(bn, y, q, p, ctx);
  BN_CTX_free(ctx);
}
__attribute__((unused)) static void bnum_modadd(bnum bn, bnum a, bnum b, bnum m)
{
  // No error handling
  BN_CTX *ctx = BN_CTX_new();
  BN_mod_add(bn, a, b, m, ctx);
  BN_CTX_free(ctx);
}
#endif


/* -------------------------- SHARED HASHING HELPERS ------------------------ */

#ifdef CONFIG_OPENSSL
enum hash_alg
{
  HASH_SHA1,
  HASH_SHA224,
  HASH_SHA256,
  HASH_SHA384,
  HASH_SHA512,
};
#elif CONFIG_GCRYPT
enum hash_alg
{
  HASH_SHA1 = GCRY_MD_SHA1,
  HASH_SHA224 = GCRY_MD_SHA224,
  HASH_SHA256 = GCRY_MD_SHA256,
  HASH_SHA384 = GCRY_MD_SHA384,
  HASH_SHA512 = GCRY_MD_SHA512,
};
#endif

#if CONFIG_OPENSSL
typedef union
{
  SHA_CTX    sha;
  SHA256_CTX sha256;
  SHA512_CTX sha512;
} HashCTX;
#elif CONFIG_GCRYPT
typedef gcry_md_hd_t HashCTX;
#endif

int
hash_init(enum hash_alg alg, HashCTX *c);

int
hash_update(enum hash_alg alg, HashCTX *c, const void *data, size_t len);

int
hash_final(enum hash_alg alg, HashCTX *c, unsigned char *md);

unsigned char *
hash(enum hash_alg alg, const unsigned char *d, size_t n, unsigned char *md);

int
hash_length(enum hash_alg alg);

int
hash_ab(enum hash_alg alg, unsigned char *md, const unsigned char *m1, int m1_len, const unsigned char *m2, int m2_len);

bnum
H_nn_pad(enum hash_alg alg, const bnum n1, const bnum n2, int padded_len);

bnum
H_ns(enum hash_alg alg, const bnum n, const unsigned char *bytes, int len_bytes);

void
update_hash_n(enum hash_alg alg, HashCTX *ctx, const bnum n);

void
hash_num(enum hash_alg alg, const bnum n, unsigned char *dest);


/* ----------------------------- OTHER HELPERS -------------------------------*/

#ifdef DEBUG_PAIR
void
hexdump(const char *msg, uint8_t *mem, size_t len);

void
bnum_dump(const char *msg, bnum n);
#endif
