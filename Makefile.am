ARFLAGS = cr

# To be able to ref to the user's own home directory:
homedir = @HOME@

# Note that xmlmantohtml isn't producing correct HTML.
# Uncomment the if/else and SUBDIR lines in this stanza to make xmltoman build shairport-sync.1
# if USE_XMLTOMAN
#   SUBDIRS = man
# else
  man_MANS = $(top_srcdir)/man/shairport-sync.1
# endif

lib_pair_ap_a_CFLAGS = -Wall -g -DCONFIG_GCRYPT -pthread 
lib_tinyhttp_a_CFLAGS = -pthread
lib_dbus_interface_a_CFLAGS = -pthread
lib_mpris_interface_a_CFLAGS = -pthread

bin_PROGRAMS = shairport-sync

# BUILT_SOURCES get built before anything else (?)

BUILT_SOURCES =
CLEANFILES =
shairport_sync_LDADD =
noinst_LIBRARIES =

# See below for the flags for the test client program

shairport_sync_SOURCES = shairport.c bonjour_strings.c rtsp.c mdns.c common.c rtp.c player.c audio.c loudness.c activity_monitor.c utilities/debug.c utilities/network_utilities.c

if BUILD_FOR_DARWIN
  AM_CXXFLAGS = -I/usr/local/include -Wno-multichar -Wall -Wextra -Wno-deprecated-declarations -pthread -DSYSCONFDIR=\"$(sysconfdir)\"
  AM_CFLAGS = --include=utilities/debug.h -Wno-multichar -Wall -Wextra -Wno-deprecated-declarations -pthread -DSYSCONFDIR=\"$(sysconfdir)\"
else
if BUILD_FOR_FREEBSD
  AM_CXXFLAGS = -I/usr/local/include -Wno-multichar -Wall -Wextra -pthread -DSYSCONFDIR=\"$(sysconfdir)\"
  AM_CFLAGS = --include=utilities/debug.h -Wno-multichar -Wall -Wextra -pthread -DSYSCONFDIR=\"$(sysconfdir)\"
else
if BUILD_FOR_OPENBSD
  AM_CXXFLAGS = -I/usr/local/include -Wno-multichar -Wall -Wextra -Wno-clobbered -Wno-psabi -pthread -DSYSCONFDIR=\"$(sysconfdir)\"
  AM_CFLAGS = --include=utilities/debug.h -Wno-multichar -Wall -Wextra -pthread -DSYSCONFDIR=\"$(sysconfdir)\"
else
  AM_CXXFLAGS = -Wshadow -fno-common -Wno-multichar -Wall -Wextra -Wformat -Wformat=2 -Wno-clobbered -Wno-psabi -pthread -DSYSCONFDIR=\"$(sysconfdir)\"
  AM_CFLAGS = --include=utilities/debug.h -Wshadow -fno-common -Wno-multichar -Wall -Wextra -Wformat -Wformat=2 -Wno-clobbered -Wno-psabi -pthread -DSYSCONFDIR=\"$(sysconfdir)\"
endif
endif
endif

# include version information from git if available
if USE_GIT_VERSION
## Check if the git version information has changed and rebuild gitversion.h if so
.PHONY: gitversion-check
gitversion-check: 
	$(top_srcdir)/verify-gitversion

BUILT_SOURCES += gitversion-check
CLEANFILES += gitversion-stamp gitversion.h
endif

if USE_HAMMERTON
  shairport_sync_SOURCES += alac.c
endif

if USE_APPLE_ALAC
  shairport_sync_SOURCES += apple_alac.cpp
endif

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

if USE_EXTERNAL_MDNS
shairport_sync_SOURCES += mdns_external.c
endif

if USE_ALSA
shairport_sync_SOURCES += audio_alsa.c
endif

if USE_JACK
shairport_sync_SOURCES += audio_jack.c
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

if USE_SOUNDIO
shairport_sync_SOURCES += audio_soundio.c
endif

if USE_PULSEAUDIO
shairport_sync_SOURCES += audio_pa.c
endif

if USE_PIPEWIRE
shairport_sync_SOURCES += audio_pw.c
endif

if USE_CONVOLUTION
shairport_sync_SOURCES += FFTConvolver/AudioFFT.cpp FFTConvolver/FFTConvolver.cpp FFTConvolver/Utilities.cpp FFTConvolver/convolver.cpp FFTConvolver/ConvolverThreadPool.cpp
AM_CXXFLAGS += -std=c++11
endif

if USE_DNS_SD
shairport_sync_SOURCES += mdns_dns_sd.c
endif

if USE_METADATA_HUB
shairport_sync_SOURCES += metadata_hub.c
endif

if USE_MQTT
shairport_sync_SOURCES += mqtt.c
endif

if USE_DACP_CLIENT
shairport_sync_SOURCES += dacp.c
shairport_sync_LDADD += lib_tinyhttp.a
noinst_LIBRARIES += lib_tinyhttp.a
lib_tinyhttp_a_SOURCES =  tinyhttp/chunk.c tinyhttp/header.c tinyhttp/http.c
endif

if USE_AIRPLAY_2
shairport_sync_SOURCES += ap2_buffered_audio_processor.c ap2_event_receiver.c ap2_rc_event_receiver.c ptp-utilities.c utilities/buffered_read.c utilities/structured_buffer.c utilities/mod23.c plists/get_info_response.c
shairport_sync_LDADD += lib_pair_ap.a
lib_pair_ap_a_SOURCES = pair_ap/pair.c pair_ap/pair_fruit.c pair_ap/pair_homekit.c pair_ap/pair-tlv.c
noinst_LIBRARIES += lib_pair_ap.a

plists/get_info_response.h: plists/get_info_response.xml
	sh $(top_srcdir)/xml_plist_codegen.sh $(top_srcdir)/plists/get_info_response.xml $(abs_builddir)/plists
plists/get_info_response.c: plists/get_info_response.h
	touch plists/get_info_response.c

BUILT_SOURCES += plists/get_info_response.h plists/get_info_response.c
CLEANFILES +=plists/get_info_response.h plists/get_info_response.c

endif

if USE_DBUS
shairport_sync_LDADD += lib_dbus_interface.a
noinst_LIBRARIES += lib_dbus_interface.a
lib_dbus_interface_a_SOURCES = dbus-interface.c
shairport_sync_SOURCES += dbus-service.c
BUILT_SOURCES += dbus-interface.h dbus-interface.c
CLEANFILES += dbus-interface.h dbus-interface.c

dbus-interface.c:  org.gnome.ShairportSync.xml
	gdbus-codegen --interface-prefix org.gnome --generate-c-code dbus-interface $(top_srcdir)/org.gnome.ShairportSync.xml
dbus-interface.h: dbus-interface.c
	touch dbus-interface.h
endif

if USE_MPRIS
shairport_sync_LDADD += lib_mpris_interface.a
noinst_LIBRARIES += lib_mpris_interface.a
lib_mpris_interface_a_SOURCES = mpris-interface.c
shairport_sync_SOURCES += mpris-service.c
BUILT_SOURCES += mpris-interface.h mpris-interface.c
CLEANFILES += mpris-interface.h mpris-interface.c

mpris-interface.c:  org.mpris.MediaPlayer2.xml
	gdbus-codegen --interface-prefix org.mpris --generate-c-code mpris-interface $(top_srcdir)/org.mpris.MediaPlayer2.xml
mpris-interface.h: mpris-interface.c
	touch mpris-interface.h
endif

noinst_PROGRAMS =

if USE_DBUS_CLIENT
 #Make it, but don't install it anywhere
noinst_PROGRAMS += shairport-sync-dbus-test-client
shairport_sync_dbus_test_client_SOURCES = shairport-sync-dbus-test-client.c
shairport_sync_dbus_test_client_LDADD = lib_dbus_interface.a
endif

if USE_MPRIS_CLIENT
 #Make it, but don't install it anywhere
noinst_PROGRAMS += shairport-sync-mpris-test-client
shairport_sync_mpris_test_client_SOURCES = shairport-sync-mpris-test-client.c
shairport_sync_mpris_test_client_LDADD = lib_mpris_interface.a
endif

CONFIG_FILE_INSTALL_TARGET = config-file-install-local
$(CONFIG_FILE_INSTALL_TARGET): scripts/shairport-sync.conf
	install -d $(DESTDIR)$(sysconfdir)
	install -m 0644 $(top_srcdir)/scripts/shairport-sync.conf $(DESTDIR)$(sysconfdir)/shairport-sync.conf.sample
	[ -f $(DESTDIR)$(sysconfdir)/shairport-sync.conf ] || cp $(top_srcdir)/scripts/shairport-sync.conf $(DESTDIR)$(sysconfdir)/shairport-sync.conf

if BUILD_FOR_LINUX
DBUS_POLICY_DIR = /etc/dbus-1/system.d
else
DBUS_POLICY_DIR = $(sysconfdir)/dbus-1/system.d
endif

if USE_DBUS

if INSTALL_CYGWIN_STARTUP
DBUS_POLICY_FILE = scripts/shairport-sync-dbus-policy-cygwin.conf
else
DBUS_POLICY_FILE = scripts/shairport-sync-dbus-policy.conf
endif # INSTALL_CYGWIN_STARTUP

DBUS_POLICY_INSTALL_TARGET = dbus-policy-install-local

$(DBUS_POLICY_INSTALL_TARGET): $(DBUS_POLICY_FILE)
	install -d $(DESTDIR)$(DBUS_POLICY_DIR)
	install -m 0644 $(top_srcdir)/$(DBUS_POLICY_FILE) $(DESTDIR)$(DBUS_POLICY_DIR)/shairport-sync-dbus.conf

endif # USE_DBUS

if USE_MPRIS

if INSTALL_CYGWIN_STARTUP
MPRIS_POLICY_FILE = scripts/shairport-sync-mpris-policy-cygwin.conf
else
MPRIS_POLICY_FILE = scripts/shairport-sync-mpris-policy.conf
endif # INSTALL_CYGWIN_STARTUP

MPRIS_POLICY_INSTALL_TARGET = mpris-policy-install-local

$(MPRIS_POLICY_INSTALL_TARGET): $(MPRIS_POLICY_FILE)
	install -d $(DESTDIR)$(DBUS_POLICY_DIR)
	install -m 0644 $(top_srcdir)/$(MPRIS_POLICY_FILE) $(DESTDIR)$(DBUS_POLICY_DIR)/shairport-sync-mpris.conf

endif # USE_MPRIS

INSTALL_GROUP_TARGET = install-group-local

$(INSTALL_GROUP_TARGET):
	getent group shairport-sync &>/dev/null || groupadd -r shairport-sync &>/dev/null

INSTALL_USER_TARGET = install-user-local

$(INSTALL_USER_TARGET): $(INSTALL_GROUP_TARGET)
	getent passwd shairport-sync &> /dev/null || useradd -r -M -g shairport-sync -s /usr/sbin/nologin -G audio shairport-sync &>/dev/null

if INSTALL_SYSTEMV_STARTUP

INSTALL_SYSTEMV_TARGET = install-systemv-local

# watch out -- shairport-sync is created during the ./configure step and
# will be stored in a scripts folder in the _build_ folder
# which will be the source folder if you're not using a separate build folder

$(INSTALL_SYSTEMV_TARGET): scripts/shairport-sync $(INSTALL_USER_TARGET)
	install -d $(DESTDIR)$(sysconfdir)/init.d
	[ -e $(DESTDIR)$(sysconfdir)/init.d/shairport-sync ] || install -m 0755 scripts/shairport-sync $(DESTDIR)$(sysconfdir)/init.d

endif # INSTALL_SYSTEMV_STARTUP

if INSTALL_SYSTEMD_STARTUP

SYSTEMD_SERVICE = shairport-sync.service

INSTALL_SYSTEMD_TARGET = install-systemd-local

# watch out -- shairport-sync.service is created during the ./configure step and
# will be stored in a scripts folder in the _build_ folder
# which will be the source folder if you're not using a separate build folder

# Install a system service script, but don't replace an existing script
$(INSTALL_SYSTEMD_TARGET): scripts/$(SYSTEMD_SERVICE) $(INSTALL_USER_TARGET)
	install -d $(DESTDIR)$(systemdsystemunitdir)
	[ -e $(DESTDIR)$(systemdsystemunitdir)/shairport-sync.service ] || install -m 0644 scripts/$(SYSTEMD_SERVICE) $(DESTDIR)$(systemdsystemunitdir)/shairport-sync.service

endif # INSTALL_SYSTEMD_STARTUP

if INSTALL_FREEBSD_STARTUP

# Choose a uid and gid of 801 completely arbitrarity, except that it should be below 1000. FreeBSD doesn't seem to allow you to say "an ID in the range of..."
install-freebsd-user-local:
	pw showgroup shairport-sync > /dev/null 2>&1 || pw addgroup -n shairport-sync -g 801 > /dev/null 2>&1
	pw showuser shairport-sync > /dev/null 2>&1 || pw adduser -c "shairport-sync unprivileged user" -n shairport-sync -u 801 -s /usr/sbin/nologin -d /nonexistent > /dev/null 2>&1

INSTALL_FREEBSD_TARGET = install-freebsd-local

$(INSTALL_FREEBSD_TARGET): scripts/shairport-sync.freebsd install-freebsd-user-local
	install -d -o shairport-sync -g shairport-sync $(DESTDIR)/var/run/shairport-sync
	install -d $(DESTDIR)/usr/local/etc/rc.d/
	install -m 0555 $(top_srcdir)/scripts/shairport-sync.freebsd $(DESTDIR)/usr/local/etc/rc.d/shairport_sync

endif # INSTALL_FREEBSD_STARTUP

if INSTALL_CYGWIN_STARTUP

INSTALL_CYGWIN_TARGET = install-cygwin-local

$(INSTALL_CYGWIN_TARGET): scripts/shairport-sync-config
	install -d $(DESTDIR)/usr/local/bin
	[ -e $(DESTDIR)/usr/local/bin/shairport-sync-config ] || install -m 0755 $(top_srcdir)/scripts/shairport-sync-config $(DESTDIR)/usr/local/bin/

endif # INSTALL_CYGWIN_STARTUP

install-config-files: $(CONFIG_FILE_INSTALL_TARGET) \
                   $(DBUS_POLICY_INSTALL_TARGET) \
                   $(MPRIS_POLICY_INSTALL_TARGET) \
                   $(INSTALL_SYSTEMV_TARGET) \
                   $(INSTALL_SYSTEMD_TARGET) \
                   $(INSTALL_SYSTEMD_USER_TARGET) \
                   $(INSTALL_FREEBSD_TARGET) \
                   $(INSTALL_CYGWIN_TARGET)

if INSTALL_CONFIG_FILES

install-exec-hook: install-config-files

endif
