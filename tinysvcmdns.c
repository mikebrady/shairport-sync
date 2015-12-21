// This file is the concatenation of mdnsd.c and mdns.c
// from tinysvcmdns with minor modifications
// The code was taken from https://bitbucket.org/geekman/tinysvcmdns at revision e34b562

/*
 * tinysvcmdns - a tiny MDNS implementation for publishing services
 * Copyright (C) 2011 Darell Tan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "tinysvcmdns.h"
#include "common.h"

#define DEBUG_PRINTF(...) debug(3, __VA_ARGS__)
#define log_message(level, ...)                                                                    \
  do {                                                                                             \
    switch (level) {                                                                               \
    case LOG_ERR:                                                                                  \
      warn(__VA_ARGS__);                                                                           \
      break;                                                                                       \
    default:                                                                                       \
      debug(3, __VA_ARGS__);                                                                       \
    }                                                                                              \
  } while (0)

//******************************************************//
//                      mdns.c                          //
//******************************************************//
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <winsock.h>
#include <in6addr.h>
#else
#include <netinet/in.h>
#endif

// See RFC 6762 Section 10 for an account of two TTLs -- 120 seconds for rrs with a host name as the
// record's name
// or a host name in the record's rdata
// 75 minutes for everything else.
// https://tools.ietf.org/html/rfc6762

#define DEFAULT_TTL_FOR_RECORD_WITH_HOSTNAME 120
#define DEFAULT_TTL 4500

struct name_comp {
  uint8_t *label; // label
  size_t pos;     // position in msg

  struct name_comp *next;
};

// ----- label functions -----

// duplicates a name
inline uint8_t *dup_nlabel(const uint8_t *n) {
  assert(n[0] <= 63); // prevent mis-use
  return (uint8_t *)strdup((char *)n);
}

// duplicates a label
uint8_t *dup_label(const uint8_t *label) {
  int len = *label + 1;
  if (len > 63)
    return NULL;
  uint8_t *newlabel = malloc(len + 1);
  strncpy((char *)newlabel, (char *)label, len);
  newlabel[len] = '\0';
  return newlabel;
}

uint8_t *join_nlabel(const uint8_t *n1, const uint8_t *n2) {
  int len1, len2;
  uint8_t *s;

  assert(n1[0] <= 63 && n2[0] <= 63); // detect misuse

  len1 = strlen((char *)n1);
  len2 = strlen((char *)n2);

  s = malloc(len1 + len2 + 1);
  strncpy((char *)s, (char *)n1, len1);
  strncpy((char *)s + len1, (char *)n2, len2);
  s[len1 + len2] = '\0';
  return s;
}

// returns a human-readable name label in dotted form
char *nlabel_to_str(const uint8_t *name) {
  char *label, *labelp;
  const uint8_t *p;

  assert(name != NULL);

  label = labelp = malloc(256);

  for (p = name; *p; p++) {
    strncpy(labelp, (char *)p + 1, *p);
    labelp += *p;
    *labelp = '.';
    labelp++;

    p += *p;
  }

  *labelp = '\0';

  return label;
}

// returns the length of a label field
// does NOT uncompress the field, so it could be as small as 2 bytes
// or 1 for the root
static size_t label_len(uint8_t *pkt_buf, size_t pkt_len, size_t off) {
  uint8_t *p;
  uint8_t *e = pkt_buf + pkt_len;
  size_t len = 0;

  for (p = pkt_buf + off; p < e; p++) {
    if (*p == 0) {
      return len + 1;
    } else if ((*p & 0xC0) == 0xC0) {
      return len + 2;
    } else {
      len += *p + 1;
      p += *p;
    }
  }

  return len;
}

// creates a label
// free() after use
uint8_t *create_label(const char *txt) {
  int len;
  uint8_t *s;

  assert(txt != NULL);
  len = strlen(txt);
  if (len > 63)
    return NULL;

  s = malloc(len + 2);
  s[0] = len;
  strncpy((char *)s + 1, txt, len);
  s[len + 1] = '\0';

  return s;
}

// creates a uncompressed name label given a DNS name like "apple.b.com"
// free() after use
uint8_t *create_nlabel(const char *name) {
  char *label;
  char *p, *e, *lenpos;
  int len = 0;

  assert(name != NULL);

  len = strlen(name);
  label = malloc(len + 1 + 1);
  if (label == NULL)
    return NULL;

  strncpy((char *)label + 1, name, len);
  label[len + 1] = '\0';

  p = label;
  e = p + len;
  lenpos = p;

  while (p < e) {
    *lenpos = 0;
    char *dot = memchr(p + 1, '.', e - p - 1);
    if (dot == NULL)
      dot = e + 1;
    *lenpos = dot - p - 1;

    p = dot;
    lenpos = dot;
  }

  return (uint8_t *)label;
}

// copies a label from the buffer into a newly-allocated string
// free() after use
static uint8_t *copy_label(uint8_t *pkt_buf, size_t pkt_len, size_t off) {
  int len;

  if (off > pkt_len)
    return NULL;

  len = pkt_buf[off] + 1;
  if (off + len > pkt_len) {
    DEBUG_PRINTF("label length exceeds packet buffer\n");
    return NULL;
  }

  return dup_label(pkt_buf + off);
}

// uncompresses a name
// free() after use
static uint8_t *uncompress_nlabel(uint8_t *pkt_buf, size_t pkt_len, size_t off) {
  uint8_t *p;
  uint8_t *e = pkt_buf + pkt_len;
  size_t len = 0;
  char *str, *sp;
  if (off >= pkt_len)
    return NULL;

  // calculate length of uncompressed label
  for (p = pkt_buf + off; *p && p < e; p++) {
    size_t llen = 0;
    if ((*p & 0xC0) == 0xC0) {
      uint8_t *p2 = pkt_buf + (((p[0] & ~0xC0) << 8) | p[1]);
      llen = *p2 + 1;
      p = p2 + llen - 1;
    } else {
      llen = *p + 1;
      p += llen - 1;
    }
    len += llen;
  }

  str = sp = malloc(len + 1);
  if (str == NULL)
    return NULL;

  // FIXME: must merge this with above code
  for (p = pkt_buf + off; *p && p < e; p++) {
    size_t llen = 0;
    if ((*p & 0xC0) == 0xC0) {
      uint8_t *p2 = pkt_buf + (((p[0] & ~0xC0) << 8) | p[1]);
      llen = *p2 + 1;
      strncpy(sp, (char *)p2, llen);
      p = p2 + llen - 1;
    } else {
      llen = *p + 1;
      strncpy(sp, (char *)p, llen);
      p += llen - 1;
    }
    sp += llen;
  }
  *sp = '\0';

  return (uint8_t *)str;
}

// ----- RR list & group functions -----

const char *rr_get_type_name(enum rr_type type) {
  switch (type) {
  case RR_A:
    return "A";
  case RR_PTR:
    return "PTR";
  case RR_TXT:
    return "TXT";
  case RR_AAAA:
    return "AAAA";
  case RR_SRV:
    return "SRV";
  case RR_NSEC:
    return "NSEC";
  case RR_ANY:
    return "ANY";
  }
  return NULL;
}

void rr_entry_destroy(struct rr_entry *rr) {
  struct rr_data_txt *txt_rec;
  assert(rr);

  // check rr_type and free data elements
  switch (rr->type) {
  case RR_PTR:
    if (rr->data.PTR.name)
      free(rr->data.PTR.name);
    // don't free entry
    break;

  case RR_TXT:
    txt_rec = &rr->data.TXT;
    while (txt_rec) {
      struct rr_data_txt *next = txt_rec->next;
      if (txt_rec->txt)
        free(txt_rec->txt);

      // only free() if it wasn't part of the struct
      if (txt_rec != &rr->data.TXT)
        free(txt_rec);

      txt_rec = next;
    }
    break;

  case RR_SRV:
    if (rr->data.SRV.target)
      free(rr->data.SRV.target);
    break;

  default:
    // nothing to free
    break;
  }

  free(rr->name);
  free(rr);
}

// destroys an RR list (and optionally, items)
void rr_list_destroy(struct rr_list *rr, char destroy_items) {
  struct rr_list *rr_next;

  for (; rr; rr = rr_next) {
    rr_next = rr->next;
    if (destroy_items)
      rr_entry_destroy(rr->e);
    free(rr);
  }
}

int rr_list_count(struct rr_list *rr) {
  int i = 0;
  for (; rr; i++, rr = rr->next)
    ;
  return i;
}

struct rr_entry *rr_list_remove(struct rr_list **rr_head, struct rr_entry *rr) {
  struct rr_list *le = *rr_head, *pe = NULL;
  for (; le; le = le->next) {
    if (le->e == rr) {
      if (pe == NULL) {
        *rr_head = le->next;
        free(le);
        return rr;
      } else {
        pe->next = le->next;
        free(le);
        return rr;
      }
    }
    pe = le;
  }
  return NULL;
}

// appends an rr_entry to an RR list
// if the RR is already in the list, it will not be added
// RRs are compared by memory location - not its contents
// return value of 0 means item not added
int rr_list_append(struct rr_list **rr_head, struct rr_entry *rr) {
  struct rr_list *node = malloc(sizeof(struct rr_list));
  node->e = rr;
  node->next = NULL;

  if (*rr_head == NULL) {
    *rr_head = node;
  } else {
    struct rr_list *e = *rr_head, *taile;
    for (; e; e = e->next) {
      // already in list - don't add
      if (e->e == rr) {
        free(node);
        return 0;
      }
      if (e->next == NULL)
        taile = e;
    }
    taile->next = node;
  }
  return 1;
}

#define FILL_RR_ENTRY(rr, _name, _type)                                                            \
  rr->name = _name;                                                                                \
  rr->type = _type;                                                                                \
  rr->ttl = DEFAULT_TTL;                                                                           \
  rr->cache_flush = 1;                                                                             \
  rr->rr_class = 1;

struct rr_entry *rr_create_a(uint8_t *name, uint32_t addr) {
  DECL_MALLOC_ZERO_STRUCT(rr, rr_entry);
  FILL_RR_ENTRY(rr, name, RR_A);
  rr->data.A.addr = addr;
  rr->ttl = DEFAULT_TTL_FOR_RECORD_WITH_HOSTNAME; // 120 seconds -- see RFC 6762 Section 10
  return rr;
}

struct rr_entry *rr_create_aaaa(uint8_t *name, struct in6_addr *addr) {
  DECL_MALLOC_ZERO_STRUCT(rr, rr_entry);
  FILL_RR_ENTRY(rr, name, RR_AAAA);
  rr->data.AAAA.addr = addr;
  rr->ttl = DEFAULT_TTL_FOR_RECORD_WITH_HOSTNAME; // 120 seconds -- see RFC 6762 Section 10
  return rr;
}

struct rr_entry *rr_create_srv(uint8_t *name, uint16_t port, uint8_t *target) {
  DECL_MALLOC_ZERO_STRUCT(rr, rr_entry);
  FILL_RR_ENTRY(rr, name, RR_SRV);
  rr->data.SRV.port = port;
  rr->data.SRV.target = target;
  return rr;
}

struct rr_entry *rr_create_ptr(uint8_t *name, struct rr_entry *d_rr) {
  DECL_MALLOC_ZERO_STRUCT(rr, rr_entry);
  FILL_RR_ENTRY(rr, name, RR_PTR);
  rr->cache_flush = 0; // PTRs shouldn't have their cache flush bit set
  rr->data.PTR.entry = d_rr;
  return rr;
}

struct rr_entry *rr_create(uint8_t *name, enum rr_type type) {
  DECL_MALLOC_ZERO_STRUCT(rr, rr_entry);
  FILL_RR_ENTRY(rr, name, type);
  return rr;
}

void rr_set_nsec(struct rr_entry *rr_nsec, enum rr_type type) {
  assert(rr_nsec->type = RR_NSEC);
  assert((type / 8) < sizeof(rr_nsec->data.NSEC.bitmap));

  rr_nsec->data.NSEC.bitmap[type / 8] = 1 << (7 - (type % 8));
}

void rr_add_txt(struct rr_entry *rr_txt, const char *txt) {
  struct rr_data_txt *txt_rec;
  assert(rr_txt->type == RR_TXT);

  txt_rec = &rr_txt->data.TXT;

  // is current data filled?
  if (txt_rec->txt == NULL) {
    txt_rec->txt = create_label(txt);
    return;
  }

  // find the last node
  for (; txt_rec->next; txt_rec = txt_rec->next)
    ;

  // create a new empty node
  txt_rec->next = malloc(sizeof(struct rr_data_txt));

  txt_rec = txt_rec->next;
  txt_rec->txt = create_label(txt);
  txt_rec->next = NULL;
}

// adds a record to an rr_group
void rr_group_add(struct rr_group **group, struct rr_entry *rr) {
  struct rr_group *g;

  assert(rr != NULL);

  if (*group) {
    g = rr_group_find(*group, rr->name);
    if (g) {
      rr_list_append(&g->rr, rr);
      return;
    }
  }

  MALLOC_ZERO_STRUCT(g, rr_group);
  g->name = dup_nlabel(rr->name);
  rr_list_append(&g->rr, rr);

  // prepend to list
  g->next = *group;
  *group = g;
}

// finds a rr_group matching the given name
struct rr_group *rr_group_find(struct rr_group *g, uint8_t *name) {
  for (; g; g = g->next) {
    if (cmp_nlabel(g->name, name) == 0)
      return g;
  }
  return NULL;
}

struct rr_entry *rr_entry_find(struct rr_list *rr_list, uint8_t *name, uint16_t type) {
  struct rr_list *rr = rr_list;
  for (; rr; rr = rr->next) {
    if (rr->e->type == type && cmp_nlabel(rr->e->name, name) == 0)
      return rr->e;
  }
  return NULL;
}

// looks for a matching entry in rr_list
// if entry is a PTR, we need to check if the PTR target also matches
struct rr_entry *rr_entry_match(struct rr_list *rr_list, struct rr_entry *entry) {
  struct rr_list *rr = rr_list;
  for (; rr; rr = rr->next) {
    if (rr->e->type == entry->type && cmp_nlabel(rr->e->name, entry->name) == 0) {
      if (entry->type != RR_PTR) {
        return rr->e;
      } else if (cmp_nlabel(MDNS_RR_GET_PTR_NAME(entry), MDNS_RR_GET_PTR_NAME(rr->e)) == 0) {
        // if it's a PTR, we need to make sure PTR target also matches
        return rr->e;
      }
    }
  }
  return NULL;
}

void rr_group_destroy(struct rr_group *group) {
  struct rr_group *g = group;

  while (g) {
    struct rr_group *nextg = g->next;
    free(g->name);
    rr_list_destroy(g->rr, 1);
    free(g);
    g = nextg;
  }
}

uint8_t *mdns_write_u16(uint8_t *ptr, const uint16_t v) {
  *ptr++ = (uint8_t)(v >> 8) & 0xFF;
  *ptr++ = (uint8_t)(v >> 0) & 0xFF;
  return ptr;
}

uint8_t *mdns_write_u32(uint8_t *ptr, const uint32_t v) {
  *ptr++ = (uint8_t)(v >> 24) & 0xFF;
  *ptr++ = (uint8_t)(v >> 16) & 0xFF;
  *ptr++ = (uint8_t)(v >> 8) & 0xFF;
  *ptr++ = (uint8_t)(v >> 0) & 0xFF;
  return ptr;
}

uint16_t mdns_read_u16(const uint8_t *ptr) {
  return ((ptr[0] & 0xFF) << 8) | ((ptr[1] & 0xFF) << 0);
}

uint32_t mdns_read_u32(const uint8_t *ptr) {
  return ((ptr[0] & 0xFF) << 24) | ((ptr[1] & 0xFF) << 16) | ((ptr[2] & 0xFF) << 8) |
         ((ptr[3] & 0xFF) << 0);
}

// initialize the packet for reply
// clears the packet of list structures but not its list items
void mdns_init_reply(struct mdns_pkt *pkt, uint16_t id) {
  // copy transaction ID
  pkt->id = id;

  // response flags
  pkt->flags = MDNS_FLAG_RESP | MDNS_FLAG_AA;

  rr_list_destroy(pkt->rr_qn, 0);
  rr_list_destroy(pkt->rr_ans, 0);
  rr_list_destroy(pkt->rr_auth, 0);
  rr_list_destroy(pkt->rr_add, 0);

  pkt->rr_qn = NULL;
  pkt->rr_ans = NULL;
  pkt->rr_auth = NULL;
  pkt->rr_add = NULL;

  pkt->num_qn = 0;
  pkt->num_ans_rr = 0;
  pkt->num_auth_rr = 0;
  pkt->num_add_rr = 0;
}

// destroys an mdns_pkt struct, including its contents
void mdns_pkt_destroy(struct mdns_pkt *p) {
  rr_list_destroy(p->rr_qn, 1);
  rr_list_destroy(p->rr_ans, 1);
  rr_list_destroy(p->rr_auth, 1);
  rr_list_destroy(p->rr_add, 1);

  free(p);
}

// parse the MDNS questions section
// stores the parsed data in the given mdns_pkt struct
static size_t mdns_parse_qn(uint8_t *pkt_buf, size_t pkt_len, size_t off, struct mdns_pkt *pkt) {
  const uint8_t *p = pkt_buf + off;
  struct rr_entry *rr;
  uint8_t *name;

  assert(pkt != NULL);

  rr = malloc(sizeof(struct rr_entry));
  memset(rr, 0, sizeof(struct rr_entry));

  name = uncompress_nlabel(pkt_buf, pkt_len, off);
  p += label_len(pkt_buf, pkt_len, off);
  rr->name = name;

  rr->type = mdns_read_u16(p);
  p += sizeof(uint16_t);

  rr->unicast_query = (*p & 0x80) == 0x80;
  rr->rr_class = mdns_read_u16(p) & ~0x80;
  p += sizeof(uint16_t);

  rr_list_append(&pkt->rr_qn, rr);

  return p - (pkt_buf + off);
}

// parse the MDNS RR section
// stores the parsed data in the given mdns_pkt struct
static size_t mdns_parse_rr(uint8_t *pkt_buf, size_t pkt_len, size_t off, struct mdns_pkt *pkt) {
  const uint8_t *p = pkt_buf + off;
  const uint8_t *e = pkt_buf + pkt_len;
  struct rr_entry *rr;
  uint8_t *name;
  size_t rr_data_len = 0;
  struct rr_data_txt *txt_rec;
  int parse_error = 0;

  assert(pkt != NULL);

  if (off > pkt_len)
    return 0;

  rr = malloc(sizeof(struct rr_entry));
  memset(rr, 0, sizeof(struct rr_entry));

  name = uncompress_nlabel(pkt_buf, pkt_len, off);
  p += label_len(pkt_buf, pkt_len, off);
  rr->name = name;

  rr->type = mdns_read_u16(p);
  p += sizeof(uint16_t);

  rr->cache_flush = (*p & 0x80) == 0x80;
  rr->rr_class = mdns_read_u16(p) & ~0x80;
  p += sizeof(uint16_t);

  rr->ttl = mdns_read_u32(p);
  p += sizeof(uint32_t);

  // RR data
  rr_data_len = mdns_read_u16(p);
  p += sizeof(uint16_t);

  if (p + rr_data_len > e) {
    DEBUG_PRINTF("rr_data_len goes beyond packet buffer: %lu > %lu\n", rr_data_len, e - p);
    rr_entry_destroy(rr);
    return 0;
  }

  e = p + rr_data_len;

  // see if we can parse the RR data
  switch (rr->type) {
  case RR_A:
    if (rr_data_len < sizeof(uint32_t)) {
      DEBUG_PRINTF("invalid rr_data_len=%lu for A record\n", rr_data_len);
      parse_error = 1;
      break;
    }
    rr->data.A.addr = ntohl(mdns_read_u32(p)); /* addr already in net order */
    p += sizeof(uint32_t);
    break;

  case RR_AAAA:
    if (rr_data_len < sizeof(struct in6_addr)) {
      DEBUG_PRINTF("invalid rr_data_len=%lu for AAAA record\n", rr_data_len);
      parse_error = 1;
      break;
    }
    rr->data.AAAA.addr = malloc(sizeof(struct in6_addr));
    int i;
    for (i = 0; i < sizeof(struct in6_addr); i++)
      rr->data.AAAA.addr->s6_addr[i] = p[i];
    p += sizeof(struct in6_addr);
    break;

  case RR_PTR:
    rr->data.PTR.name = uncompress_nlabel(pkt_buf, pkt_len, p - pkt_buf);
    if (rr->data.PTR.name == NULL) {
      DEBUG_PRINTF("unable to parse/uncompress label for PTR name\n");
      parse_error = 1;
      break;
    }
    p += rr_data_len;
    break;

  case RR_TXT:
    txt_rec = &rr->data.TXT;

    // not supposed to happen, but we should handle it
    if (rr_data_len == 0) {
      DEBUG_PRINTF("WARN: rr_data_len for TXT is 0\n");
      txt_rec->txt = create_label("");
      break;
    }

    while (1) {
      txt_rec->txt = copy_label(pkt_buf, pkt_len, p - pkt_buf);
      if (txt_rec->txt == NULL) {
        DEBUG_PRINTF("unable to copy label for TXT record\n");
        parse_error = 1;
        break;
      }
      p += txt_rec->txt[0] + 1;

      if (p >= e)
        break;

      // allocate another record
      txt_rec->next = malloc(sizeof(struct rr_data_txt));
      txt_rec = txt_rec->next;
      txt_rec->next = NULL;
    }
    break;

  default:
    // skip to end of RR data
    p = e;
  }

  // if there was a parse error, destroy partial rr_entry
  if (parse_error) {
    rr_entry_destroy(rr);
    return 0;
  }

  rr_list_append(&pkt->rr_ans, rr);

  return p - (pkt_buf + off);
}

// parse a MDNS packet into an mdns_pkt struct
struct mdns_pkt *mdns_parse_pkt(uint8_t *pkt_buf, size_t pkt_len) {
  uint8_t *p = pkt_buf;
  size_t off;
  struct mdns_pkt *pkt;
  int i;

  if (pkt_len < 12)
    return NULL;

  MALLOC_ZERO_STRUCT(pkt, mdns_pkt);

  // parse header
  pkt->id = mdns_read_u16(p);
  p += sizeof(uint16_t);
  pkt->flags = mdns_read_u16(p);
  p += sizeof(uint16_t);
  pkt->num_qn = mdns_read_u16(p);
  p += sizeof(uint16_t);
  pkt->num_ans_rr = mdns_read_u16(p);
  p += sizeof(uint16_t);
  pkt->num_auth_rr = mdns_read_u16(p);
  p += sizeof(uint16_t);
  pkt->num_add_rr = mdns_read_u16(p);
  p += sizeof(uint16_t);

  off = p - pkt_buf;

  // parse questions
  for (i = 0; i < pkt->num_qn; i++) {
    size_t l = mdns_parse_qn(pkt_buf, pkt_len, off, pkt);
    if (!l) {
      DEBUG_PRINTF("error parsing question #%d\n", i);
      mdns_pkt_destroy(pkt);
      return NULL;
    }

    off += l;
  }

  // parse answer RRs
  for (i = 0; i < pkt->num_ans_rr; i++) {
    size_t l = mdns_parse_rr(pkt_buf, pkt_len, off, pkt);
    if (!l) {
      DEBUG_PRINTF("error parsing answer #%d\n", i);
      mdns_pkt_destroy(pkt);
      return NULL;
    }

    off += l;
  }

  // TODO: parse the authority and additional RR sections

  return pkt;
}

// encodes a name (label) into a packet using the name compression scheme
// encoded names will be added to the compression list for subsequent use
static size_t mdns_encode_name(uint8_t *pkt_buf, size_t pkt_len, size_t off, const uint8_t *name,
                               struct name_comp *comp) {
  struct name_comp *c, *c_tail = NULL;
  uint8_t *p = pkt_buf + off;
  size_t len = 0;

  if (name) {
    while (*name) {
      // find match for compression
      for (c = comp; c; c = c->next) {
        if (cmp_nlabel(name, c->label) == 0) {
          mdns_write_u16(p, 0xC000 | (c->pos & ~0xC000));
          return len + sizeof(uint16_t);
        }

        if (c->next == NULL)
          c_tail = c;
      }

      // copy this segment
      int segment_len = *name + 1;
      strncpy((char *)p, (char *)name, segment_len);

      // cache the name for subsequent compression
      DECL_MALLOC_ZERO_STRUCT(new_c, name_comp);

      new_c->label = (uint8_t *)name;
      new_c->pos = p - pkt_buf;
      c_tail->next = new_c;

      // advance to next name segment
      p += segment_len;
      len += segment_len;
      name += segment_len;
    }
  }

  *p = '\0'; // root "label"
  len += 1;

  return len;
}

// encodes an RR entry at the given offset
// returns the size of the entire RR entry
static size_t mdns_encode_rr(uint8_t *pkt_buf, size_t pkt_len, size_t off, struct rr_entry *rr,
                             struct name_comp *comp) {
  uint8_t *p = pkt_buf + off, *p_data;
  size_t l;
  struct rr_data_txt *txt_rec;
  uint8_t *label;
  int i;

  assert(off < pkt_len);

  // name
  l = mdns_encode_name(pkt_buf, pkt_len, off, rr->name, comp);
  assert(l != 0);
  p += l;

  // type
  p = mdns_write_u16(p, rr->type);

  // class & cache flush
  p = mdns_write_u16(p, (rr->rr_class & ~0x8000) | (rr->cache_flush << 15));

  // TTL
  p = mdns_write_u32(p, rr->ttl);

  // data length (filled in later)
  p += sizeof(uint16_t);

  // start of data marker
  p_data = p;

  switch (rr->type) {
  case RR_A:
    /* htonl() needed coz addr already in net order */
    p = mdns_write_u32(p, htonl(rr->data.A.addr));
    break;

  case RR_AAAA:
    for (i = 0; i < sizeof(struct in6_addr); i++)
      *p++ = rr->data.AAAA.addr->s6_addr[i];
    break;

  case RR_PTR:
    label = rr->data.PTR.name ? rr->data.PTR.name : rr->data.PTR.entry->name;
    p += mdns_encode_name(pkt_buf, pkt_len, p - pkt_buf, label, comp);
    break;

  case RR_TXT:
    txt_rec = &rr->data.TXT;
    for (; txt_rec; txt_rec = txt_rec->next) {
      int len = txt_rec->txt[0] + 1;
      strncpy((char *)p, (char *)txt_rec->txt, len);
      p += len;
    }
    break;

  case RR_SRV:
    p = mdns_write_u16(p, rr->data.SRV.priority);

    p = mdns_write_u16(p, rr->data.SRV.weight);

    p = mdns_write_u16(p, rr->data.SRV.port);

    p += mdns_encode_name(pkt_buf, pkt_len, p - pkt_buf, rr->data.SRV.target, comp);
    break;

  case RR_NSEC:
    p += mdns_encode_name(pkt_buf, pkt_len, p - pkt_buf, rr->name, comp);

    *p++ = 0; // bitmap window/block number

    *p++ = sizeof(rr->data.NSEC.bitmap); // bitmap length

    for (i = 0; i < sizeof(rr->data.NSEC.bitmap); i++)
      *p++ = rr->data.NSEC.bitmap[i];

    break;

  default:
    DEBUG_PRINTF("unhandled rr type 0x%02x\n", rr->type);
  }

  // calculate data length based on p
  l = p - p_data;

  // fill in the length
  mdns_write_u16(p - l - sizeof(uint16_t), l);

  return p - pkt_buf - off;
}

// encodes a MDNS packet from the given mdns_pkt struct into a buffer
// returns the size of the entire MDNS packet
size_t mdns_encode_pkt(struct mdns_pkt *answer, uint8_t *pkt_buf, size_t pkt_len) {
  struct name_comp *comp;
  uint8_t *p = pkt_buf;
  // uint8_t *e = pkt_buf + pkt_len;
  size_t off;
  int i;

  assert(answer != NULL);
  assert(pkt_len >= 12);

  if (p == NULL)
    return -1;

  // this is an Answer - number of qns should be zero
  assert(answer->num_qn == 0);

  p = mdns_write_u16(p, answer->id);
  p = mdns_write_u16(p, answer->flags);
  p = mdns_write_u16(p, answer->num_qn);
  p = mdns_write_u16(p, answer->num_ans_rr);
  p = mdns_write_u16(p, answer->num_auth_rr);
  p = mdns_write_u16(p, answer->num_add_rr);

  off = p - pkt_buf;

  // allocate list for name compression
  comp = malloc(sizeof(struct name_comp));
  if (comp == NULL)
    return -1;
  memset(comp, 0, sizeof(struct name_comp));

  // dummy entry
  comp->label = (uint8_t *)"";
  comp->pos = 0;

  // skip encoding of qn

  struct rr_list *rr_set[] = {answer->rr_ans, answer->rr_auth, answer->rr_add};

  // encode answer, authority and additional RRs
  for (i = 0; i < sizeof(rr_set) / sizeof(rr_set[0]); i++) {
    struct rr_list *rr = rr_set[i];
    for (; rr; rr = rr->next) {
      size_t l = mdns_encode_rr(pkt_buf, pkt_len, off, rr->e, comp);
      off += l;

      if (off >= pkt_len) {
        DEBUG_PRINTF("packet buffer too small\n");
        return -1;
      }
    }
  }

  // free name compression list
  while (comp) {
    struct name_comp *c = comp->next;
    free(comp);
    comp = c;
  }

  return off;
}

//******************************************************//
//                      mdnsd.c                         //
//******************************************************//

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define LOG_ERR 3
#else
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <syslog.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

/*
 * Define a proper IP socket level if not already done.
 * Required to compile on OS X
 */
#ifndef SOL_IP
#define SOL_IP IPPROTO_IP
#endif

#define MDNS_ADDR "224.0.0.251"
#define MDNS_PORT 5353

#define PACKET_SIZE 65536

#define SERVICES_DNS_SD_NLABEL ((uint8_t *)"\x09_services\x07_dns-sd\x04_udp\x05local")

struct mdnsd {
  pthread_mutex_t data_lock;
  int sockfd;
  int notify_pipe[2];
  int stop_flag;

  struct rr_group *group;
  struct rr_list *announce;
  struct rr_list *services;
  uint8_t *hostname;
};

struct mdns_service {
  struct rr_list *entries;
};

/////////////////////////////////

static int create_recv_sock() {
  int sd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sd < 0) {
    log_message(LOG_ERR, "recv socket(): %m");
    return sd;
  }

  int r = -1;

  int on = 1;
  if ((r = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on))) < 0) {
    log_message(LOG_ERR, "recv setsockopt(SO_REUSEADDR): %m");
    return r;
  }

  /* bind to an address */
  struct sockaddr_in serveraddr;
  memset(&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_port = htons(MDNS_PORT);
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); /* receive multicast */
  if ((r = bind(sd, (struct sockaddr *)&serveraddr, sizeof(serveraddr))) < 0) {
    log_message(LOG_ERR, "recv bind(): %m");
  }

  // add membership to receiving socket
  struct ip_mreq mreq;
  memset(&mreq, 0, sizeof(struct ip_mreq));
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  mreq.imr_multiaddr.s_addr = inet_addr(MDNS_ADDR);
  if ((r = setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq))) < 0) {
    log_message(LOG_ERR, "recv setsockopt(IP_ADD_MEMBERSHIP): %m");
    return r;
  }

  // enable loopback in case someone else needs the data
  if ((r = setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&on, sizeof(on))) < 0) {
    log_message(LOG_ERR, "recv setsockopt(IP_MULTICAST_LOOP): %m");
    return r;
  }

#ifdef IP_PKTINFO
  if ((r = setsockopt(sd, SOL_IP, IP_PKTINFO, (char *)&on, sizeof(on))) < 0) {
    log_message(LOG_ERR, "recv setsockopt(IP_PKTINFO): %m");
    return r;
  }
#endif

  return sd;
}

static ssize_t send_packet(int fd, const void *data, size_t len) {
  static struct sockaddr_in toaddr;
  if (toaddr.sin_family != AF_INET) {
    memset(&toaddr, 0, sizeof(struct sockaddr_in));
    toaddr.sin_family = AF_INET;
    toaddr.sin_port = htons(MDNS_PORT);
    toaddr.sin_addr.s_addr = inet_addr(MDNS_ADDR);
  }

  return sendto(fd, data, len, 0, (struct sockaddr *)&toaddr, sizeof(struct sockaddr_in));
}

// populate the specified list which matches the RR name and type
// type can be RR_ANY, which populates all entries EXCEPT RR_NSEC
static int populate_answers(struct mdnsd *svr, struct rr_list **rr_head, uint8_t *name,
                            enum rr_type type) {
  int num_ans = 0;

  // check if we have the records
  pthread_mutex_lock(&svr->data_lock);
  struct rr_group *ans_grp = rr_group_find(svr->group, name);
  if (ans_grp == NULL) {
    pthread_mutex_unlock(&svr->data_lock);
    return num_ans;
  }

  // decide which records should go into answers
  struct rr_list *n = ans_grp->rr;
  for (; n; n = n->next) {
    // exclude NSEC for RR_ANY
    if (type == RR_ANY && n->e->type == RR_NSEC)
      continue;

    if ((type == n->e->type || type == RR_ANY) && cmp_nlabel(name, n->e->name) == 0) {
      num_ans += rr_list_append(rr_head, n->e);
    }
  }

  pthread_mutex_unlock(&svr->data_lock);

  return num_ans;
}

// given a list of RRs, look up related records and add them
static void add_related_rr(struct mdnsd *svr, struct rr_list *list, struct mdns_pkt *reply) {
  for (; list; list = list->next) {
    struct rr_entry *ans = list->e;

    switch (ans->type) {
    case RR_PTR:
      // target host A, AAAA records
      reply->num_add_rr += populate_answers(svr, &reply->rr_add, MDNS_RR_GET_PTR_NAME(ans), RR_ANY);
      break;

    case RR_SRV:
      // target host A, AAAA records
      reply->num_add_rr += populate_answers(svr, &reply->rr_add, ans->data.SRV.target, RR_ANY);

      // perhaps TXT records of the same name?
      // if we use RR_ANY, we risk pulling in the same RR_SRV
      reply->num_add_rr += populate_answers(svr, &reply->rr_add, ans->name, RR_TXT);
      break;

    case RR_A:
    case RR_AAAA:
      reply->num_add_rr += populate_answers(svr, &reply->rr_add, ans->name, RR_NSEC);
      break;

    default:
      // nothing to add
      break;
    }
  }
}

// creates an announce packet given the type name PTR
static void announce_srv(struct mdnsd *svr, struct mdns_pkt *reply, uint8_t *name) {
  mdns_init_reply(reply, 0);

  reply->num_ans_rr += populate_answers(svr, &reply->rr_ans, name, RR_PTR);

  // remember to add the services dns-sd PTR too
  reply->num_ans_rr += populate_answers(svr, &reply->rr_ans, SERVICES_DNS_SD_NLABEL, RR_PTR);

  // see if we can match additional records for answers
  add_related_rr(svr, reply->rr_ans, reply);

  // additional records for additional records
  add_related_rr(svr, reply->rr_add, reply);
}

// processes the incoming MDNS packet
// returns >0 if processed, 0 otherwise
static int process_mdns_pkt(struct mdnsd *svr, struct mdns_pkt *pkt, struct mdns_pkt *reply) {
  int i;

  assert(pkt != NULL);

  // is it standard query?
  if ((pkt->flags & MDNS_FLAG_RESP) == 0 && MDNS_FLAG_GET_OPCODE(pkt->flags) == 0) {
    mdns_init_reply(reply, pkt->id);

    DEBUG_PRINTF("flags = %04x, qn = %d, ans = %d, add = %d\n", pkt->flags, pkt->num_qn,
                 pkt->num_ans_rr, pkt->num_add_rr);

    // loop through questions
    struct rr_list *qnl = pkt->rr_qn;
    for (i = 0; i < pkt->num_qn; i++, qnl = qnl->next) {
      struct rr_entry *qn = qnl->e;
      int num_ans_added = 0;

      char *namestr = nlabel_to_str(qn->name);
      DEBUG_PRINTF("qn #%d: type %s (%02x) %s - ", i, rr_get_type_name(qn->type), qn->type,
                   namestr);
      free(namestr);

      // check if it's a unicast query - we ignore those
      if (qn->unicast_query) {
        DEBUG_PRINTF("skipping unicast query\n");
        continue;
      }

      num_ans_added = populate_answers(svr, &reply->rr_ans, qn->name, qn->type);
      reply->num_ans_rr += num_ans_added;

      DEBUG_PRINTF("added %d answers\n", num_ans_added);
    }

    // remove our replies if they were already in their answers
    struct rr_list *ans = NULL, *prev_ans = NULL;
    for (ans = reply->rr_ans; ans;) {
      struct rr_list *next_ans = ans->next;
      struct rr_entry *known_ans = rr_entry_match(pkt->rr_ans, ans->e);

      // discard answers that have at least half of the actual TTL
      if (known_ans != NULL && known_ans->ttl >= ans->e->ttl / 2) {
        char *namestr = nlabel_to_str(ans->e->name);
        DEBUG_PRINTF("removing answer for %s\n", namestr);
        free(namestr);

        // check if list item is head
        if (prev_ans == NULL)
          reply->rr_ans = ans->next;
        else
          prev_ans->next = ans->next;
        free(ans);

        ans = prev_ans;

        // adjust answer count
        reply->num_ans_rr--;
      }

      prev_ans = ans;
      ans = next_ans;
    }

    // see if we can match additional records for answers
    add_related_rr(svr, reply->rr_ans, reply);

    // additional records for additional records
    add_related_rr(svr, reply->rr_add, reply);

    DEBUG_PRINTF("\n");

    return reply->num_ans_rr;
  }

  return 0;
}

int create_pipe(int handles[2]) {
#ifdef _WIN32
  SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == INVALID_SOCKET) {
    return -1;
  }
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(0);
  serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
    closesocket(sock);
    return -1;
  }
  if (listen(sock, 1) == SOCKET_ERROR) {
    closesocket(sock);
    return -1;
  }
  int len = sizeof(serv_addr);
  if (getsockname(sock, (SOCKADDR *)&serv_addr, &len) == SOCKET_ERROR) {
    closesocket(sock);
    return -1;
  }
  if ((handles[1] = socket(PF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
    closesocket(sock);
    return -1;
  }
  if (connect(handles[1], (struct sockaddr *)&serv_addr, len) == SOCKET_ERROR) {
    closesocket(sock);
    return -1;
  }
  if ((handles[0] = accept(sock, (struct sockaddr *)&serv_addr, &len)) == INVALID_SOCKET) {
    closesocket((SOCKET)handles[1]);
    handles[1] = INVALID_SOCKET;
    closesocket(sock);
    return -1;
  }
  closesocket(sock);
  return 0;
#else
  return pipe(handles);
#endif
}

int read_pipe(int s, char *buf, int len) {
#ifdef _WIN32
  int ret = recv(s, buf, len, 0);
  if (ret < 0 && WSAGetLastError() == WSAECONNRESET) {
    ret = 0;
  }
  return ret;
#else
  return read(s, buf, len);
#endif
}

int write_pipe(int s, char *buf, int len) {
#ifdef _WIN32
  return send(s, buf, len, 0);
#else
  return write(s, buf, len);
#endif
}

int close_pipe(int s) {
#ifdef _WIN32
  return closesocket(s);
#else
  return close(s);
#endif
}

// main loop to receive, process and send out MDNS replies
// also handles MDNS service announces
static void main_loop(struct mdnsd *svr) {
  fd_set sockfd_set;
  int max_fd = svr->sockfd;
  char notify_buf[2]; // buffer for reading of notify_pipe

  void *pkt_buffer = malloc(PACKET_SIZE);

  if (svr->notify_pipe[0] > max_fd)
    max_fd = svr->notify_pipe[0];

  struct mdns_pkt *mdns_reply = malloc(sizeof(struct mdns_pkt));
  memset(mdns_reply, 0, sizeof(struct mdns_pkt));

  while (!svr->stop_flag) {
    FD_ZERO(&sockfd_set);
    FD_SET(svr->sockfd, &sockfd_set);
    FD_SET(svr->notify_pipe[0], &sockfd_set);
    select(max_fd + 1, &sockfd_set, NULL, NULL, NULL);

    if (FD_ISSET(svr->notify_pipe[0], &sockfd_set)) {
      // flush the notify_pipe
      read_pipe(svr->notify_pipe[0], (char *)&notify_buf, 1);
    } else if (FD_ISSET(svr->sockfd, &sockfd_set)) {
      struct sockaddr_in fromaddr;
      socklen_t sockaddr_size = sizeof(struct sockaddr_in);

      ssize_t recvsize = recvfrom(svr->sockfd, pkt_buffer, PACKET_SIZE, 0,
                                  (struct sockaddr *)&fromaddr, &sockaddr_size);
      if (recvsize < 0) {
        log_message(LOG_ERR, "recv(): %m");
      }

      DEBUG_PRINTF("data from=%s size=%ld\n", inet_ntoa(fromaddr.sin_addr), (long)recvsize);
      struct mdns_pkt *mdns = mdns_parse_pkt(pkt_buffer, recvsize);
      if (mdns != NULL) {
        if (process_mdns_pkt(svr, mdns, mdns_reply)) {
          size_t replylen = mdns_encode_pkt(mdns_reply, pkt_buffer, PACKET_SIZE);
          send_packet(svr->sockfd, pkt_buffer, replylen);
        } else if (mdns->num_qn == 0) {
          DEBUG_PRINTF("(no questions in packet)\n\n");
        }

        mdns_pkt_destroy(mdns);
      }
    }

    // send out announces
    while (1) {
      struct rr_entry *ann_e = NULL;

      // extract from head of list
      pthread_mutex_lock(&svr->data_lock);
      if (svr->announce)
        ann_e = rr_list_remove(&svr->announce, svr->announce->e);
      pthread_mutex_unlock(&svr->data_lock);

      if (!ann_e)
        break;

      char *namestr = nlabel_to_str(ann_e->name);
      DEBUG_PRINTF("sending announce for %s\n", namestr);
      free(namestr);

      announce_srv(svr, mdns_reply, ann_e->name);

      if (mdns_reply->num_ans_rr > 0) {
        size_t replylen = mdns_encode_pkt(mdns_reply, pkt_buffer, PACKET_SIZE);
        send_packet(svr->sockfd, pkt_buffer, replylen);
      }
    }
  }

  // main thread terminating. send out "goodbye packets" for services
  mdns_init_reply(mdns_reply, 0);

  pthread_mutex_lock(&svr->data_lock);
  struct rr_list *svc_le = svr->services;
  for (; svc_le; svc_le = svc_le->next) {
    // set TTL to zero
    svc_le->e->ttl = 0;
    mdns_reply->num_ans_rr += rr_list_append(&mdns_reply->rr_ans, svc_le->e);
  }
  pthread_mutex_unlock(&svr->data_lock);

  // send out packet
  if (mdns_reply->num_ans_rr > 0) {
    size_t replylen = mdns_encode_pkt(mdns_reply, pkt_buffer, PACKET_SIZE);
    send_packet(svr->sockfd, pkt_buffer, replylen);
  }

  // destroy packet
  mdns_init_reply(mdns_reply, 0);
  free(mdns_reply);

  free(pkt_buffer);

  close_pipe(svr->sockfd);

  svr->stop_flag = 2;
}

/////////////////////////////////////////////////////

void mdnsd_set_hostname(struct mdnsd *svr, const char *hostname, uint32_t ip) {
  struct rr_entry *a_e = NULL, *nsec_e = NULL;

  // currently can't be called twice
  // dont ask me what happens if the IP changes
  assert(svr->hostname == NULL);

  a_e = rr_create_a(create_nlabel(hostname), ip); // 120 seconds automatically

  nsec_e = rr_create(create_nlabel(hostname), RR_NSEC);
  nsec_e->ttl = DEFAULT_TTL_FOR_RECORD_WITH_HOSTNAME; // set to 120 seconds (default is 4500)
  rr_set_nsec(nsec_e, RR_A);

  pthread_mutex_lock(&svr->data_lock);
  svr->hostname = create_nlabel(hostname);
  rr_group_add(&svr->group, a_e);
  rr_group_add(&svr->group, nsec_e);
  pthread_mutex_unlock(&svr->data_lock);
}

void mdnsd_set_hostname_v6(struct mdnsd *svr, const char *hostname, struct in6_addr *addr) {
  struct rr_entry *aaaa_e = NULL, *nsec_e = NULL;

  // currently can't be called twice
  // dont ask me what happens if the IP changes
  assert(svr->hostname == NULL);

  aaaa_e = rr_create_aaaa(create_nlabel(hostname), addr); // 120 seconds automatically

  nsec_e = rr_create(create_nlabel(hostname), RR_NSEC);
  nsec_e->ttl = DEFAULT_TTL_FOR_RECORD_WITH_HOSTNAME; // set to 120 seconds (default is 4500)
  rr_set_nsec(nsec_e, RR_AAAA);

  pthread_mutex_lock(&svr->data_lock);
  svr->hostname = create_nlabel(hostname);
  rr_group_add(&svr->group, aaaa_e);
  rr_group_add(&svr->group, nsec_e);
  pthread_mutex_unlock(&svr->data_lock);
}

void mdnsd_add_rr(struct mdnsd *svr, struct rr_entry *rr) {
  pthread_mutex_lock(&svr->data_lock);
  rr_group_add(&svr->group, rr);
  pthread_mutex_unlock(&svr->data_lock);
}

struct mdns_service *mdnsd_register_svc(struct mdnsd *svr, const char *instance_name,
                                        const char *type, uint16_t port, const char *hostname,
                                        const char *txt[]) {
  struct rr_entry *txt_e = NULL, *srv_e = NULL, *ptr_e = NULL, *bptr_e = NULL;
  uint8_t *target;
  uint8_t *inst_nlabel, *type_nlabel, *nlabel;
  struct mdns_service *service = malloc(sizeof(struct mdns_service));
  memset(service, 0, sizeof(struct mdns_service));

  // combine service name
  type_nlabel = create_nlabel(type);
  inst_nlabel = create_label(instance_name);
  nlabel = join_nlabel(inst_nlabel, type_nlabel);

  // create TXT record
  if (txt && *txt) {
    txt_e = rr_create(dup_nlabel(nlabel), RR_TXT); // automatically 4500 seconds
    rr_list_append(&service->entries, txt_e);

    // add TXTs
    for (; *txt; txt++)
      rr_add_txt(txt_e, *txt);
  }

  // create SRV record
  assert(hostname || svr->hostname); // either one as target
  target = hostname ? create_nlabel(hostname) : dup_nlabel(svr->hostname);

  srv_e = rr_create_srv(dup_nlabel(nlabel), port, target); // automatically 4500 seconds
  rr_list_append(&service->entries, srv_e);

  // create PTR record for type
  ptr_e = rr_create_ptr(type_nlabel, srv_e); // automatically 4500 seconds

  // create services PTR record for type
  // this enables the type to show up as a "service"
  bptr_e = rr_create_ptr(dup_nlabel(SERVICES_DNS_SD_NLABEL), ptr_e); // automatically 4500 seconds

  // modify lists here
  pthread_mutex_lock(&svr->data_lock);

  if (txt_e)
    rr_group_add(&svr->group, txt_e);
  rr_group_add(&svr->group, srv_e);
  rr_group_add(&svr->group, ptr_e);
  rr_group_add(&svr->group, bptr_e);

  // append PTR entry to announce list
  rr_list_append(&svr->announce, ptr_e);
  rr_list_append(&svr->services, ptr_e);

  pthread_mutex_unlock(&svr->data_lock);

  // don't free type_nlabel - it's with the PTR record
  free(nlabel);
  free(inst_nlabel);

  // notify server
  write_pipe(svr->notify_pipe[1], ".", 1);

  return service;
}

void mdns_service_destroy(struct mdns_service *srv) {
  assert(srv != NULL);
  rr_list_destroy(srv->entries, 0);
  free(srv);
}

struct mdnsd *mdnsd_start() {
  pthread_t tid;
  pthread_attr_t attr;

  struct mdnsd *server = malloc(sizeof(struct mdnsd));
  memset(server, 0, sizeof(struct mdnsd));

  if (create_pipe(server->notify_pipe) != 0) {
    log_message(LOG_ERR, "pipe(): %m\n");
    free(server);
    return NULL;
  }

  server->sockfd = create_recv_sock();
  if (server->sockfd < 0) {
    log_message(LOG_ERR, "unable to create recv socket");
    free(server);
    return NULL;
  }

  pthread_mutex_init(&server->data_lock, NULL);

  // init thread
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  if (pthread_create(&tid, &attr, (void *(*)(void *))main_loop, (void *)server) != 0) {
    pthread_mutex_destroy(&server->data_lock);
    free(server);
    return NULL;
  }

  return server;
}

void mdnsd_stop(struct mdnsd *s) {
  assert(s != NULL);

  struct timeval tv = {
      .tv_sec = 0, .tv_usec = 500 * 1000,
  };

  s->stop_flag = 1;
  write_pipe(s->notify_pipe[1], ".", 1);

  while (s->stop_flag != 2)
    select(0, NULL, NULL, NULL, &tv);

  close_pipe(s->notify_pipe[0]);
  close_pipe(s->notify_pipe[1]);

  pthread_mutex_destroy(&s->data_lock);
  rr_group_destroy(s->group);
  rr_list_destroy(s->announce, 0);
  rr_list_destroy(s->services, 0);

  if (s->hostname)
    free(s->hostname);

  free(s);
}
