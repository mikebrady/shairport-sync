project(
  'shairport-sync',
  ['c', 'cpp'],
  version : '3.3.5',
)

conf_data = configuration_data()
conf_data.set_quoted('PACKAGE_VERSION', meson.project_version())

conf_data.set_quoted('SYSCONFDIR', get_option('sysconfdir'))

with_piddir = get_option('piddir')
if with_piddir != ''
  conf_data.set_quoted('PIDDIR', with_piddir)
endif

add_global_arguments(['-D_GNU_SOURCE', '-D__USE_XOPEN'], language : 'c')

cc = meson.get_compiler('c')

threads = dependency('threads')
math = cc.find_library('m', required : true)
config = cc.find_library('config', required : true)
popt = cc.find_library('popt', required : true)

deps = [threads, math, config, popt]

files_shairport_sync = files(
  'activity_monitor.c',
  'activity_monitor.h',
  'alac.c',
  'alac.h',
  'audio.c',
  'audio.h',
  'common.c',
  'common.h',
  'loudness.c',
  'loudness.h',
  'mdns.c',
  'mdns.h',
  'player.c',
  'player.h',
  'rtp.c',
  'rtp.h',
  'rtsp.c',
  'rtsp.h',
  'shairport.c',
)

with_os = get_option('os')
with_apple_alac = get_option('apple_alac')
with_libdaemon = get_option('libdaemon')
with_ssl = get_option('ssl')
with_soxr = get_option('soxr')
with_avahi = get_option('avahi')
with_tinysvcmdns = get_option('tinysvcmdns')
with_external_mdns = get_option('external_mdns')
with_alsa = get_option('alsa')
with_jack = get_option('jack')
with_sndio = get_option('sndio')
with_stdout = get_option('stdout')
with_pipe = get_option('pipe')
with_dummy = get_option('dummy')
with_ao = get_option('ao')
with_soundio = get_option('soundio')
with_pa = get_option('pa')
with_convolution = get_option('convolution')
with_dns_sd = get_option('dns_sd')
with_mqtt = get_option('mqtt')
with_dbus = get_option('dbus')
with_mpris = get_option('mpris')
with_metadata = get_option('metadata')

if with_os == 'linux' or with_os == freebsd
  rt = cc.find_library('rt', required : true)
  deps += rt
endif

if with_os == 'openbsd'
  c = cc.find_library('c', required : true)
  deps += c
endif

if with_apple_alac
  alac = dependency('alac')
  deps += alac
  files_shairport_sync += 'apple_alac.cpp'
  conf_data.set('CONFIG_APPLE_ALAC', true)
endif

if with_libdaemon
  libdaemon = cc.find_library('daemon', required : true)
  deps += libdaemon
  conf_data.set('CONFIG_LIBDAEMON', true)
endif

if with_ssl == 'openssl'
  openssl = dependency('openssl')
  deps += openssl
  conf_data.set('CONFIG_OPENSSL', true)
elif with_ssl == 'mbedtls'
  mbedtls = dependency('mdedtls')
  deps += mbedtls
  conf_data.set('CONFIG_MBEDTLS', true)
elif with_ssl == 'polarssl'
  polarssl = dependency('polarssl')
  deps += polarssl
  conf_data.set('CONFIG_POLARSSL', true)
endif

if with_soxr
  soxr = cc.find_library('soxr')
  deps += soxr
  conf_data.set('CONFIG_SOXR', true)
endif

if with_metadata
  conf_data.set('CONFIG_METADATA', true)
endif

if with_avahi
  avahi_client = dependency('avahi-client')
  deps += avahi_client
  files_shairport_sync += 'mdns_avahi.c'
  conf_data.set('CONFIG_AVAHI', true)
endif

if with_tinysvcmdns
  files_shairport_sync += ['mdns_tinysvcmdns.c', 'tinysvcmdns.c']
  conf_data.set('CONFIG_TINYSVCMDNS', true)
endif

if with_external_mdns
  files_shairport_sync += 'mdns_external.c'
  conf_data.set('CONFIG_EXTERNAL_MDNS', true)
endif

if with_alsa
  alsa = dependency('alsa')
  deps += alsa
  files_shairport_sync += 'audio_alsa.c'
  conf_data.set('CONFIG_ALSA', true)
endif

if with_jack
  jack = dependency('jack')
  deps += jack
  files_shairport_sync += 'audio_jack.c'
  conf_data.set('CONFIG_JACK', true)
endif

if with_sndio
  sndio = dependency('sndio')
  deps += sndio
  files_shairport_sync += 'audio_sndio.c'
  conf_data.set('CONFIG_SNDIO', true)
endif

if with_stdout
  files_shairport_sync += 'audio_stdout.c'
  conf_data.set('CONFIG_STDOUT', true)
endif

if with_pipe
  files_shairport_sync += 'audio_pipe.c'
  conf_data.set('CONFIG_PIPE', true)
endif

if with_dummy
  files_shairport_sync += 'audio_dummy.c'
  conf_data.set('CONFIG_DUMMY', true)
endif

if with_ao
  ao = dependency('ao')
  deps += ao
  files_shairport_sync += 'audio_ao.c'
  conf_data.set('CONFIG_AO', true)
endif

if with_soundio
  soundio = dependency('soundio')
  deps += soundio
  files_shairport_sync += 'audio_soundio.c'
  conf_data.set('CONFIG_SOUNDIO', true)
endif

if with_pa
  pa = dependency('libpulse')
  deps += pa
  files_shairport_sync += 'audio_pa.c'
  conf_data.set('CONFIG_PA', true)
endif

if with_convolution
  sndfile = dependency('sndfile')
  deps += sndfile
  files_shairport_sync += ['FFTConvolver/AudioFFT.cpp', 'FFTConvolver/FFTConvolver.cpp', 'FFTConvolver/Utilities.cpp', 'FFTConvolver/convolver.cpp']
  # c++11
  conf_data.set('CONFIG_CONVOLUTION', true)
endif

if with_dns_sd
  dns_sd = dependency('dns_sd')
  deps += dns_sd
  files_shairport_sync += 'mdns_dns_sd.c'
  conf_data.set('CONFIG_DNS_SD', true)
endif

if with_mqtt
  mqtt = dependency('mosquitto')
  deps += mqtt
  files_shairport_sync += 'mqtt.c'
  conf_data.set('CONFIG_MQTT', true)
endif

static_libs = []

if with_dbus
  glib = dependency('glib-2.0')
  gio_unix = dependency('gio-unix-2.0')
  deps += glib

  prog_gdbus_codegen = find_program('gdbus-codegen')

  dbus_interface_ch = custom_target(
      'dbus-interface.[ch]',
      input : 'org.gnome.ShairportSync.xml',
      output : ['dbus-interface.c', 'dbus-interface.h'],
      command : [prog_gdbus_codegen, '--interface-prefix', 'org.gnome', '--generate-c-code', 'dbus-interface', '@INPUT@'],
  )

  files_dbus = files('dbus-service.c', 'dbus-service.h')

  dbus = static_library('dbus', [dbus_interface_ch], files_dbus, dependencies : [glib, gio_unix])
  static_libs += dbus

  executable('shairport-sync-dbus-test-client', 'shairport-sync-dbus-test-client.c', dependencies : deps, link_with : dbus)
  conf_data.set('CONFIG_DBUS_INTERFACE', true)
endif

if with_mpris
  glib = dependency('glib-2.0')
  gio_unix = dependency('gio-unix-2.0')
  deps += glib

  prog_gdbus_codegen = find_program('gdbus-codegen')

  mpris_interface_ch = custom_target(
      'mpris-interface.[ch]',
      input : 'org.mpris.MediaPlayer2.xml',
      output : ['mpris-interface.c', 'mpris-interface.h'],
      command : [prog_gdbus_codegen, '--interface-prefix', 'org.mpris', '--generate-c-code', 'mpris-interface', '@INTPUT@'],
  )

  files_mpris = files('mpris-service.c', 'mpris-service.h')

  mpris = static_library('mpris', [mpris_interface_ch], files_mpris, dependencies : [glib, gio_unix])
  static_libs += mpris

  executable('shairport-sync-mpris-test-client', 'shairport-sync-mpris-test-client.c', dependencies : deps, link_with : mpris)
  conf_data.set('CONFIG_MPRIS_INTERFACE', true)
endif

if with_mqtt and not with_avahi
  message('MQTT needs Avahi to allow remote control functionality. Only Metadata publishing will be supported')
endif

if with_mpris or with_dbus or with_mqtt
  files_shairport_sync += 'metadata_hub.c'
  files_shairport_sync += ['dacp.c', 'tinyhttp/chunk.c', 'tinyhttp/header.c', 'tinyhttp/http.c']

  conf_data.set('CONFIG_METADATA_HUB', true)
  conf_data.set('CONFIG_DACP_CLIENT', true)
  conf_data.set('CONFIG_METADATA', true)
endif

configure_file(
    output : 'config.h',
    configuration : conf_data
)

executable('shairport-sync', sources: files_shairport_sync, dependencies : deps, link_with : static_libs, install : true)

if get_option('install_config_files')

  install_data(sources : 'scripts/shairport-sync.conf', install_dir : get_option('sysconfdir'))

  if with_dbus
    install_data(sources : 'scripts/shairport-sync-dbus-policy.conf', rename : 'shairport-sync-dbus.conf', install_dir : get_option('sysconfdir') + '/dbus-1/system.d')
  endif

  if with_mpris
    install_data(sources : 'scripts/shairport-sync-mpris-policy.conf', rename : 'shairport-sync-mpris.conf', install_dir : get_option('sysconfdir') + '/dbus-1/system.d')
  endif
endif

if get_option('systemd')
  systemd_system_unit_dir = get_option('systemdsystemunitdir')
  if systemd_system_unit_dir == 'auto'
    systemd_system_unit_dir = 'lib/systemd/system'
  endif

  configure_file(
    input: 'scripts/shairport-sync.service.in',
    output: 'shairport-sync.service',
    configuration: { 'prefix' : get_option('prefix'), },
    install_dir: systemd_system_unit_dir,
  )
endif

