/*
 * Utility routines. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * The volume to attenuation function vol2attn copyright (c) Mike Brady 2014
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <memory.h>
#include <poll.h>
#include <popt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include <assert.h>

#ifdef COMPILE_FOR_OSX
#include <CoreServices/CoreServices.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#ifdef HAVE_LIBSSL
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#endif

#ifdef HAVE_LIBPOLARSSL
#include "polarssl/ctr_drbg.h"
#include "polarssl/entropy.h"
#include <polarssl/base64.h>
#include <polarssl/md.h>
#include <polarssl/version.h>
#include <polarssl/x509.h>

#if POLARSSL_VERSION_NUMBER >= 0x01030000
#include "polarssl/compat-1.2.h"
#endif
#endif

#ifdef HAVE_LIBMBEDTLS
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/version.h>
#include <mbedtls/x509.h>

#endif

#include <libdaemon/dlog.h>

// true if Shairport Sync is supposed to be sending output to the output device, false otherwise

static volatile int requested_connection_state_to_output = 1;

shairport_cfg config;

int debuglev = 0;

int get_requested_connection_state_to_output() { return requested_connection_state_to_output; }

void set_requested_connection_state_to_output(int v) { requested_connection_state_to_output = v; }

void die(const char *format, ...) {
  char s[1024];
  s[0] = 0;
  va_list args;
  va_start(args, format);
  vsprintf(s, format, args);
  va_end(args);
  daemon_log(LOG_EMERG, "%s", s);
  shairport_shutdown();
  exit(1);
}

void warn(const char *format, ...) {
  char s[1024];
  s[0] = 0;
  va_list args;
  va_start(args, format);
  vsprintf(s, format, args);
  va_end(args);
  daemon_log(LOG_WARNING, "%s", s);
}

void debug(int level, const char *format, ...) {
  if (level > debuglev)
    return;
  char s[1024];
  s[0] = 0;
  va_list args;
  va_start(args, format);
  vsprintf(s, format, args);
  va_end(args);
  daemon_log(LOG_DEBUG, "%s", s);
}

void inform(const char *format, ...) {
  char s[1024];
  s[0] = 0;
  va_list args;
  va_start(args, format);
  vsprintf(s, format, args);
  va_end(args);
  daemon_log(LOG_INFO, "%s", s);
}

#ifdef HAVE_LIBMBEDTLS
char *base64_enc(uint8_t *input, int length) {
  char *buf = NULL;
  size_t dlen = 0;
  int rc = mbedtls_base64_encode(NULL, 0, &dlen, input, length);
  if (rc && (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL))
    debug(1, "Error %d getting length of base64 encode.", rc);
  else {
    buf = (char *)malloc(dlen);
    rc = mbedtls_base64_encode((unsigned char *)buf, dlen, &dlen, input, length);
    if (rc != 0)
      debug(1, "Error %d encoding base64.", rc);
  }
  return buf;
}

uint8_t *base64_dec(char *input, int *outlen) {
  // slight problem here is that Apple cut the padding off their challenges. We must restore it
  // before passing it in to the decoder, it seems
  uint8_t *buf = NULL;
  size_t dlen = 0;
  int inbufsize = ((strlen(input) + 3) / 4) * 4; // this is the size of the input buffer we will
                                                 // send to the decoder, but we need space for 3
                                                 // extra "="s and a NULL
  char *inbuf = malloc(inbufsize + 4);
  if (inbuf == 0)
    debug(1, "Can't malloc memory  for inbuf in base64_decode.");
  else {
    strcpy(inbuf, input);
    strcat(inbuf, "===");
    // debug(1,"base64_dec called with string \"%s\", length %d, filled string: \"%s\", length %d.",
    //		input,strlen(input),inbuf,inbufsize);
    int rc = mbedtls_base64_decode(NULL, 0, &dlen, (unsigned char *)inbuf, inbufsize);
    if (rc && (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL))
      debug(1, "Error %d getting decode length, result is %d.", rc, dlen);
    else {
      // debug(1,"Decode size is %d.",dlen);
      buf = malloc(dlen);
      if (buf == 0)
        debug(1, "Can't allocate memory in base64_dec.");
      else {
        rc = mbedtls_base64_decode(buf, dlen, &dlen, (unsigned char *)inbuf, inbufsize);
        if (rc != 0)
          debug(1, "Error %d in base64_dec.", rc);
      }
    }
    free(inbuf);
  }
  *outlen = dlen;
  return buf;
}
#endif

#ifdef HAVE_LIBPOLARSSL
char *base64_enc(uint8_t *input, int length) {
  char *buf = NULL;
  size_t dlen = 0;
  int rc = base64_encode(NULL, &dlen, input, length);
  if (rc && (rc != POLARSSL_ERR_BASE64_BUFFER_TOO_SMALL))
    debug(1, "Error %d getting length of base64 encode.", rc);
  else {
    buf = (char *)malloc(dlen);
    rc = base64_encode((unsigned char *)buf, &dlen, input, length);
    if (rc != 0)
      debug(1, "Error %d encoding base64.", rc);
  }
  return buf;
}

uint8_t *base64_dec(char *input, int *outlen) {
  // slight problem here is that Apple cut the padding off their challenges. We must restore it
  // before passing it in to the decoder, it seems
  uint8_t *buf = NULL;
  size_t dlen = 0;
  int inbufsize = ((strlen(input) + 3) / 4) * 4; // this is the size of the input buffer we will
                                                 // send to the decoder, but we need space for 3
                                                 // extra "="s and a NULL
  char *inbuf = malloc(inbufsize + 4);
  if (inbuf == 0)
    debug(1, "Can't malloc memory  for inbuf in base64_decode.");
  else {
    strcpy(inbuf, input);
    strcat(inbuf, "===");
    // debug(1,"base64_dec called with string \"%s\", length %d, filled string: \"%s\", length
    // %d.",input,strlen(input),inbuf,inbufsize);
    int rc = base64_decode(buf, &dlen, (unsigned char *)inbuf, inbufsize);
    if (rc && (rc != POLARSSL_ERR_BASE64_BUFFER_TOO_SMALL))
      debug(1, "Error %d getting decode length, result is %d.", rc, dlen);
    else {
      // debug(1,"Decode size is %d.",dlen);
      buf = malloc(dlen);
      if (buf == 0)
        debug(1, "Can't allocate memory in base64_dec.");
      else {
        rc = base64_decode(buf, &dlen, (unsigned char *)inbuf, inbufsize);
        if (rc != 0)
          debug(1, "Error %d in base64_dec.", rc);
      }
    }
    free(inbuf);
  }
  *outlen = dlen;
  return buf;
}
#endif

#ifdef HAVE_LIBSSL
char *base64_enc(uint8_t *input, int length) {
  BIO *bmem, *b64;
  BUF_MEM *bptr;
  b64 = BIO_new(BIO_f_base64());
  bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, input, length);
  BIO_flush(b64);
  BIO_get_mem_ptr(b64, &bptr);

  char *buf = (char *)malloc(bptr->length);
  if (bptr->length) {
    memcpy(buf, bptr->data, bptr->length - 1);
    buf[bptr->length - 1] = 0;
  }

  BIO_free_all(bmem);

  return buf;
}

uint8_t *base64_dec(char *input, int *outlen) {
  BIO *bmem, *b64;
  int inlen = strlen(input);

  b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);

  // Apple cut the padding off their challenges; restore it
  BIO_write(bmem, input, inlen);
  while (inlen++ & 3)
    BIO_write(bmem, "=", 1);
  BIO_flush(bmem);

  int bufsize = strlen(input) * 3 / 4 + 1;
  uint8_t *buf = malloc(bufsize);
  int nread;

  nread = BIO_read(b64, buf, bufsize);

  BIO_free_all(bmem);

  *outlen = nread;
  return buf;
}
#endif

static char super_secret_key[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n"
    "wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n"
    "wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n"
    "/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n"
    "UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n"
    "BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n"
    "LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n"
    "NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n"
    "lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n"
    "aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n"
    "a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n"
    "oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n"
    "oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n"
    "k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n"
    "AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n"
    "cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
    "54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n"
    "17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n"
    "1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n"
    "LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n"
    "2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
    "-----END RSA PRIVATE KEY-----\0";

#ifdef HAVE_LIBSSL
uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode) {
  static RSA *rsa = NULL;

  if (!rsa) {
    BIO *bmem = BIO_new_mem_buf(super_secret_key, -1);
    rsa = PEM_read_bio_RSAPrivateKey(bmem, NULL, NULL, NULL);
    BIO_free(bmem);
  }

  uint8_t *out = malloc(RSA_size(rsa));
  switch (mode) {
  case RSA_MODE_AUTH:
    *outlen = RSA_private_encrypt(inlen, input, out, rsa, RSA_PKCS1_PADDING);
    break;
  case RSA_MODE_KEY:
    *outlen = RSA_private_decrypt(inlen, input, out, rsa, RSA_PKCS1_OAEP_PADDING);
    break;
  default:
    die("bad rsa mode");
  }
  return out;
}
#endif

#ifdef HAVE_LIBMBEDTLS
uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode) {
  mbedtls_pk_context pkctx;
  mbedtls_rsa_context *trsa;
  const char *pers = "rsa_encrypt";
  size_t olen = *outlen;
  int rc;

  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;

  mbedtls_entropy_init(&entropy);

  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers,
                        strlen(pers));

  mbedtls_pk_init(&pkctx);

  rc = mbedtls_pk_parse_key(&pkctx, (unsigned char *)super_secret_key, sizeof(super_secret_key),
                            NULL, 0);
  if (rc != 0)
    debug(1, "Error %d reading the private key.", rc);

  uint8_t *outbuf = NULL;
  trsa = mbedtls_pk_rsa(pkctx);

  switch (mode) {
  case RSA_MODE_AUTH:
    mbedtls_rsa_set_padding(trsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
    outbuf = malloc(trsa->len);
    rc = mbedtls_rsa_pkcs1_encrypt(trsa, mbedtls_ctr_drbg_random, &ctr_drbg, MBEDTLS_RSA_PRIVATE,
                                   inlen, input, outbuf);
    if (rc != 0)
      debug(1, "mbedtls_pk_encrypt error %d.", rc);
    *outlen = trsa->len;
    break;
  case RSA_MODE_KEY:
    mbedtls_rsa_set_padding(trsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);
    outbuf = malloc(trsa->len);
    rc = mbedtls_rsa_pkcs1_decrypt(trsa, mbedtls_ctr_drbg_random, &ctr_drbg, MBEDTLS_RSA_PRIVATE,
                                   &olen, input, outbuf, trsa->len);
    if (rc != 0)
      debug(1, "mbedtls_pk_decrypt error %d.", rc);
    *outlen = olen;
    break;
  default:
    die("bad rsa mode");
  }

  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  mbedtls_pk_free(&pkctx);
  return outbuf;
}
#endif

#ifdef HAVE_LIBPOLARSSL
uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode) {
  rsa_context trsa;
  const char *pers = "rsa_encrypt";
  int rc;

  entropy_context entropy;
  ctr_drbg_context ctr_drbg;
  entropy_init(&entropy);
  if ((rc = ctr_drbg_init(&ctr_drbg, entropy_func, &entropy, (const unsigned char *)pers,
                          strlen(pers))) != 0)
    debug(1, "ctr_drbg_init returned %d\n", rc);

  rsa_init(&trsa, RSA_PKCS_V21, POLARSSL_MD_SHA1); // padding and hash id get overwritten
  // BTW, this seems to reset a lot of parameters in the rsa_context
  rc = x509parse_key(&trsa, (unsigned char *)super_secret_key, strlen(super_secret_key), NULL, 0);
  if (rc != 0)
    debug(1, "Error %d reading the private key.");

  uint8_t *out = NULL;

  switch (mode) {
  case RSA_MODE_AUTH:
    trsa.padding = RSA_PKCS_V15;
    trsa.hash_id = POLARSSL_MD_NONE;
    debug(2, "rsa_apply encrypt");
    out = malloc(trsa.len);
    rc = rsa_pkcs1_encrypt(&trsa, ctr_drbg_random, &ctr_drbg, RSA_PRIVATE, inlen, input, out);
    if (rc != 0)
      debug(1, "rsa_pkcs1_encrypt error %d.", rc);
    *outlen = trsa.len;
    break;
  case RSA_MODE_KEY:
    debug(2, "rsa_apply decrypt");
    trsa.padding = RSA_PKCS_V21;
    trsa.hash_id = POLARSSL_MD_SHA1;
    out = malloc(trsa.len);
#if POLARSSL_VERSION_NUMBER >= 0x01020900
    rc = rsa_pkcs1_decrypt(&trsa, ctr_drbg_random, &ctr_drbg, RSA_PRIVATE, (size_t *)outlen, input,
                           out, trsa.len);
#else
    rc = rsa_pkcs1_decrypt(&trsa, RSA_PRIVATE, outlen, input, out, trsa.len);
#endif
    if (rc != 0)
      debug(1, "decrypt error %d.", rc);
    break;
  default:
    die("bad rsa mode");
  }
  rsa_free(&trsa);
  debug(2, "rsa_apply exit");
  return out;
}
#endif

void command_set_volume(double volume) {
  if (config.cmd_set_volume) {
    /*Spawn a child to run the program.*/
    pid_t pid = fork();
    if (pid == 0) { /* child process */
    	size_t command_buffer_size = strlen(config.cmd_set_volume)+32;
      char* command_buffer = (char*)malloc(command_buffer_size);
            if (command_buffer==NULL) {
      	inform("Couldn't allocate memory for set_volume argument string");
      } else {
      	memset(command_buffer,0,command_buffer_size);
				sprintf(command_buffer, "%s%f", config.cmd_set_volume,volume);
				// debug(1,"command_buffer is \"%s\".",command_buffer);
				int argC;
				char **argV;
				// debug(1,"set_volume command found.");
				if (poptParseArgvString(command_buffer, &argC, (const char ***)&argV) !=
					0) {
					// note that argV should be free()'d after use, but we expect this fork to exit
					// eventually.
					warn("Can't decipher on-set-volume command arguments \"%s\".",command_buffer);
					free(argV);
					free(command_buffer);
				} else {
					free(command_buffer);
					// debug(1,"Executing on-set-volume command %s with %d arguments.",argV[0],argC);
					execv(argV[0], argV);
					warn("Execution of on-set-volume command \"%s\" failed to start", config.cmd_set_volume);
					// debug(1, "Error executing on-set-volume command %s", config.cmd_set_volume);
					exit(127); /* only if execv fails */
				}
      }
    
    } else {
      if (config.cmd_blocking) { /* pid!=0 means parent process and if blocking is true, wait for
                                    process to finish */
        pid_t rc = waitpid(pid, 0, 0); /* wait for child to exit */
        if (rc != pid) {
          warn("Execution of on-set-volume command returned an error.");
          debug(1, "on-set-volume command %s finished with error %d", config.cmd_set_volume,errno);
        }
      }
      // debug(1,"Continue after on-set-volume command");
    }
  }
}

void command_start(void) {
  if (config.cmd_start) {
    pid_t pid;
    int pipes[2];

    if (config.cmd_start_returns_output && pipe(pipes) != 0) {
      warn("Unable to allocate pipe for popen of start command.");
      debug(1, "pipe finished with error %d", errno);
      return;
    }
    /*Spawn a child to run the program.*/
    pid = fork();
    if (pid == 0) { /* child process */
      int argC;
      char **argV;

      if (config.cmd_start_returns_output) {
        close(pipes[0]);
        if (dup2(pipes[1], 1) < 0) {
          warn("Unable to reopen pipe as stdout for popen of start command");
          debug(1, "dup2 finished with error %d", errno);
          close(pipes[1]);
          return;
        }
      }

      // debug(1,"on-start command found.");
      if (poptParseArgvString(config.cmd_start, &argC, (const char ***)&argV) !=
          0) // note that argV should be free()'d after use, but we expect this fork to exit
             // eventually.
        debug(1, "Can't decipher on-start command arguments");
      else {
        // debug(1,"Executing on-start command %s with %d arguments.",argV[0],argC);
        execv(argV[0], argV);
        warn("Execution of on-start command failed to start");
        debug(1, "Error executing on-start command %s", config.cmd_start);
        exit(127); /* only if execv fails */
      }
    } else {
      if (config.cmd_blocking || config.cmd_start_returns_output) { /* pid!=0 means parent process and if blocking is true, wait for
                                    process to finish */
        pid_t rc = waitpid(pid, 0, 0); /* wait for child to exit */
        if (rc != pid) {
          warn("Execution of on-start command returned an error.");
          debug(1, "on-start command %s finished with error %d", config.cmd_start, errno);
        }
        if (config.cmd_start_returns_output) {
          char buffer[256];
          int len;
          close(pipes[1]);
          len = read(pipes[0], buffer, 255);
          close(pipes[0]);
          buffer[len] = '\0';
          if (buffer[len - 1] == '\n')
            buffer[len - 1] = '\0'; // strip trailing newlines
          debug(1, "received '%s' as the device to use from the on-start command", buffer);
          if (config.output->change_output_device)
            config.output->change_output_device(buffer);
        }
      }
      // debug(1,"Continue after on-start command");
    }
  }
}

void command_stop(void) {
  if (config.cmd_stop) {
    /*Spawn a child to run the program.*/
    pid_t pid = fork();
    if (pid == 0) { /* child process */
      int argC;
      char **argV;
      // debug(1,"on-stop command found.");
      if (poptParseArgvString(config.cmd_stop, &argC, (const char ***)&argV) !=
          0) // note that argV should be free()'d after use, but we expect this fork to exit
             // eventually.
        debug(1, "Can't decipher on-stop command arguments");
      else {
        // debug(1,"Executing on-stop command %s",config.cmd_stop);
        execv(argV[0], argV);
        warn("Execution of on-stop command failed to start");
        debug(1, "Error executing on-stop command %s", config.cmd_stop);
        exit(127); /* only if execv fails */
      }
    } else {
      if (config.cmd_blocking) { /* pid!=0 means parent process and if blocking is true, wait for
                                    process to finish */
        pid_t rc = waitpid(pid, 0, 0); /* wait for child to exit */
        if (rc != pid) {
          warn("Execution of on-stop command returned an error.");
          debug(1, "Stop command %s finished with error %d", config.cmd_stop, errno);
        }
      }
      // debug(1,"Continue after on-stop command");
    }
  }
}

// this is for reading an unsigned 32 bit number, such as an RTP timestamp

uint32_t uatoi(const char *nptr) {
  uint64_t llint = atoll(nptr);
  uint32_t r = llint;
  return r;
}

// Given a volume (0 to -30) and high and low attenuations available in the mixer in dB, return an
// attenuation depending on the volume and the function's transfer function
// See http://tangentsoft.net/audio/atten.html for data on good attenuators.
// We want a smooth attenuation function, like, for example, the ALPS RK27 Potentiometer transfer
// functions referred to at the link above.

// Note that the max_db and min_db are given as dB*100

double vol2attn(double vol, long max_db, long min_db) {

// We use a little coordinate geometry to build a transfer function from the volume passed in to the
// device's dynamic range.
// (See the diagram in the documents folder.)
// The x axis is the "volume in" which will be from -30 to 0. The y axis will be the "volume out"
// which will be from the bottom of the range to the top.
// We build the transfer function from one or more lines. We characterise each line with two
// numbers:
// the first is where on x the line starts when y=0 (x can be from 0 to -30); the second is where on
// y the line stops when when x is -30.
// thus, if the line was characterised as {0,-30}, it would be an identity transfer.
// Assuming, for example, a dynamic range of lv=-60 to hv=0
// Typically we'll use three lines -- a three order transfer function
// First: {0,30} giving a gentle slope -- the 30 comes from half the dynamic range
// Second: {-5,-30-(lv+30)/2} giving a faster slope from y=0 at x=-12 to y=-42.5 at x=-30
// Third: {-17,lv} giving a fast slope from y=0 at x=-19 to y=-60 at x=-30

#define order 3

  double vol_setting = 0;

  if ((vol <= 0.0) && (vol >= -30.0)) {
    long range_db = max_db - min_db; // this will be a positive number
    // debug(1,"Volume min %ddB, max %ddB, range %ddB.",min_db,max_db,range_db);
    // double first_slope = -3000.0; // this is the slope of the attenuation at the high end -- 30dB
    // for the full rotation.
    double first_slope =
        -range_db /
        2; // this is the slope of the attenuation at the high end -- 30dB for the full rotation.
    if (-range_db > first_slope)
      first_slope = range_db;
    double lines[order][2] = {
        {0, first_slope}, {-5, first_slope - (range_db + first_slope) / 2}, {-17, -range_db}};
    int i;
    for (i = 0; i < order; i++) {
      if (vol <= lines[i][0]) {
        double tvol = lines[i][1] * (vol - lines[i][0]) / (-30 - lines[i][0]);
        // debug(1,"On line %d, end point of %f, input vol %f yields output vol
        // %f.",i,lines[i][1],vol,tvol);
        if (tvol < vol_setting)
          vol_setting = tvol;
      }
    }
    vol_setting += max_db;
  } else if (vol != -144.0) {
    debug(1, "Volume request value %f is out of range: should be from 0.0 to -30.0 or -144.0.",
          vol);
    vol_setting = min_db; // for safety, return the lowest setting...
  } else {
    vol_setting = min_db; // for safety, return the lowest setting...
  }
  // debug(1,"returning an attenuation of %f.",vol_setting);
  return vol_setting;
}

uint64_t get_absolute_time_in_fp() {
  uint64_t time_now_fp;
#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN
  struct timespec tn;
  // can't use CLOCK_MONOTONIC_RAW as it's not implemented in OpenWrt
  clock_gettime(CLOCK_MONOTONIC, &tn);
  time_now_fp =
      ((uint64_t)tn.tv_sec << 32) + ((uint64_t)tn.tv_nsec << 32) / 1000000000; // types okay
#endif
#ifdef COMPILE_FOR_OSX
  uint64_t time_now_mach;
  uint64_t elapsedNano;
  static mach_timebase_info_data_t sTimebaseInfo = {0, 0};

  time_now_mach = mach_absolute_time();

  // If this is the first time we've run, get the timebase.
  // We can use denom == 0 to indicate that sTimebaseInfo is
  // uninitialised because it makes no sense to have a zero
  // denominator in a fraction.

  if (sTimebaseInfo.denom == 0) {
    debug(1, "Mac initialise timebase info.");
    (void)mach_timebase_info(&sTimebaseInfo);
  }

  // Do the maths. We hope that the multiplication doesn't
  // overflow; the price you pay for working in fixed point.

  // this gives us nanoseconds
  uint64_t time_now_ns = time_now_mach * sTimebaseInfo.numer / sTimebaseInfo.denom;

  // take the units and shift them to the upper half of the fp, and take the nanoseconds, shift them
  // to the upper half and then divide the result to 1000000000
  time_now_fp =
      ((time_now_ns / 1000000000) << 32) + (((time_now_ns % 1000000000) << 32) / 1000000000);

#endif
  return time_now_fp;
}

ssize_t non_blocking_write(int fd, const void *buf, size_t count) {
  void *ibuf = (void *)buf;
  size_t bytes_remaining = count;
  int rc = 0;
  struct pollfd ufds[1];
  while ((bytes_remaining > 0) && (rc == 0)) {
    // check that we can do some writing
    ufds[0].fd = fd;
    ufds[0].events = POLLOUT;
    rc = poll(ufds, 1, 5000);
    if (rc < 0) {
      // debug(1, "non-blocking write error waiting for pipe to become ready for writing...");
    } else if (rc == 0) {
      // warn("non-blocking write timeout waiting for pipe to become ready for writing");
      rc = -1;
      errno = -ETIMEDOUT;
    } else { // rc > 0, implying it might be ready
      ssize_t bytes_written = write(fd, ibuf, bytes_remaining);
      if (bytes_written == -1) {
        // debug(1,"Error %d in non_blocking_write: \"%s\".",errno,strerror(errno));
        rc = -1;
      } else {
        ibuf += bytes_written;
        bytes_remaining -= bytes_written;
      }
    }
  }
  if (rc == 0)
    return count - bytes_remaining; // this is just to mimic a normal write/3.
  else
    return rc;
  //  return write(fd,buf,count);
}

/* from
 * http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring#comment-722
 */

char *str_replace(const char *string, const char *substr, const char *replacement) {
  char *tok = NULL;
  char *newstr = NULL;
  char *oldstr = NULL;
  char *head = NULL;

  /* if either substr or replacement is NULL, duplicate string a let caller handle it */
  if (substr == NULL || replacement == NULL)
    return strdup(string);
  newstr = strdup(string);
  head = newstr;
  while ((tok = strstr(head, substr))) {
    oldstr = newstr;
    newstr = malloc(strlen(oldstr) - strlen(substr) + strlen(replacement) + 1);
    /*failed to alloc mem, free old string and return NULL */
    if (newstr == NULL) {
      free(oldstr);
      return NULL;
    }
    memcpy(newstr, oldstr, tok - oldstr);
    memcpy(newstr + (tok - oldstr), replacement, strlen(replacement));
    memcpy(newstr + (tok - oldstr) + strlen(replacement), tok + strlen(substr),
           strlen(oldstr) - strlen(substr) - (tok - oldstr));
    memset(newstr + strlen(oldstr) - strlen(substr) + strlen(replacement), 0, 1);
    /* move back head right after the last replacement */
    head = newstr + (tok - oldstr) + strlen(replacement);
    free(oldstr);
  }
  return newstr;
}

/* from http://burtleburtle.net/bob/rand/smallprng.html */

// typedef uint64_t u8;
typedef struct ranctx {
  uint64_t a;
  uint64_t b;
  uint64_t c;
  uint64_t d;
} ranctx;

static struct ranctx rx;

#define rot(x, k) (((x) << (k)) | ((x) >> (64 - (k))))
uint64_t ranval(ranctx *x) {
  uint64_t e = x->a - rot(x->b, 7);
  x->a = x->b ^ rot(x->c, 13);
  x->b = x->c + rot(x->d, 37);
  x->c = x->d + e;
  x->d = e + x->a;
  return x->d;
}

void raninit(ranctx *x, uint64_t seed) {
  uint64_t i;
  x->a = 0xf1ea5eed, x->b = x->c = x->d = seed;
  for (i = 0; i < 20; ++i) {
    (void)ranval(x);
  }
}

void r64init(uint64_t seed) { raninit(&rx, seed); }

uint64_t r64u() { return (ranval(&rx)); }

int64_t r64i() { return (ranval(&rx) >> 1); }

/* generate an array of 64-bit random numbers */
const int ranarraylength = 1009; // these will be 8-byte numbers.

uint64_t *ranarray;

int ranarraynext;

void ranarrayinit() {
  ranarray = (uint64_t *)malloc(ranarraylength * sizeof(uint64_t));
  int i;
  for (i = 0; i < ranarraylength; i++)
    ranarray[i] = r64u();
  ranarraynext = 0;
}

uint64_t ranarrayval() {
  uint64_t v = ranarray[ranarraynext];
  ranarraynext++;
  ranarraynext = ranarraynext % ranarraylength;
  return v;
}

void r64arrayinit() { ranarrayinit(); }

uint64_t ranarray64u() { return (ranarrayval()); }

int64_t ranarray64i() { return (ranarrayval() >> 1); }
