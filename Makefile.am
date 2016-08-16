SUBDIRS = man

bin_PROGRAMS = shairport-sync
shairport_sync_SOURCES = shairport.c rtsp.c mdns.c mdns_external.c common.c rtp.c player.c alac.c audio.c 

AM_CFLAGS = -Wno-multichar

if USE_CUSTOMPIDDIR
AM_CFLAGS+= \
	-DPIDDIR=\"$(CUSTOM_PID_DIR)\"
endif

if USE_AVAHI
shairport_sync_SOURCES += mdns_avahi.c
endif

if USE_TINYSVCMDNS
shairport_sync_SOURCES += mdns_tinysvcmdns.c tinysvcmdns.c
endif

if USE_ALSA
shairport_sync_SOURCES += audio_alsa.c
endif

if USE_SNDIO
shairport_sync_SOURCES += audio_sndio.c
endif

if USE_STDOUT
shairport_sync_SOURCES += audio_stdout.c
endif

if USE_PIPE
shairport_sync_SOURCES += audio_pipe.c
endif

if USE_DUMMY
shairport_sync_SOURCES += audio_dummy.c
endif

if USE_AO
shairport_sync_SOURCES += audio_ao.c
endif

if USE_PULSE
shairport_sync_SOURCES += audio_pulse.c
endif

if USE_DNS_SD
shairport_sync_SOURCES += mdns_dns_sd.c
endif

install-exec-hook:
if INSTALL_CONFIG_FILES
	[ -e $(DESTDIR)$(sysconfdir) ] || mkdir $(DESTDIR)$(sysconfdir)
	cp scripts/shairport-sync.conf $(DESTDIR)$(sysconfdir)/shairport-sync.conf.sample
	[ -f $(DESTDIR)$(sysconfdir)/shairport-sync.conf ] || cp scripts/shairport-sync.conf $(DESTDIR)$(sysconfdir)/shairport-sync.conf
endif
if INSTALL_SYSTEMV
	[ -e $(DESTDIR)$(sysconfdir)/init.d ] || mkdir -p $(DESTDIR)$(sysconfdir)/init.d
	[ -f $(DESTDIR)$(sysconfdir)/init.d/shairport-sync ] || cp scripts/shairport-sync $(DESTDIR)$(sysconfdir)/init.d/
endif
if INSTALL_SYSTEMD
	[ -e $(DESTDIR)$(systemdsystemunitdir) ] || mkdir -p $(DESTDIR)$(systemdsystemunitdir)
	[ -f $(DESTDIR)$(systemdsystemunitdir)/shairport-sync.service ] || cp scripts/shairport-sync.service $(DESTDIR)$(systemdsystemunitdir)
endif
