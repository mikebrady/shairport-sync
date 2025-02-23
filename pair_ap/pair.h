#ifndef __PAIR_AP_H__
#define __PAIR_AP_H__

#include <stdint.h>

#define PAIR_AP_VERSION_MAJOR 0
#define PAIR_AP_VERSION_MINOR 14

#define PAIR_AP_DEVICE_ID_LEN_MAX 64

#define PAIR_AP_POST_PIN_START "POST /pair-pin-start"
#define PAIR_AP_POST_SETUP "POST /pair-setup"
#define PAIR_AP_POST_VERIFY "POST /pair-verify"
#define PAIR_AP_POST_ADD "POST /pair-add"
#define PAIR_AP_POST_LIST "POST /pair-list"
#define PAIR_AP_POST_REMOVE "POST /pair-remove"

enum pair_type
{
  // This is the pairing type required for Apple TV device verification, which
  // became mandatory with tvOS 10.2.
  PAIR_CLIENT_FRUIT,
  // This is the Homekit type required for AirPlay 2 with both PIN setup and
  // verification
  PAIR_CLIENT_HOMEKIT_NORMAL,
  // Same as normal except PIN is fixed to 3939 and stops after setup step 2,
  // when session key is established
  PAIR_CLIENT_HOMEKIT_TRANSIENT,
  // Server side implementation supporting both transient and normal mode,
  // letting client choose mode. If a PIN is with pair_setup_new() then only
  // normal mode will be possible.
  PAIR_SERVER_HOMEKIT,
};

/* This struct stores the various forms of pairing results. The shared secret
 * is used to initialise an encrypted session via pair_cipher_new(). For
 * non-transient client pair setup, you also get a key string (client_setup_keys) from
 * pair_setup_result() that you can store and use to later initialise
 * pair_verify_new(). For non-transient server pair setup, you can either:
 *  - Register an "add pairing" callback (add_cb) with pair_setup_new(), and
 *    then save the client id and key in the callback (see server-example.c for
 *    this approach).
 *  - Check pairing result with pair_setup_result() and if successful read and
 *    store the client id and key from the result struct.
 *  - Decide not to authenticate clients during pair-verify (set get_cb to NULL)
 *    in which case you don't need to save client ids and keys from pair-setup.
 *
 * Table showing returned data (everything else will be zeroed):
 *
 *                                  | pair-setup                    | pair-verify
 *  --------------------------------|-------------------------------|--------------
 *  PAIR_CLIENT_FRUIT               | client keys                   | shared secret
 *  PAIR_CLIENT_HOMEKIT_NORMAL      | client keys, server public    | shared secret
                                    | key, server id                | shared secret
 *  PAIR_CLIENT_HOMEKIT_TRANSIENT   | shared secret                 | n/a
 *  PAIR_SERVER_HOMEKIT (normal)    | client public key, client id  | shared secret
 *  PAIR_SERVER_HOMEKIT (transient) | shared secret                 | n/a
 */
struct pair_result
{
  char device_id[PAIR_AP_DEVICE_ID_LEN_MAX]; // ID of the peer
  uint8_t client_private_key[64];
  uint8_t client_public_key[32];
  uint8_t server_public_key[32];
  uint8_t shared_secret[64];
  size_t shared_secret_len; // Will be 32 (normal) or 64 (transient)
};

struct pair_setup_context;
struct pair_verify_context;
struct pair_cipher_context;

typedef int (*pair_cb)(uint8_t public_key[32], const char *device_id, void *cb_arg);
typedef void (*pair_list_cb)(pair_cb list_cb, void *list_cb_arg, void *cb_arg);


/* ------------------------------- pair setup ------------------------------- */

/* Client
 * When you have the pin-code (must be 4 chars), create a new context with this
 * function and then call pair_setup() or pair_setup_request1(). device_id is
 * only required for Homekit pairing. If the client previously paired
 * (non-transient) and has saved credentials, it should instead skip setup and
 * only do verification. The callback is only for Homekit, and you can leave it
 * at NULL if you don't care about saving ID and key of the server for later
 * verification (then you also set get_cb to NULL in pair_verify_new), or if you
 * will read the id and key via pair_setup_result.
 *
 * Server
 * The client will make a connection and then at some point make a /pair-setup
 * or a /pair-verify. The server should:
 *   - new /pair-setup: create a setup context with a pin-code (or NULL to allow
 *     transient pairing), and then call pair_setup() to process request and
 *     construct reply (also for subsequent /pair-setup requests)
 *   - new /pair_verify: create a verify context and then call pair_verify()
 *     to process request and construct reply (also for subsequent /pair-verify
 *     requests)
 */
struct pair_setup_context *
pair_setup_new(enum pair_type type, const char *pin, pair_cb add_cb, void *cb_arg, const char *device_id);
void
pair_setup_free(struct pair_setup_context *sctx);

/* Returns last error message
 */
const char *
pair_setup_errmsg(struct pair_setup_context *sctx);

/* Will create a request (if client) or response (if server) based on the setup
 * context and last message from the peer. If this is the first client request
 * then set *in to NULL. Returns negative on error.
 */
int
pair_setup(uint8_t **out, size_t *out_len, struct pair_setup_context *sctx, const uint8_t *in, size_t in_len);

/* Returns the result of a pairing, or negative if pairing is not completed. See
 * 'struct pair_result' for info about pairing results. The string is a
 * representation of the result that is easy to persist and can be used to feed
 * back into pair_verify_new. The result and string becomes invalid when you
 * free sctx.
 */
int
pair_setup_result(const char **client_setup_keys, struct pair_result **result, struct pair_setup_context *sctx);

/* These are for constructing specific message types and reading specific
 * message types. Not needed for Homekit pairing if you use pair_setup().
 */
uint8_t *
pair_setup_request1(size_t *len, struct pair_setup_context *sctx);
uint8_t *
pair_setup_request2(size_t *len, struct pair_setup_context *sctx);
uint8_t *
pair_setup_request3(size_t *len, struct pair_setup_context *sctx);

int
pair_setup_response1(struct pair_setup_context *sctx, const uint8_t *in, size_t in_len);
int
pair_setup_response2(struct pair_setup_context *sctx, const uint8_t *in, size_t in_len);
int
pair_setup_response3(struct pair_setup_context *sctx, const uint8_t *in, size_t in_len);


/* ------------------------------ pair verify ------------------------------- */

/* Client
 * When you have completed pair setup you get a string containing some keys
 * from pair_setup_result(). Give the string as input to this function to create
 * a verification context. Set the callback to NULL. Then call pair_verify().
 * The device_id is required for Homekit pairing.
 *
 * Server
 * When you get a pair verify request from a new peer, create a new context with
 * client_setup_keys set to NULL, with a callback set and the server's device ID
 * (same as for setup). Then call pair_verify(). The callback is used to get
 * the persisted client public key (saved after pair setup), so the client can
 * be verified. You can set the callback to NULL if you don't care about that.
 * If set, the callback is made as part of pair_verify_response2. The job of the
 * callback is to fill out the public_key with the public key from the setup
 * stage (see 'struct pair_result'). If the client device id is not known (i.e.
 * it has not completed pair-setup), return -1.
 */
struct pair_verify_context *
pair_verify_new(enum pair_type type, const char *client_setup_keys, pair_cb get_cb, void *cb_arg, const char *device_id);
void
pair_verify_free(struct pair_verify_context *vctx);

/* Returns last error message
 */
const char *
pair_verify_errmsg(struct pair_verify_context *vctx);

/* Will create a request (if client) or response (if server) based on the verify
 * context and last message from the peer. If this is the first client request
 * then set *in to NULL. Returns negative on error.
 */
int
pair_verify(uint8_t **out, size_t *out_len, struct pair_verify_context *sctx, const uint8_t *in, size_t in_len);

/* Returns a pointer to the result of the pairing. Only the shared secret will
 * be filled out. Note that the result become invalid when you free vctx.
 */
int
pair_verify_result(struct pair_result **result, struct pair_verify_context *vctx);

/* These are for constructing specific message types and reading specific
 * message types. Not needed for Homekit pairing where you can use pair_verify().
 */
uint8_t *
pair_verify_request1(size_t *len, struct pair_verify_context *vctx);
uint8_t *
pair_verify_request2(size_t *len, struct pair_verify_context *vctx);

int
pair_verify_response1(struct pair_verify_context *vctx, const uint8_t *in, size_t in_len);
int
pair_verify_response2(struct pair_verify_context *vctx, const uint8_t *in, size_t in_len);


/* ------------------------------- ciphering -------------------------------- */

/* When you have completed the verification you can extract a shared secret with
 * pair_verify_result() - or, in case of transient pairing, from
 * pair_setup_result(). Give the shared secret as input to this function to
 * create a ciphering context.
 */
struct pair_cipher_context *
pair_cipher_new(enum pair_type type, int channel, const uint8_t *shared_secret, size_t shared_secret_len, const char *dynamic_salt_suffix);
void
pair_cipher_free(struct pair_cipher_context *cctx);

/* Returns last error message
 */
const char *
pair_cipher_errmsg(struct pair_cipher_context *cctx);

/* The return value equals length of plaintext that was encrypted, so if the
 * return value == plaintext_len then everything was encrypted. On error -1 is
 * returned.
 */
ssize_t
pair_encrypt(uint8_t **ciphertext, size_t *ciphertext_len, const uint8_t *plaintext, size_t plaintext_len, struct pair_cipher_context *cctx);

/* The return value equals length of ciphertext that was decrypted, so if the
 * return value == ciphertext_len then everything was decrypted. On error -1 is
 * returned.
 */
ssize_t
pair_decrypt(uint8_t **plaintext, size_t *plaintext_len, const uint8_t *ciphertext, size_t ciphertext_len, struct pair_cipher_context *cctx);

/* Rolls back the nonce
 */
void
pair_encrypt_rollback(struct pair_cipher_context *cctx);
void
pair_decrypt_rollback(struct pair_cipher_context *cctx);


/* --------------------------------- other ---------------------------------- */

/* These are for Homekit pairing where they are called by the controller, e.g.
 * the Home app
 *
 * TODO this part is currenly not working
 */
int
pair_add(enum pair_type type, uint8_t **out, size_t *out_len, pair_cb add_cb, void *cb_arg, const uint8_t *in, size_t in_len);

int
pair_remove(enum pair_type type, uint8_t **out, size_t *out_len, pair_cb remove_cb, void *cb_arg, const uint8_t *in, size_t in_len);

int
pair_list(enum pair_type type, uint8_t **out, size_t *out_len, pair_list_cb list_cb, void *cb_arg, const uint8_t *in, size_t in_len);

/* For parsing an incoming message to see what type ("state") it is. Mostly
 * useful for servers. Returns 1-6 for pair-setup and 1-4 for pair-verify.
 */
int
pair_state_get(enum pair_type type, const char **errmsg, const uint8_t *in, size_t in_len);

/* For servers, pair_ap calculates the public key using device_id as a seed.
 * This function returns that public key.
 */
void
pair_public_key_get(enum pair_type type, uint8_t server_public_key[32], const char *device_id);

#endif  /* !__PAIR_AP_H__ */
