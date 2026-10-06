#ifndef __PAIR_AP_TLV_H__
#define __PAIR_AP_TLV_H__

#include <stdint.h>

#define PAIR_TLV_ERROR_NONE 0
#define PAIR_TLV_ERROR_MEMORY -1
#define PAIR_TLV_ERROR_INSUFFICIENT_SIZE -2

typedef enum {
  TLVType_Method = 0,        // (integer) Method to use for pairing. See PairMethod
  TLVType_Identifier = 1,    // (UTF-8) Identifier for authentication
  TLVType_Salt = 2,          // (bytes) 16+ bytes of random salt
  TLVType_PublicKey = 3,     // (bytes) Curve25519, SRP public key or signed Ed25519 key
  TLVType_Proof = 4,         // (bytes) Ed25519 or SRP proof
  TLVType_EncryptedData = 5, // (bytes) Encrypted data with auth tag at end
  TLVType_State = 6,         // (integer) State of the pairing process. 1=M1, 2=M2, etc.
  TLVType_Error = 7,         // (integer) Error code. Must only be present if error code is
                             // not 0. See TLVError
  TLVType_RetryDelay = 8,    // (integer) Seconds to delay until retrying a setup code
  TLVType_Certificate = 9,   // (bytes) X.509 Certificate
  TLVType_Signature = 10,    // (bytes) Ed25519
  TLVType_Permissions = 11,  // (integer) Bit value describing permissions of the controller
                             // being added.
                             // None (0x00): Regular user
                             // Bit 1 (0x01): Admin that is able to add and remove
                             // pairings against the accessory
  TLVType_FragmentData = 13, // (bytes) Non-last fragment of data. If length is 0,
                             // it's an ACK.
  TLVType_FragmentLast = 14, // (bytes) Last fragment of data
  TLVType_Flags = 19,        // Added from airplay2_receiver
  TLVType_Separator = 0xff,
} TLVType;


typedef enum {
  TLVError_Unknown = 1,         // Generic error to handle unexpected errors
  TLVError_Authentication = 2,  // Setup code or signature verification failed
  TLVError_Backoff = 3,         // Client must look at the retry delay TLV item and
                                // wait that many seconds before retrying
  TLVError_MaxPeers = 4,        // Server cannot accept any more pairings
  TLVError_MaxTries = 5,        // Server reached its maximum number of
                                // authentication attempts
  TLVError_Unavailable = 6,     // Server pairing method is unavailable
  TLVError_Busy = 7,            // Server is busy and cannot accept a pairing
                                // request at this time
} TLVError;

typedef struct _tlv {
    struct _tlv *next;
    uint8_t type;
    uint8_t *value;
    size_t size;
} pair_tlv_t;


typedef struct {
    pair_tlv_t *head;
} pair_tlv_values_t;


pair_tlv_values_t *pair_tlv_new();

void pair_tlv_free(pair_tlv_values_t *values);

int pair_tlv_add_value(pair_tlv_values_t *values, uint8_t type, const uint8_t *value, size_t size);

pair_tlv_t *pair_tlv_get_value(const pair_tlv_values_t *values, uint8_t type);

int pair_tlv_format(const pair_tlv_values_t *values, uint8_t *buffer, size_t *size);

int pair_tlv_parse(const uint8_t *buffer, size_t length, pair_tlv_values_t *values);

#ifdef DEBUG_PAIR
void pair_tlv_debug(const pair_tlv_values_t *values);
#endif

#endif  /* !__PAIR_AP_TLV_H__ */
