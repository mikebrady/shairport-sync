#ifndef _MDNS_H
#define _MDNS_H

#include "config.h"

extern int mdns_pid;

void mdns_unregister(void);
void mdns_register(void);
void mdns_ls_backends(void);

typedef struct {
  char *name;
  int (*mdns_register)(char *apname, int port);
  void (*mdns_unregister)(void);
} mdns_backend;

#ifdef CONFIG_METADATA

#define METADATA_EXPRESSION config.get_coverart ? "md=0,1,2" : "md=0,2"

#define MDNS_RECORD_WITH_METADATA \
	"sf=0x4", "fv=76400.10", "am=ShairportSync", "vs=105.1", "tp=TCP,UDP", "vn=65537", \
		METADATA_EXPRESSION, "ss=16", "sr=44100", "da=true", "sv=false", "et=0,1", "ek=1", "cn=0,1", "ch=2", "txtvers=1", config.password ? "pw=true" : "pw=false"

#endif

#define MDNS_RECORD_WITHOUT_METADATA \
	"sf=0x4", "fv=76400.10", "am=ShairportSync", "vs=105.1", "tp=TCP,UDP", "vn=65537", \
		"ss=16", "sr=44100", "da=true", "sv=false", "et=0,1", "ek=1", "cn=0,1", "ch=2", "txtvers=1", config.password ? "pw=true" : "pw=false"

#endif // _MDNS_H



