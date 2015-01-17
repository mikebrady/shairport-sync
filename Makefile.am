SUBDIRS = man

bin_PROGRAMS = shairport-sync
shairport_sync_SOURCES = shairport.c rtsp.c mdns.c mdns_external.c common.c rtp.c player.c alac.c audio.c audio_dummy.c audio_pipe.c

if USE_CUSTOMPIDDIR
AM_CFLAGS= \
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

if USE_AO
shairport_sync_SOURCES += audio_ao.c
endif

if USE_PULSE
shairport_sync_SOURCES += audio_pulse.c
endif

if USE_DNS_SD
shairport_sync_SOURCES += mdns_dns_sd.c
endif
