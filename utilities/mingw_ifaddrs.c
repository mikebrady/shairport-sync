#include "config.h"

#ifdef CONFIG_FOR_MINGW

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "../ifaddrs.h"
#include "../net/if.h"
#include "mingw_compat.h"

#include <iphlpapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct sockaddr *copy_sockaddr(const SOCKET_ADDRESS *source) {
  if ((source == NULL) || (source->lpSockaddr == NULL) || (source->iSockaddrLength <= 0))
    return NULL;

  struct sockaddr *copy = malloc((size_t)source->iSockaddrLength);
  if (copy)
    memcpy(copy, source->lpSockaddr, (size_t)source->iSockaddrLength);
  return copy;
}

static struct sockaddr *make_netmask(int family, ULONG prefix_length) {
  if (family == AF_INET) {
    struct sockaddr_in *mask = calloc(1, sizeof(*mask));
    if (mask == NULL)
      return NULL;
    mask->sin_family = AF_INET;
    uint32_t bits = prefix_length == 0 ? 0 : (0xffffffffUL << (32 - prefix_length));
    mask->sin_addr.s_addr = htonl(bits);
    return (struct sockaddr *)mask;
  }

  if (family == AF_INET6) {
    struct sockaddr_in6 *mask = calloc(1, sizeof(*mask));
    if (mask == NULL)
      return NULL;
    mask->sin6_family = AF_INET6;
    for (ULONG bit = 0; bit < prefix_length && bit < 128; bit++)
      mask->sin6_addr.s6_addr[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
    return (struct sockaddr *)mask;
  }

  return NULL;
}

static unsigned int adapter_flags(const IP_ADAPTER_ADDRESSES *adapter) {
  unsigned int flags = 0;
  if (adapter->OperStatus == IfOperStatusUp)
    flags |= IFF_UP | IFF_RUNNING;
  if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
    flags |= IFF_LOOPBACK;
  return flags;
}

int getifaddrs(struct ifaddrs **ifap) {
  if (ifap == NULL)
    return -1;

  *ifap = NULL;

  ULONG size = 15000;
  IP_ADAPTER_ADDRESSES *adapters = NULL;
  ULONG rc;
  do {
    IP_ADAPTER_ADDRESSES *new_adapters = realloc(adapters, size);
    if (new_adapters == NULL) {
      free(adapters);
      return -1;
    }
    adapters = new_adapters;
    rc = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapters, &size);
  } while (rc == ERROR_BUFFER_OVERFLOW);

  if (rc != NO_ERROR) {
    free(adapters);
    return -1;
  }

  struct ifaddrs *head = NULL;
  struct ifaddrs **tail = &head;

  for (IP_ADAPTER_ADDRESSES *adapter = adapters; adapter != NULL; adapter = adapter->Next) {
    for (IP_ADAPTER_UNICAST_ADDRESS *addr = adapter->FirstUnicastAddress; addr != NULL;
         addr = addr->Next) {
      struct ifaddrs *entry = calloc(1, sizeof(*entry));
      if (entry == NULL) {
        freeifaddrs(head);
        free(adapters);
        return -1;
      }

      entry->ifa_name = strdup(adapter->AdapterName);
      entry->ifa_flags = adapter_flags(adapter);
      entry->ifa_addr = copy_sockaddr(&addr->Address);
      if (entry->ifa_addr)
        entry->ifa_netmask = make_netmask(entry->ifa_addr->sa_family, addr->OnLinkPrefixLength);

      *tail = entry;
      tail = &entry->ifa_next;
    }
  }

  free(adapters);
  *ifap = head;
  return 0;
}

void freeifaddrs(struct ifaddrs *ifa) {
  while (ifa) {
    struct ifaddrs *next = ifa->ifa_next;
    free(ifa->ifa_name);
    free(ifa->ifa_addr);
    free(ifa->ifa_netmask);
    free(ifa->ifa_dstaddr);
    free(ifa);
    ifa = next;
  }
}

int shairport_mingw_get_device_id(uint8_t *id, int int_length) {
  memset(id, 0, (size_t)int_length);

  ULONG size = 15000;
  IP_ADAPTER_ADDRESSES *adapters = malloc(size);
  if (adapters == NULL)
    return -1;

  ULONG rc = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, adapters, &size);
  if (rc == ERROR_BUFFER_OVERFLOW) {
    IP_ADAPTER_ADDRESSES *new_adapters = realloc(adapters, size);
    if (new_adapters == NULL) {
      free(adapters);
      return -1;
    }
    adapters = new_adapters;
    rc = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, adapters, &size);
  }

  int response = -1;
  if (rc == NO_ERROR) {
    for (IP_ADAPTER_ADDRESSES *adapter = adapters; adapter != NULL; adapter = adapter->Next) {
      if ((adapter->OperStatus == IfOperStatusUp) &&
          (adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK) && (adapter->PhysicalAddressLength > 0)) {
        ULONG copy_length = adapter->PhysicalAddressLength < (ULONG)int_length
                                ? adapter->PhysicalAddressLength
                                : (ULONG)int_length;
        memcpy(id, adapter->PhysicalAddress, copy_length);
        response = 0;
        break;
      }
    }
  }

  free(adapters);
  return response;
}

#endif
