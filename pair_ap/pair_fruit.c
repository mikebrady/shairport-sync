/*
 *
 * The Secure Remote Password 6a implementation is adapted from:
 *  - Tom Cocagne
 *    <https://github.com/cocagne/csrp>
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <plist/plist.h>
#include <sodium.h>

#include <assert.h>

#include "pair-internal.h"

/* ----------------------------- DEFINES ETC ------------------------------- */

#define USERNAME "12:34:56:78:90:AB"
#define EPK_LENGTH 32
#define AUTHTAG_LENGTH 16
#define AES_SETUP_KEY  "Pair-Setup-AES-Key"
#define AES_SETUP_IV   "Pair-Setup-AES-IV"
#define AES_VERIFY_KEY "Pair-Verify-AES-Key"
#define AES_VERIFY_IV  "Pair-Verify-AES-IV"


/* ---------------------------------- SRP ---------------------------------- */

typedef enum
{
  SRP_NG_2048,
  SRP_NG_CUSTOM
} SRP_NGType;

typedef struct
{
  bnum N;
  bnum g;
  int N_len;
} NGConstant;

struct SRPUser
{
  enum hash_alg     alg;
  NGConstant        *ng;

  bnum a;
  bnum A;
  bnum S;

  const unsigned char *bytes_A;
  int           authenticated;

  char          *username;
  unsigned char *password;
  int           password_len;

  unsigned char M           [SHA512_DIGEST_LENGTH];
  unsigned char H_AMK       [SHA512_DIGEST_LENGTH];
  unsigned char session_key [2 * SHA512_DIGEST_LENGTH]; // See hash_session_key()
  int           session_key_len;
};

struct NGHex
{
  const char *n_hex;
  const char *g_hex;
};

// We only need 2048 right now, but keep the array in case we want to add others later
// All constants here were pulled from Appendix A of RFC 5054
static struct NGHex global_Ng_constants[] =
{
  { /* 2048 */
    "AC6BDB41324A9A9BF166DE5E1389582FAF72B6651987EE07FC3192943DB56050A37329CBB4"
    "A099ED8193E0757767A13DD52312AB4B03310DCD7F48A9DA04FD50E8083969EDB767B0CF60"
    "95179A163AB3661A05FBD5FAAAE82918A9962F0B93B855F97993EC975EEAA80D740ADBF4FF"
    "747359D041D5C33EA71D281E446B14773BCA97B43A23FB801676BD207A436C6481F1D2B907"
    "8717461A5B9D32E688F87748544523B524B0D57D5EA77A2775D2ECFA032CFBDBF52FB37861"
    "60279004E57AE6AF874E7303CE53299CCC041C7BC308D82A5698F3A8D0C38271AE35F8E9DB"
    "FBB694B5C803D89F7AE435DE236D525F54759B65E372FCD68EF20FA7111F9E4AFF73",
    "2"
  },
  {0,0} /* null sentinel */
};


static NGConstant *
new_ng(SRP_NGType ng_type, const char *n_hex, const char *g_hex)
{
  NGConstant *ng = calloc(1, sizeof(NGConstant));

  if ( ng_type != SRP_NG_CUSTOM )
    {
      n_hex = global_Ng_constants[ ng_type ].n_hex;
      g_hex = global_Ng_constants[ ng_type ].g_hex;
    }

  bnum_hex2bn(ng->N, n_hex);
  bnum_hex2bn(ng->g, g_hex);

  ng->N_len = bnum_num_bytes(ng->N);

  return ng;
}

static void
free_ng(NGConstant * ng)
{
  if (!ng)
    return;

  bnum_free(ng->N);
  bnum_free(ng->g);
  free(ng);
}

static bnum
calculate_x(enum hash_alg alg, const bnum salt, const char *username, const unsigned char *password, int password_len)
{
  unsigned char ucp_hash[SHA512_DIGEST_LENGTH];
  HashCTX       ctx;

  hash_init( alg, &ctx );
  hash_update( alg, &ctx, username, strlen(username) );
  hash_update( alg, &ctx, ":", 1 );
  hash_update( alg, &ctx, password, password_len );
  hash_final( alg, &ctx, ucp_hash );

  return H_ns( alg, salt, ucp_hash, hash_length(alg) );
}

static int
hash_session_key(enum hash_alg alg, const bnum n, unsigned char *dest)
{
  int           nbytes = bnum_num_bytes(n);
  unsigned char *bin   = malloc(nbytes);
  unsigned char fourbytes[4] = { 0 }; // Only God knows the reason for this, and perhaps some poor soul at Apple

  bnum_bn2bin(n, bin, nbytes);

  hash_ab(alg, dest, bin, nbytes, fourbytes, sizeof(fourbytes));

  fourbytes[3] = 1; // Again, only ...

  hash_ab(alg, dest + hash_length(alg), bin, nbytes, fourbytes, sizeof(fourbytes));

  free(bin);

  return (2 * hash_length(alg));
}

static void
calculate_M(enum hash_alg alg, NGConstant *ng, unsigned char *dest, const char *I, const bnum s,
            const bnum A, const bnum B, const unsigned char *K, int K_len)
{
  unsigned char H_N[ SHA512_DIGEST_LENGTH ];
  unsigned char H_g[ SHA512_DIGEST_LENGTH ];
  unsigned char H_I[ SHA512_DIGEST_LENGTH ];
  unsigned char H_xor[ SHA512_DIGEST_LENGTH ];
  HashCTX       ctx;
  int           i = 0;
  int           hash_len = hash_length(alg);

  hash_num( alg, ng->N, H_N );
  hash_num( alg, ng->g, H_g );

  hash(alg, (const unsigned char *)I, strlen(I), H_I);

  for (i=0; i < hash_len; i++ )
    H_xor[i] = H_N[i] ^ H_g[i];
    
  hash_init( alg, &ctx );

  hash_update( alg, &ctx, H_xor, hash_len );
  hash_update( alg, &ctx, H_I,   hash_len );
  update_hash_n( alg, &ctx, s );
  update_hash_n( alg, &ctx, A );
  update_hash_n( alg, &ctx, B );
  hash_update( alg, &ctx, K, K_len );

  hash_final( alg, &ctx, dest );
}

static void
calculate_H_AMK(enum hash_alg alg, unsigned char *dest, const bnum A, const unsigned char * M, const unsigned char * K, int K_len)
{
  HashCTX ctx;

  hash_init( alg, &ctx );

  update_hash_n( alg, &ctx, A );
  hash_update( alg, &ctx, M, hash_length(alg) );
  hash_update( alg, &ctx, K, K_len );

  hash_final( alg, &ctx, dest );
}

static struct SRPUser *
srp_user_new(enum hash_alg alg, SRP_NGType ng_type, const char *username,
             const unsigned char *bytes_password, int len_password,
             const char *n_hex, const char *g_hex)
{
  struct SRPUser  *usr  = calloc(1, sizeof(struct SRPUser));
  int              ulen = strlen(username) + 1;

  if (!usr)
    goto err_exit;

  usr->alg = alg;
  usr->ng  = new_ng( ng_type, n_hex, g_hex );

  bnum_new(usr->a);
  bnum_new(usr->A);
  bnum_new(usr->S);

  if (!usr->ng || !usr->a || !usr->A || !usr->S)
    goto err_exit;

  usr->username     = malloc(ulen);
  usr->password     = malloc(len_password);
  usr->password_len = len_password;

  if (!usr->username || !usr->password)
    goto err_exit;

  memcpy(usr->username, username,       ulen);
  memcpy(usr->password, bytes_password, len_password);

  usr->authenticated = 0;
  usr->bytes_A = 0;

  return usr;

 err_exit:
  if (!usr)
    return NULL;

  bnum_free(usr->a);
  bnum_free(usr->A);
  bnum_free(usr->S);

  free(usr->username);
  if (usr->password)
    {
      memset(usr->password, 0, usr->password_len);
      free(usr->password);
    }
  free(usr);

  return NULL;
}

static void
srp_user_free(struct SRPUser *usr)
{
  if(!usr)
    return;

  bnum_free(usr->a);
  bnum_free(usr->A);
  bnum_free(usr->S);

  free_ng(usr->ng);

  memset(usr->password, 0, usr->password_len);

  free(usr->username);
  free(usr->password);
  free((char *)usr->bytes_A);

  memset(usr, 0, sizeof(*usr));
  free(usr);
}

static int
srp_user_is_authenticated(struct SRPUser *usr)
{
  return usr->authenticated;
}

static const unsigned char *
srp_user_get_session_key(struct SRPUser *usr, int *key_length)
{
  if (key_length)
    *key_length = usr->session_key_len;
  return usr->session_key;
}

/* Output: username, bytes_A, len_A */
static void
srp_user_start_authentication(struct SRPUser *usr, const char **username,
                              const unsigned char **bytes_A, int *len_A)
{
  bnum_random(usr->a, 256);
  bnum_modexp(usr->A, usr->ng->g, usr->a, usr->ng->N);

  *len_A   = bnum_num_bytes(usr->A);
  *bytes_A = malloc(*len_A);

  if (!*bytes_A)
    {
      *len_A = 0;
      *bytes_A = 0;
      *username = 0;
      return;
    }

  bnum_bn2bin(usr->A, (unsigned char *) *bytes_A, *len_A);

  usr->bytes_A = *bytes_A;
  *username = usr->username;
}

/* Output: bytes_M. Buffer length is SHA512_DIGEST_LENGTH */
static void
srp_user_process_challenge(struct SRPUser *usr, const unsigned char *bytes_s, int len_s,
                           const unsigned char *bytes_B, int len_B,
                           const unsigned char **bytes_M, int *len_M )
{
  bnum s, B, k, v;
  bnum tmp1, tmp2, tmp3;
  bnum u, x;

  *len_M = 0;
  *bytes_M = NULL;

  bnum_bin2bn(s, bytes_s, len_s);
  bnum_bin2bn(B, bytes_B, len_B);
  k    = H_nn_pad(usr->alg, usr->ng->N, usr->ng->g, usr->ng->N_len);
  bnum_new(v);
  bnum_new(tmp1);
  bnum_new(tmp2);
  bnum_new(tmp3);

  if (!s || !B || !k || !v || !tmp1 || !tmp2 || !tmp3)
    goto cleanup1;

  u = H_nn_pad(usr->alg, usr->A, B, usr->ng->N_len);
  x = calculate_x(usr->alg, s, usr->username, usr->password, usr->password_len);
  if (!u || !x)
    goto cleanup2;

  // SRP-6a safety check
  if (!bnum_is_zero(B) && !bnum_is_zero(u))
    {
      bnum_modexp(v, usr->ng->g, x, usr->ng->N);

      // S = (B - k*(g^x)) ^ (a + ux)
      bnum_mul(tmp1, u, x);
      bnum_add(tmp2, usr->a, tmp1);        // tmp2 = (a + ux)
      bnum_modexp(tmp1, usr->ng->g, x, usr->ng->N);
      bnum_mul(tmp3, k, tmp1);             // tmp3 = k*(g^x)
      bnum_sub(tmp1, B, tmp3);             // tmp1 = (B - K*(g^x))
      bnum_modexp(usr->S, tmp1, tmp2, usr->ng->N);

      usr->session_key_len = hash_session_key(usr->alg, usr->S, usr->session_key);

      calculate_M(usr->alg, usr->ng, usr->M, usr->username, s, usr->A, B, usr->session_key, usr->session_key_len);
      calculate_H_AMK(usr->alg, usr->H_AMK, usr->A, usr->M, usr->session_key, usr->session_key_len);

      *bytes_M = usr->M;
      *len_M = hash_length(usr->alg);
    }

 cleanup2:
  bnum_free(x);
  bnum_free(u);
 cleanup1:
  bnum_free(tmp3);
  bnum_free(tmp2);
  bnum_free(tmp1);
  bnum_free(v);
  bnum_free(k);
  bnum_free(B);
  bnum_free(s);
}

static void
srp_user_verify_session(struct SRPUser *usr, const unsigned char *bytes_HAMK)
{
  if (memcmp(usr->H_AMK, bytes_HAMK, hash_length(usr->alg)) == 0)
    usr->authenticated = 1;
}


/* -------------------------------- HELPERS -------------------------------- */

static int
encrypt_gcm(unsigned char *ciphertext, int ciphertext_len, unsigned char *tag, unsigned char *plaintext, int plaintext_len, unsigned char *key, unsigned char *iv, const char **errmsg)
{
#ifdef CONFIG_OPENSSL
  EVP_CIPHER_CTX *ctx;
  int len;

  *errmsg = NULL;

  if ( !(ctx = EVP_CIPHER_CTX_new()) ||
       (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1) ||
       (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL) != 1) ||
       (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) )
    {
      *errmsg = "Error initialising AES 128 GCM encryption";
      goto error;
    }

  if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len) != 1)
    {
      *errmsg = "Error GCM encrypting";
      goto error;
    }

  if (len > ciphertext_len)
    {
      *errmsg = "Bug! Buffer overflow";
      goto error;
    }

  if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) != 1)
    {
      *errmsg = "Error finalising GCM encryption";
      goto error;
    }

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AUTHTAG_LENGTH, tag) != 1)
    {
      *errmsg = "Error getting authtag";
      goto error;
    }

  EVP_CIPHER_CTX_free(ctx);
  return 0;

 error:
  EVP_CIPHER_CTX_free(ctx);
  return -1;
#elif CONFIG_GCRYPT
  gcry_cipher_hd_t hd;
  gcry_error_t err;

  err = gcry_cipher_open(&hd, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_GCM, 0);
  if (err)
    {
      *errmsg = "Error initialising AES 128 GCM encryption";
      return -1;
    }

  err = gcry_cipher_setkey(hd, key, gcry_cipher_get_algo_keylen(GCRY_CIPHER_AES128));
  if (err)
    {
      *errmsg = "Could not set key for AES 128 GCM";
      goto error;
    }

  err = gcry_cipher_setiv(hd, iv, gcry_cipher_get_algo_blklen(GCRY_CIPHER_AES128));
  if (err)
    {
      *errmsg = "Could not set iv for AES 128 GCM";
      goto error;
    }

  err = gcry_cipher_encrypt(hd, ciphertext, ciphertext_len, plaintext, plaintext_len);
  if (err)
    {
      *errmsg = "Error GCM encrypting";
      goto error;
    }

  err = gcry_cipher_gettag(hd, tag, AUTHTAG_LENGTH);
  if (err)
    {
      *errmsg = "Error getting authtag";
      goto error;
    }

  gcry_cipher_close(hd);
  return 0;

 error:
  gcry_cipher_close(hd);
  return -1;
#endif
}

static int
encrypt_ctr(unsigned char *ciphertext, int ciphertext_len,
            unsigned char *plaintext1, int plaintext1_len, unsigned char *plaintext2, int plaintext2_len,
            unsigned char *key, unsigned char *iv, const char **errmsg)
{
#ifdef CONFIG_OPENSSL
  EVP_CIPHER_CTX *ctx;
  int len;

  *errmsg = NULL;

  if ( !(ctx = EVP_CIPHER_CTX_new()) || (EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), NULL, key, iv) != 1) )
    {
      *errmsg = "Error initialising AES 128 CTR encryption";
      goto error;
    }

  if ( (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext1, plaintext1_len) != 1) ||
       (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext2, plaintext2_len) != 1) )
    {
      *errmsg = "Error CTR encrypting";
      goto error;
    }

  if (EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) != 1)
    {
      *errmsg = "Error finalising encryption";
      goto error;
    }

  EVP_CIPHER_CTX_free(ctx);
  return 0;

 error:
  EVP_CIPHER_CTX_free(ctx);
  return -1;
#elif CONFIG_GCRYPT
  gcry_cipher_hd_t hd;
  gcry_error_t err;

  err = gcry_cipher_open(&hd, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_CTR, 0);
  if (err)
    {
      *errmsg = "Error initialising AES 128 CTR encryption";
      return -1;
    }
  err = gcry_cipher_setkey(hd, key, gcry_cipher_get_algo_keylen(GCRY_CIPHER_AES128));
  if (err)
    {
      *errmsg = "Could not set key for AES 128 CTR";
      goto error;
    }

  err = gcry_cipher_setctr(hd, iv, gcry_cipher_get_algo_blklen(GCRY_CIPHER_AES128));
  if (err)
    {
      *errmsg = "Could not set iv for AES 128 CTR";
      goto error;
    }

  err = gcry_cipher_encrypt(hd, ciphertext, ciphertext_len, plaintext1, plaintext1_len);
  if (err)
    {
      *errmsg = "Error CTR encrypting plaintext 1";
      goto error;
    }

  err = gcry_cipher_encrypt(hd, ciphertext, ciphertext_len, plaintext2, plaintext2_len);
  if (err)
    {
      *errmsg = "Error CTR encrypting plaintext 2";
      goto error;
    }

  gcry_cipher_close(hd);
  return 0;

 error:
  gcry_cipher_close(hd);
  return -1;
#endif
}


/* -------------------------- IMPLEMENTATION -------------------------------- */

static int
client_setup_new(struct pair_setup_context *handle, const char *pin, __attribute__((unused)) pair_cb add_cb, __attribute__((unused)) void *cb_arg, __attribute__((unused)) const char *device_id)
{
  struct pair_client_setup_context *sctx = &handle->sctx.client;

  if (!is_initialized())
    return -1;

  if (!pin)
    return -1;

  sctx->pin = strdup(pin);
  if (!sctx->pin)
    return -1;

  return 0;
}

static void
client_setup_free(struct pair_setup_context *handle)
{
  struct pair_client_setup_context *sctx = &handle->sctx.client;

  srp_user_free(sctx->user);

  free(sctx->pkB);
  free(sctx->M2);
  free(sctx->salt);
  free(sctx->epk);
  free(sctx->authtag);
  free(sctx->pin);
}

static uint8_t *
client_setup_request1(size_t *len, struct pair_setup_context *handle)
{
  struct pair_client_setup_context *sctx = &handle->sctx.client;
  plist_t dict;
  plist_t method;
  plist_t user;
  uint32_t uint32;
  char *data = NULL; // Necessary to initialize because plist_to_bin() uses value

  sctx->user = srp_user_new(HASH_SHA1, SRP_NG_2048, USERNAME, (unsigned char *)sctx->pin, strlen(sctx->pin), 0, 0);

  dict = plist_new_dict();

  method = plist_new_string("pin");
  user = plist_new_string(USERNAME);

  plist_dict_set_item(dict, "method", method);
  plist_dict_set_item(dict, "user", user);
  plist_to_bin(dict, &data, &uint32);
  plist_free(dict);

  *len = (size_t)uint32;
  return (uint8_t *)data;
}

static uint8_t *
client_setup_request2(size_t *len, struct pair_setup_context *handle)
{
  struct pair_client_setup_context *sctx = &handle->sctx.client;
  plist_t dict;
  plist_t pk;
  plist_t proof;
  const char *auth_username = NULL;
  uint32_t uint32;
  char *data = NULL;

  // Calculate A
  srp_user_start_authentication(sctx->user, &auth_username, &sctx->pkA, &sctx->pkA_len);

  // Calculate M1 (client proof)
  srp_user_process_challenge(sctx->user, (const unsigned char *)sctx->salt, sctx->salt_len, (const unsigned char *)sctx->pkB, sctx->pkB_len, &sctx->M1, &sctx->M1_len);

  pk = plist_new_data((char *)sctx->pkA, sctx->pkA_len);
  proof = plist_new_data((char *)sctx->M1, sctx->M1_len);

  dict = plist_new_dict();
  plist_dict_set_item(dict, "pk", pk);
  plist_dict_set_item(dict, "proof", proof);
  plist_to_bin(dict, &data, &uint32);
  plist_free(dict);

  *len = (size_t)uint32;
  return (uint8_t *)data;
}

static uint8_t *
client_setup_request3(size_t *len, struct pair_setup_context *handle)
{
  struct pair_client_setup_context *sctx = &handle->sctx.client;
  plist_t dict;
  plist_t epk;
  plist_t authtag;
  uint32_t uint32;
  char *data = NULL;
  const unsigned char *session_key;
  int session_key_len;
  unsigned char key[SHA512_DIGEST_LENGTH];
  unsigned char iv[SHA512_DIGEST_LENGTH];
  unsigned char encrypted[128]; // Alloc a bit extra - should only need 2*16
  unsigned char tag[16];
  const char *errmsg;
  int ret;

  session_key = srp_user_get_session_key(sctx->user, &session_key_len);
  if (!session_key)
    {
      handle->errmsg = "Setup request 3: No valid session key";
      return NULL;
    }

  ret = hash_ab(HASH_SHA512, key, (unsigned char *)AES_SETUP_KEY, strlen(AES_SETUP_KEY), session_key, session_key_len);
  if (ret < 0)
    {
      handle->errmsg = "Setup request 3: Hashing of key string and shared secret failed";
      return NULL;
    }

  ret = hash_ab(HASH_SHA512, iv, (unsigned char *)AES_SETUP_IV, strlen(AES_SETUP_IV), session_key, session_key_len);
  if (ret < 0)
    {
      handle->errmsg = "Setup request 3: Hashing of iv string and shared secret failed";
      return NULL;
    }

  iv[15]++; // Nonce?
/*
  if (iv[15] == 0x00 || iv[15] == 0xff)
    printf("- note that value of last byte is %d!\n", iv[15]);
*/
  crypto_sign_keypair(sctx->public_key, sctx->private_key);

  ret = encrypt_gcm(encrypted, sizeof(encrypted), tag, sctx->public_key, sizeof(sctx->public_key), key, iv, &errmsg);
  if (ret < 0)
    {
      handle->errmsg = errmsg;
      return NULL;
    }

  epk = plist_new_data((char *)encrypted, EPK_LENGTH);
  authtag = plist_new_data((char *)tag, AUTHTAG_LENGTH);

  dict = plist_new_dict();
  plist_dict_set_item(dict, "epk", epk);
  plist_dict_set_item(dict, "authTag", authtag);
  plist_to_bin(dict, &data, &uint32);
  plist_free(dict);

  *len = (size_t)uint32;
  return (uint8_t *)data;
}

static int
client_setup_response1(struct pair_setup_context *handle, const uint8_t *data, size_t data_len)
{
  struct pair_client_setup_context *sctx = &handle->sctx.client;
  plist_t dict;
  plist_t pk;
  plist_t salt;

  plist_from_bin((const char *)data, data_len, &dict);

  pk = plist_dict_get_item(dict, "pk");
  salt = plist_dict_get_item(dict, "salt");
  if (!pk || !salt)
    {
      handle->errmsg = "Setup response 1: Missing pk or salt";
      plist_free(dict);
      return -1;
    }

  plist_get_data_val(pk, (char **)&sctx->pkB, &sctx->pkB_len); // B
  plist_get_data_val(salt, (char **)&sctx->salt, &sctx->salt_len);

  plist_free(dict);

  return 0;
}

static int
client_setup_response2(struct pair_setup_context *handle, const uint8_t *data, size_t data_len)
{
  struct pair_client_setup_context *sctx = &handle->sctx.client;
  plist_t dict;
  plist_t proof;

  plist_from_bin((const char *)data, data_len, &dict);

  proof = plist_dict_get_item(dict, "proof");
  if (!proof)
    {
      handle->errmsg = "Setup response 2: Missing proof";
      plist_free(dict);
      return -1;
    }

  plist_get_data_val(proof, (char **)&sctx->M2, &sctx->M2_len); // M2

  plist_free(dict);

  // Check M2
  srp_user_verify_session(sctx->user, (const unsigned char *)sctx->M2);
  if (!srp_user_is_authenticated(sctx->user))
    {
      handle->errmsg = "Setup response 2: Server authentication failed";
      return -1;
    }

  return 0;
}

static int
client_setup_response3(struct pair_setup_context *handle, const uint8_t *data, size_t data_len)
{
  struct pair_client_setup_context *sctx = &handle->sctx.client;
  plist_t dict;
  plist_t epk;
  plist_t authtag;

  plist_from_bin((const char *)data, data_len, &dict);

  epk = plist_dict_get_item(dict, "epk");
  if (!epk)
    {
      handle->errmsg = "Setup response 3: Missing epk";
      plist_free(dict);
      return -1;
    }

  plist_get_data_val(epk, (char **)&sctx->epk, &sctx->epk_len);

  authtag = plist_dict_get_item(dict, "authTag");
  if (!authtag)
    {
      handle->errmsg = "Setup response 3: Missing authTag";
      plist_free(dict);
      return -1;
    }

  plist_get_data_val(authtag, (char **)&sctx->authtag, &sctx->authtag_len);

  plist_free(dict);

  assert(sizeof(handle->result.client_private_key) == sizeof(sctx->private_key));
  assert(sizeof(handle->result.client_public_key) == sizeof(sctx->public_key));

  memcpy(handle->result.client_private_key, sctx->private_key, sizeof(sctx->private_key));
  memcpy(handle->result.client_public_key, sctx->public_key, sizeof(sctx->public_key));

  handle->status = PAIR_STATUS_COMPLETED;
  return 0;
}

static int
client_setup_result(struct pair_setup_context *handle)
{
  struct pair_client_setup_context *sctx = &handle->sctx.client;
  char *ptr;
  unsigned int i;

  // Last 32 bytes of the private key is the public key, so we don't need to
  // explicitly export that
  ptr = handle->result_str;
  for (i = 0; i < sizeof(sctx->private_key); i++)
    ptr += sprintf(ptr, "%02x", sctx->private_key[i]); // 2 x 64 bytes
  *ptr = '\0';

  return 0;
}


static int
client_verify_new(struct pair_verify_context *handle, const char *client_setup_keys, __attribute__((unused)) pair_cb cb, __attribute__((unused)) void *cb_arg, const char *device_id)
{
  struct pair_client_verify_context *vctx = &handle->vctx.client;
  char hex[] = { 0, 0, 0 };
  size_t hexkey_len;
  const char *ptr;
  unsigned int i;

  if (!is_initialized())
    return -1;

  if (!client_setup_keys)
    return -1;

  hexkey_len = strlen(client_setup_keys);

  if (hexkey_len != 2 * sizeof(vctx->client_private_key))
    return -1;

  if (device_id && strlen(device_id) != 16)
    return -1;

  if (device_id)
    memcpy(vctx->device_id, device_id, strlen(device_id));

  ptr = client_setup_keys;
  for (i = 0; i < sizeof(vctx->client_private_key); i++, ptr+=2)
    {
      hex[0] = ptr[0];
      hex[1] = ptr[1];
      vctx->client_private_key[i] = strtol(hex, NULL, 16);
    }

  crypto_sign_ed25519_sk_to_pk(vctx->client_public_key, vctx->client_private_key);

  return 0;
}

static uint8_t *
client_verify_request1(size_t *len, struct pair_verify_context *handle)
{
  struct pair_client_verify_context *vctx = &handle->vctx.client;
  const uint8_t basepoint[32] = {9};
  uint8_t *data;
  int ret;

  ret = crypto_scalarmult(vctx->client_eph_public_key, vctx->client_eph_private_key, basepoint);
  if (ret < 0)
    {
      handle->errmsg = "Verify request 1: Curve 25519 returned an error";
      return NULL;
    }

  *len = 4 + sizeof(vctx->client_eph_public_key) + sizeof(vctx->client_public_key);
  data = calloc(1, *len);
  if (!data)
    {
      handle->errmsg = "Verify request 1: Out of memory";
      return NULL;
    }

  data[0] = 1; // Magic
  memcpy(data + 4, vctx->client_eph_public_key, sizeof(vctx->client_eph_public_key));
  memcpy(data + 4 + sizeof(vctx->client_eph_public_key), vctx->client_public_key, sizeof(vctx->client_public_key));

  return data;
}

static uint8_t *
client_verify_request2(size_t *len, struct pair_verify_context *handle)
{
  struct pair_client_verify_context *vctx = &handle->vctx.client;
  uint8_t shared_secret[crypto_scalarmult_BYTES];
  uint8_t key[SHA512_DIGEST_LENGTH];
  uint8_t iv[SHA512_DIGEST_LENGTH];
  uint8_t encrypted[128]; // Alloc a bit extra, should only really need size of public key len
  uint8_t signature[crypto_sign_BYTES];
  uint8_t *data;
  int ret;
  const char *errmsg;

  *len = sizeof(vctx->client_eph_public_key) + sizeof(vctx->server_eph_public_key);
  data = calloc(1, *len);
  if (!data)
    {
      handle->errmsg = "Verify request 2: Out of memory";
      return NULL;
    }

  memcpy(data, vctx->client_eph_public_key, sizeof(vctx->client_eph_public_key));
  memcpy(data + sizeof(vctx->client_eph_public_key), vctx->server_eph_public_key, sizeof(vctx->server_eph_public_key));

  crypto_sign_detached(signature, NULL, data, *len, vctx->client_private_key);

  free(data);

  ret = crypto_scalarmult(shared_secret, vctx->client_eph_private_key, vctx->server_eph_public_key);
  if (ret < 0)
    {
      handle->errmsg = "Verify request 2: Curve 25519 returned an error";
      return NULL;
    }

  ret = hash_ab(HASH_SHA512, key, (unsigned char *)AES_VERIFY_KEY, strlen(AES_VERIFY_KEY), shared_secret, sizeof(shared_secret));
  if (ret < 0)
    {
      handle->errmsg = "Verify request 2: Hashing of key string and shared secret failed";
      return NULL;
    }

  ret = hash_ab(HASH_SHA512, iv, (unsigned char *)AES_VERIFY_IV, strlen(AES_VERIFY_IV), shared_secret, sizeof(shared_secret));
  if (ret < 0)
    {
      handle->errmsg = "Verify request 2: Hashing of iv string and shared secret failed";
      return NULL;
    }

  ret = encrypt_ctr(encrypted, sizeof(encrypted), vctx->server_fruit_public_key, sizeof(vctx->server_fruit_public_key), signature, sizeof(signature), key, iv, &errmsg);
  if (ret < 0)
    {
      handle->errmsg = errmsg;
      return NULL;
    }

  *len = 4 + sizeof(vctx->server_fruit_public_key);
  data = calloc(1, *len);
  if (!data)
    {
      handle->errmsg = "Verify request 2: Out of memory";
      return NULL;
    }

  memcpy(data + 4, encrypted, sizeof(vctx->server_fruit_public_key));

  return data;
}

static int
client_verify_response1(struct pair_verify_context *handle, const uint8_t *data, size_t data_len)
{
  struct pair_client_verify_context *vctx = &handle->vctx.client;
  size_t wanted;

  wanted = sizeof(vctx->server_eph_public_key) + sizeof(vctx->server_fruit_public_key);
  if (data_len < wanted)
    {
      handle->errmsg = "Verify response 2: Unexpected response (too short)";
      return -1;
    }

  memcpy(vctx->server_eph_public_key, data, sizeof(vctx->server_eph_public_key));
  memcpy(vctx->server_fruit_public_key, data + sizeof(vctx->server_eph_public_key), sizeof(vctx->server_fruit_public_key));

  return 0;
}

static int
client_verify_response2(struct pair_verify_context *handle, __attribute__((unused)) const uint8_t *data, __attribute__((unused)) size_t data_len)
{
  struct pair_client_verify_context *vctx = &handle->vctx.client;
  // TODO actually check response

  memcpy(handle->result.shared_secret, vctx->shared_secret, sizeof(vctx->shared_secret));
  handle->result.shared_secret_len = sizeof(vctx->shared_secret);

  handle->status = PAIR_STATUS_COMPLETED;

  return 0;
}


struct pair_definition pair_client_fruit =
{
  .pair_setup_new = client_setup_new,
  .pair_setup_free = client_setup_free,
  .pair_setup_result = client_setup_result,

  .pair_setup_request1 = client_setup_request1,
  .pair_setup_request2 = client_setup_request2,
  .pair_setup_request3 = client_setup_request3,

  .pair_setup_response1 = client_setup_response1,
  .pair_setup_response2 = client_setup_response2,
  .pair_setup_response3 = client_setup_response3,

  .pair_verify_new = client_verify_new,

  .pair_verify_request1 = client_verify_request1,
  .pair_verify_request2 = client_verify_request2,

  .pair_verify_response1 = client_verify_response1,
  .pair_verify_response2 = client_verify_response2,
};
