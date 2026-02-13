# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.50])
AC_INIT([shairport-sync], [5.0rc0], [4265913+mikebrady@users.noreply.github.com])
: ${CFLAGS="-O3"}
: ${CXXFLAGS="-O3"}
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_SRCDIR([shairport.c])
AC_CONFIG_HEADERS([config.h])
AC_PROG_RANLIB
AC_SUBST([HOME], [$HOME])

# Derived from the Avahi configure.ac file
# Specifying the OS type, defaulting to linux.
#
AC_ARG_WITH(os, AS_HELP_STRING([--with-os=OSType],[Specify the distribution to target: One of linux, freebsd, openbsd or darwin]))
if test "z$with_os" = "z"; then
  with_os="linux"
fi
with_os=`echo ${with_os} | tr '[[:upper:]]' '[[:lower:]]' `
case "x${with_os}" in
xlinux|xfreebsd|xopenbsd|xdarwin)
  ;;
*)
  AC_MSG_ERROR([--with-os=<arg> argument must be one of linux, freebsd, openbsd or darwin])
  ;;
esac

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_CHECK_TOOL(AR, ar, :)
AC_PROG_INSTALL

# see if we have git -- not fatal if we don't
AC_CHECK_PROGS([GIT], [git])
if test -n "$GIT" && test -e "$srcdir/.git/index" ; then
  AC_DEFINE([CONFIG_USE_GIT_VERSION_STRING], 1, [Use the version string produced by running 'git describe --dirty'.])
fi
AM_CONDITIONAL([USE_GIT_VERSION], [test -n "$GIT" && test -e "$srcdir/.git/index" ])

# see if we have xmltoman -- not fatal if we don't
AC_CHECK_PROGS([XMLTOMAN], [xmltoman])
AM_CONDITIONAL([USE_XMLTOMAN], [ test -n "$XMLTOMAN" ])

PKG_PROG_PKG_CONFIG([0.9.0])

# Checks for libraries.
if test "x${with_os}" = xlinux -o "x${with_os}" = xfreebsd ; then
 AC_CHECK_LIB([rt],[clock_gettime], , AC_MSG_ERROR(librt needed))
fi

if test "x${with_os}" = xopenbsd ; then
 AC_CHECK_LIB([c],[clock_gettime], , AC_MSG_ERROR(libc needed))
fi

# Pass the conditionals to automake
AM_CONDITIONAL([BUILD_FOR_LINUX], [test "x${with_os}" = xlinux ])
AM_CONDITIONAL([BUILD_FOR_FREEBSD], [test "x${with_os}" = xfreebsd ])
AM_CONDITIONAL([BUILD_FOR_OPENBSD], [test "x${with_os}" = xopenbsd ])
AM_CONDITIONAL([BUILD_FOR_DARWIN], [test "x${with_os}" = xdarwin ])

##### Some build systems are not fully using pkg-config, so we can use the flag ${with_pkg_config} on a case-by-case basis
##### to control how to deal with them
##### Note -- this flag is sometimes ignored, especially for newer packages

AC_ARG_WITH([pkg_config],[AS_HELP_STRING([--with-pkg-config],[use pkg-config to find libraries])],[],[with_pkg_config=yes])

##### The following check for the pthreads library doesn't put the compiler into the correct pthread mode
##### so we add the -pthread compilation flag in AMFLAGS in the Makefile.am as well.

AC_CHECK_LIB([pthread],[pthread_create], , AC_MSG_ERROR(pthread library needed))
AC_CHECK_LIB([m],[exp], , AC_MSG_ERROR(maths library needed))

if  test "x${with_pkg_config}" = xyes ; then
  PKG_CHECK_MODULES(
      [popt], [popt],
      [CFLAGS="${popt_CFLAGS} ${CFLAGS}" LIBS="${popt_LIBS} ${LIBS}"], AC_MSG_ERROR(libpopt needed))
else
  AC_CHECK_LIB([popt],[poptGetContext], , AC_MSG_ERROR(libpopt needed))
fi

AC_ARG_WITH([dummy],[AS_HELP_STRING([--with-dummy],[include the dummy audio back end])])
if test "x$with_dummy" = "xyes" ; then
  AC_DEFINE([CONFIG_DUMMY], 1, [Include a fake audio backend.])
fi
AM_CONDITIONAL([USE_DUMMY], [test "x$with_dummy" = "xyes" ])

AC_ARG_WITH([stdout],[AS_HELP_STRING([--with-stdout],[include the stdout audio back end])])
if test "x$with_stdout" = "xyes" ; then
  AC_DEFINE([CONFIG_STDOUT], 1, [Include an audio backend to output to standard output (stdout).])
fi
AM_CONDITIONAL([USE_STDOUT], [test "x$with_stdout" = "xyes"])

AC_ARG_WITH([pipe],[AS_HELP_STRING([--with-pipe],[build the unix pipe backend (not the PipeWire backend!) into Shairport Sync])])
if test "x$with_pipe" = "xyes" ; then
  AC_DEFINE([CONFIG_PIPE], 1, [Include an audio backend to output to a unix pipe.])
fi
AM_CONDITIONAL([USE_PIPE], [test "x$with_pipe" = "xyes" ])

# Check to see if we should include the System V initscript

AC_ARG_WITH([systemv-startup],[AS_HELP_STRING([--with-systemv-startup],[install a System V startup script during a make install])])
AM_CONDITIONAL([INSTALL_SYSTEMV_STARTUP], [test "x$with_systemv_startup" = "xyes"])

# Check to see if we should include the systemd stuff to define it as a service

AC_ARG_WITH([systemd-startup],[AS_HELP_STRING([--with-systemd-startup],[install systemd startup scripts to start Shairport Sync after system startup or after user login])])
AM_CONDITIONAL([INSTALL_SYSTEMD_STARTUP], [test "x$with_systemd_startup" = "xyes"])

AC_ARG_WITH([freebsd-startup],[AS_HELP_STRING([--with-freebsd-startup],[install a FreeBSD startup script to start Shairport Sync after system startup])])
AM_CONDITIONAL([INSTALL_FREEBSD_STARTUP], [test "x$with_freebsd_startup" = "xyes"])

AC_ARG_WITH([cygwin-startup],[AS_HELP_STRING([--with-cygwin-startup],[install a CYGWIN startup script to start Shairport Sync after startup])])
AM_CONDITIONAL([INSTALL_CYGWIN_STARTUP], [test "x$with_cygwin_startup" = "xyes"])

AC_ARG_WITH([external-mdns],[AS_HELP_STRING([--with-external-mdns],[support the use of 'avahi-publish-service' or 'mDNSPublish' to advertise the service on Bonjour/ZeroConf])])
if  test "x$with_external_mdns" = xyes ; then
	AC_MSG_RESULT(include external mdns support)
	AC_DEFINE([CONFIG_EXTERNAL_MDNS], 1, [Use 'avahi-publish-service' or 'mDNSPublish' to advertise the service on Bonjour/ZeroConf])
fi
AM_CONDITIONAL([USE_EXTERNAL_MDNS], [test "x$with_external_mdns" = "xyes" ])

# Check if we want to create user/group (default to yes):

AC_ARG_WITH([create-user-group],[AS_HELP_STRING([--with-create-user-group],[create user/group during a make install])], ,[with_with_create_user_group=yes])
AM_CONDITIONAL([INSTALL_CREATE_USER_GROUP], [test "x$with_with_create_user_group" = "xyes"])

# Add the libconfig package
if  test "x${with_pkg_config}" = xyes ; then
  PKG_CHECK_MODULES(
      [libconfig], [libconfig],
      [CFLAGS="${libconfig_CFLAGS} ${CFLAGS}" LIBS="${libconfig_LIBS} ${LIBS}"], AC_MSG_ERROR([libconfig library needed]))
else
  AC_CHECK_LIB([config],[config_init], , AC_MSG_ERROR([libconfig library needed]))
fi

AC_ARG_WITH([configfiles],[AS_HELP_STRING([--with-configfiles],[install configuration files during a make install])], ,[with_configfiles=yes])
AM_CONDITIONAL([INSTALL_CONFIG_FILES], [test "x$with_configfiles" = "xyes"])

# Look for piddir flag
AC_ARG_WITH(piddir, [AS_HELP_STRING([--with-piddir=<pathname>],[Specify a pathname to a directory in which to write the PID file.])])
if  test "x${with_piddir}" != "x" ; then
	AC_MSG_CHECKING(--with-piddir argument)
	if test "x${with_piddir}" = x -o "x${with_piddir}" = xyes ; then
		AC_MSG_RESULT(not found)
		AC_MSG_ERROR(when you use the --with-piddir directive you must specify the pathname of the directory into which the PID file will be written)
	fi
	AC_MSG_RESULT(${with_piddir})
	AC_SUBST(CUSTOM_PID_DIR,["${with_piddir}"])
	AC_DEFINE([DEFINED_CUSTOM_PID_DIR],1,[Hook up special proc to libdaemon to point to this directory])
fi
AM_CONDITIONAL([USE_CUSTOMPIDDIR], [ test "x${with_piddir}" != "x" ])

# Look for libdaemon
AC_ARG_WITH(libdaemon,[AS_HELP_STRING([--with-libdaemon],[include support for daemonising in non-systemd systems])])
if test "x$with_libdaemon" = "xyes"; then
	AC_DEFINE([CONFIG_LIBDAEMON], 1, [Include libdaemon for daemonising in non-systemd systems])
	if  test "x${with_pkg_config}" = xyes ; then
		PKG_CHECK_MODULES(
				[DAEMON], [libdaemon],
				[LIBS="${DAEMON_LIBS} ${LIBS}"],
				[AC_MSG_ERROR(the libdaemon library has been selected but is missing -- libdaemon-dev suggested!)])
	else
		AC_CHECK_LIB([daemon],[daemon_fork], , AC_MSG_ERROR(the libdaemon library has been selected but is missing -- libdaemon-dev suggested!))
	fi
fi
AM_CONDITIONAL([USE_LIBDAEMON], [test "x$with_libdaemon" = "xyes"])

# Check --with-ssl=argument
AC_ARG_WITH(ssl, [AS_HELP_STRING([--with-ssl=<argument>],[choose --with-ssl=openssl, --with-ssl=mbedtls or --with-ssl=polarssl (deprecated) for encryption services  ])])

if test "x${with_ssl}" = xopenssl ; then
	AC_DEFINE([CONFIG_OPENSSL], 1, [Use the OpenSSL libraries for encryption and encoding and decoding])
	if  test "x${with_pkg_config}" = xyes ; then
	  PKG_CHECK_MODULES(
	      [SSL], [libssl,libcrypto],
	      [CFLAGS="${SSL_CFLAGS} ${CFLAGS}" LIBS="${SSL_LIBS} ${LIBS}"])
	else
	  AC_CHECK_LIB([crypto], [main], , AC_MSG_ERROR(libcrypto selected but the library cannot be found!))
	  AC_CHECK_LIB([ssl], [main], , AC_MSG_ERROR(libssl selected but the library cannot be found!))
	fi
elif test "x${with_ssl}" = xmbedtls ; then
	AC_DEFINE([CONFIG_MBEDTLS], 1, [Use the mbed TLS libraries for encryption and encoding and decoding])
	AC_CHECK_LIB([mbedtls],[mbedtls_ssl_init],,
		[AC_MSG_ERROR([mbed tls support requires the mbedtls library -- libmbedtls-dev suggested],1)])
	AC_CHECK_LIB([mbedcrypto], [mbedtls_entropy_func],,
	            [AC_MSG_ERROR([mbed tls support requires the mbedcrypto library -- libmbedcrypto0 suggested],1)])
	AC_CHECK_LIB([mbedx509], [mbedtls_pk_init],,
	            [AC_MSG_ERROR([mbed tls support requires the mbedx509 library -- libmbedx509-0 suggested],1)])
elif test "x${with_ssl}" = xpolarssl ; then
	AC_DEFINE([CONFIG_POLARSSL], 1, [Use the PolarSSL libraries for encryption and encoding and decoding])
	AC_CHECK_LIB([polarssl],[ssl_init], , AC_MSG_ERROR(PolarSSL is selected but the library libpolarssl-dev does not seem to be installed and is deprecated. Consider selecting mbedtls instead using --with-ssl=mbedtls.))
else
	AC_MSG_ERROR(specify one of --with-ssl=openssl or --with-ssl=mbedtls or --with-ssl=polarssl)
fi

# Look for soxr flag

AC_ARG_WITH(soxr, [AS_HELP_STRING([--with-soxr],[choose libsoxr for high-quality interpolation])])
if test "x$with_soxr" = "xyes" ; then
  AC_DEFINE([CONFIG_SOXR], 1, [Include support for using the SoX Resampler library for interpolation])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
        [soxr], [soxr],
        [LIBS="${soxr_LIBS} ${LIBS}"],
        [AC_MSG_ERROR(soxr support requires the libsoxr library -- libsoxr-dev suggested!)])
  else
    AC_CHECK_LIB([avutil],[av_get_cpu_flags])
    if  test "x${ac_cv_lib_avutil_av_get_cpu_flags}" = xyes ; then
      # soxr may link against libavutil, depending on the architecture, but for the sake of simplicity link with it if it is found
      AC_CHECK_LIB([soxr],[soxr_create], , AC_MSG_ERROR(soxr support requires the libsoxr library -- libsoxr-dev suggested!), [-lavutil])
    else
      AC_CHECK_LIB([soxr],[soxr_create], , AC_MSG_ERROR(soxr support requires the libsoxr library -- libsoxr-dev suggested!))
    fi
  fi
fi

# Look for metadata flag and resolve it further down the script
AC_ARG_WITH(metadata,[AS_HELP_STRING([--with-metadata],[include support for a metadata feed])])

# What follows is a bit messy, because if the relevant library is requested, a compiler flag is defined, a file is included in the compilation
# and the relevant link files are added.

# Look for avahi flag
AC_ARG_WITH(avahi, [AS_HELP_STRING([--with-avahi],[choose Avahi-based mDNS support])])
if test "x$with_avahi" = "xyes" ; then
  AC_DEFINE([CONFIG_AVAHI], 1, [Include Avahi-based mDNS support.])
  AC_CHECK_LIB([avahi-client], [avahi_client_new], , AC_MSG_ERROR(Avahi support requires the avahi-client library!))
  AC_CHECK_LIB([avahi-common],[avahi_strerror], , AC_MSG_ERROR(Avahi support requires the avahi-common library!))
  systemd_after_args="${systemd_after_args} avahi-daemon.service"
  systemd_requires_args="${systemd_requires_args} avahi-daemon.service"
fi
AM_CONDITIONAL([USE_AVAHI], [test "x$with_avahi" = "xyes"])

# Look for tinysvcmdns flag
AC_ARG_WITH(tinysvcmdns, [AS_HELP_STRING([--with-tinysvcmdns],[choose tinysvcmdns-based mDNS support])])
if test "x$with_tinysvcmdns" = "xyes" ; then
  AC_DEFINE([CONFIG_TINYSVCMDNS], 1, [Include TinySVCmDNS-based mDNS support])
fi
AM_CONDITIONAL([USE_TINYSVCMDNS], [test "x$with_tinysvcmdns" = "xyes"])

# Look for ALSA flag
AC_ARG_WITH(alsa, [AS_HELP_STRING([--with-alsa],[choose ALSA API support (GNU/Linux only)])])
if test "x$with_alsa" = "xyes" ; then
  AC_DEFINE([CONFIG_ALSA], 1, [Needed by the compiler.])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
      [alsa], [alsa],
      [LIBS="${alsa_LIBS} ${LIBS}"])
  else
    AC_CHECK_LIB([asound], [snd_pcm_open], , AC_MSG_ERROR(ALSA support requires the asound library!))
  fi
fi
AM_CONDITIONAL([USE_ALSA], [test "x$with_alsa" = "xyes"])

# Look for jack flag
AC_ARG_WITH(jack, [AS_HELP_STRING([--with-jack],[include a Jack Audio Connection Kit (jack) backend])])
if test "x$with_jack" = "xyes" ; then
  AC_DEFINE([CONFIG_JACK], 1, [Include a Jack Audio Connection Kit (jack) audio backend])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
      [jack], [jack],
      [LIBS="${jack_LIBS} ${LIBS}"],
      [AC_MSG_ERROR(Jack Audio Connection Kit support requires the jack library -- libjack-dev suggested!)])
  else
    AC_CHECK_LIB([jack], [jack_client_open], , AC_MSG_ERROR(Jack Audio Connection Kit support requires the jack library -- libjack-dev suggested!))
  fi
fi
AM_CONDITIONAL([USE_JACK], [test "x$with_jack" = "xyes"])

# Look for SNDIO flag
AC_ARG_WITH(sndio, [AS_HELP_STRING([--with-sndio],[choose SNDIO API support])])
if test "x$with_sndio" = "xyes" ; then
  AC_DEFINE([CONFIG_SNDIO], 1, [Include a sndio-compatible audio backend.])
  AC_CHECK_LIB([sndio], [sio_open], , AC_MSG_ERROR(SNDIO support requires the sndio library -- sndio suggested for FreeBSD and libsndio-dev for Debian))
fi
AM_CONDITIONAL([USE_SNDIO], [test "x$with_sndio" = "xyes"])

# Look for AO flag
AC_ARG_WITH(ao, [AS_HELP_STRING([--with-ao],[choose AO (Audio Output?) API support. N.B. no synchronisation -- so underflow or overflow is inevitable!])])
if test "x$with_ao" = "xyes" ; then
  AC_DEFINE([CONFIG_AO], 1, [Include an AO-compatible  audio backend.])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
        [AO], [ao > 1.0.0],
        [CFLAGS="${AO_CFLAGS} ${CFLAGS}" LIBS="${AO_LIBS} ${LIBS}"], [AC_MSG_ERROR(AO support requires the ao library -- libao-dev suggested)])
  else
    AC_CHECK_LIB([ao], [ao_initialize], , AC_MSG_ERROR(AO support requires the ao library -- libao-dev suggested))
  fi
fi
AM_CONDITIONAL([USE_AO], [test "x$with_ao" = "xyes"])

# Look for Soundio flag
AC_ARG_WITH(soundio, [AS_HELP_STRING([--with-soundio],[choose soundio API support.])])
if test "x$with_soundio" = "xyes" ; then
  AC_DEFINE([CONFIG_SOUNDIO], 1, [Include the SoundIO (libsoundio) backend.])
  AC_CHECK_LIB([soundio], [soundio_create], , AC_MSG_ERROR(soundio support requires the libsoundio library -- libsoundio-dev suggested (note: the soundio backend is deprecated and will be removed in a future update)))
  AC_MSG_WARN([The soundio (libsoundio) backend is deprecated and will be removed in a future update.])
fi
AM_CONDITIONAL([USE_SOUNDIO], [test "x$with_soundio" = "xyes"])

# Look for pulseaudio flag
AC_ARG_WITH(pulseaudio, [AS_HELP_STRING([--with-pulseaudio],[build the PulseAudio backend into Shairport Sync])])
if test "x$with_pulseaudio" = "xyes" ; then
  AC_DEFINE([CONFIG_PULSEAUDIO], 1, [Include PulseAudio support.])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
      [PULSEAUDIO], [libpulse >= 0.9.2],
      [LIBS="${PULSEAUDIO_LIBS} ${LIBS}"],[AC_MSG_ERROR(PulseAudio support requires the libpulse library -- libpulse-dev suggested!)])
  else
    AC_CHECK_LIB([pulse-simple], [pa_simple_new], , AC_MSG_ERROR(PulseAudio support requires the libpulse library -- libpulse-dev suggested!))
    AC_CHECK_LIB([pulse], [pa_stream_peek], , AC_MSG_ERROR(PulseAudio support requires the libpulse library -- libpulse-dev suggested!))
  fi
fi
AM_CONDITIONAL([USE_PULSEAUDIO], [test "x$with_pulseaudio" = "xyes"])

# Look for pipewire flag
AC_ARG_WITH(pipewire, [AS_HELP_STRING([--with-pipewire],[build the Pipewire backend into Shairport Sync.])])
if test "x$with_pipewire" = "xyes" ; then
  AC_DEFINE([CONFIG_PIPEWIRE], 1, [Include Pipewire support.])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
      PIPEWIRE, libpipewire-0.3 >= 1.0.5,
      [CFLAGS="${PIPEWIRE_CFLAGS} ${CFLAGS} -Wno-missing-field-initializers" LIBS="${PIPEWIRE_LIBS} ${LIBS}"],
      [AC_MSG_ERROR(Pipewire support requires libpipewire-0.3 version 1.0.5 or later -- libpipewire-0.3-dev suggested!)])
  else
    AC_CHECK_LIB([pipewire], [pw_stream_queue_buffer], , AC_MSG_ERROR(Pipewire support requires libpipewire-0.3 version 1.1.0 or later -- libpipewire-0.3-dev suggested!))
  fi
fi
AM_CONDITIONAL([USE_PIPEWIRE], [test "x$with_pipewire" = "xyes"])

# Look for Convolution flag
AC_ARG_WITH(convolution, [AS_HELP_STRING([--with-convolution],[choose audio DSP convolution support])])
if test "x$with_convolution" = "xyes" ; then
  AC_DEFINE([CONFIG_CONVOLUTION], 1, [Include audio DSP convolution support.])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
      [sndfile], [sndfile],
      [CFLAGS="${sndfile_CFLAGS} ${CFLAGS}"
      LIBS="${sndfile_LIBS} ${LIBS}"], AC_MSG_ERROR(Convolution support requires the sndfile library -- libsndfile1-dev suggested!))
  else
    AC_CHECK_LIB([sndfile], [sf_open], , AC_MSG_ERROR(Convolution support requires the sndfile library -- libsndfile1-dev suggested!))
  fi
  
fi
AM_CONDITIONAL([USE_CONVOLUTION], [test "x$with_convolution" = "xyes"])

# Look for dns_sd flag
AC_ARG_WITH(dns_sd, [AS_HELP_STRING([--with-dns_sd],[choose dns_sd mDNS support])])
if test "x$with_dns_sd" = "xyes" ; then
  AC_DEFINE([CONFIG_DNS_SD], 1, [Include dns_sd mDNS support.])
  AC_SEARCH_LIBS([DNSServiceRefDeallocate], [dns_sd], , [AC_MSG_ERROR(dns_sd support requires the dns_sd library. Originally in Apple's mDNSResponder but also in libavahi-compat-libdnssd-dev.)])
fi
AM_CONDITIONAL([USE_DNS_SD], [test "x$with_dns_sd" = "xyes"])

# Look for dbus flag
AC_ARG_WITH(dbus-interface, [AS_HELP_STRING([--with-dbus-interface],[include support for the native Shairport Sync D-Bus interface])])
if test "x$with_dbus_interface" = "xyes" ; then
  AC_DEFINE([CONFIG_DBUS_INTERFACE], 1, [Support the native Shairport Sync D-Bus interface])
  # remember to include glib, below
fi
AM_CONDITIONAL([USE_DBUS], [test "x$with_dbus_interface" = "xyes"])

# Look for dbus test client flag
AC_ARG_WITH(dbus-test-client, [AS_HELP_STRING([--with-dbus-test-client],[compile a D-Bus test client application])])
# remember to include glib, below
AM_CONDITIONAL([USE_DBUS_CLIENT], [test "x$with_dbus_test_client" = "xyes"])

# Look for mpris flag
AC_ARG_WITH(mpris-interface, [AS_HELP_STRING([--with-mpris-interface],[include support for a D-Bus interface conforming to the MPRIS standard])])
if test "x$with_mpris_interface" = "xyes" ; then
  AC_DEFINE([CONFIG_MPRIS_INTERFACE], 1, [Support the MPRIS standard])
  # remember to include glib, below
fi
AM_CONDITIONAL([USE_MPRIS], [test "x$with_mpris_interface" = "xyes"])

# Look for mpris test client flag
AC_ARG_WITH(mpris-test-client, [AS_HELP_STRING([--with-mpris-test-client],[compile an MPRIS test client application])])
# remember to include glib, below
AM_CONDITIONAL([USE_MPRIS_CLIENT], [test "x$with_mpris_test_client" = "xyes"])

if test "x$with_mpris_test_client" = "xyes" || test "x$with_dbus_test_client" = "xyes" || test "x$with_mpris_interface" = "xyes" || test "x$with_dbus_interface" = "xyes" ; then
  PKG_CHECK_MODULES([glib], [gio-unix-2.0 >= 2.30.0],[CFLAGS="${glib_CFLAGS} ${CFLAGS}" LIBS="${glib_LIBS} ${LIBS}"],[AC_MSG_ERROR(the glib 2.0 library is required -- libglib2.0-dev suggested!)])
fi

# Look for mqtt flag
AC_ARG_WITH(mqtt-client, [AS_HELP_STRING([--with-mqtt-client],[include a client for MQTT -- the Message Queuing Telemetry Transport protocol])])
if test "x$with_mqtt_client" = "xyes" ; then
  AC_DEFINE([CONFIG_MQTT], 1, [Include a client for MQTT, the Message Queuing Telemetry Transport protocol])
  AC_CHECK_LIB([mosquitto], [mosquitto_lib_init], , AC_MSG_ERROR(MQTT support requires the mosquitto library -- libmosquitto-dev suggested!))
  systemd_after_args="${systemd_after_args} mosquitto.service"
  systemd_requires_args="${systemd_requires_args} mosquitto.service"
fi
AM_CONDITIONAL([USE_MQTT], [test "x$with_mqtt_client" = "xyes"])

if test "x$with_mqtt_client" = "xyes" && test "x$with_avahi" != "xyes" ; then
  AC_MSG_WARN([MQTT needs Avahi for remote control functionality. With the current configuration settings, only metadata publishing will be supported.])
fi

if test "x$with_mpris_interface" = "xyes" || test "x$with_dbus_interface" = "xyes" || test "x$with_mqtt_client" = "xyes" ; then
  REQUESTED_EXTENDED_METADATA_SUPPORT=1
  AC_DEFINE([CONFIG_METADATA_HUB], 1, [Needed by the compiler.])
  AC_DEFINE([CONFIG_DACP_CLIENT], 1, [Needed by the compiler.])
fi
AM_CONDITIONAL([USE_METADATA_HUB], [test "x$REQUESTED_EXTENDED_METADATA_SUPPORT" = "x1"])
AM_CONDITIONAL([USE_DACP_CLIENT], [test "x$REQUESTED_EXTENDED_METADATA_SUPPORT" = "x1"])

if test "x$REQUESTED_EXTENDED_METADATA_SUPPORT" = "x1" || test "x$with_metadata" = "xyes" ; then
  AC_DEFINE([CONFIG_METADATA], 1, [Needed by the compiler.])
fi
AM_CONDITIONAL([USE_METADATA], [test "x$with_metadata" = "xyes"])

# Look for AirPlay 2 flag
AC_ARG_WITH(airplay-2, [AS_HELP_STRING([--with-airplay-2],[Build for AirPlay 2])])
if test "x$with_airplay_2" = "xyes" ; then
  AC_DEFINE([CONFIG_AIRPLAY_2], 1, [Build for AirPlay 2])
  AC_CHECK_PROG(PLISTUTIL_CHECK,plistutil,yes)
  AS_IF([test x"$PLISTUTIL_CHECK" != x"yes"], [AC_MSG_ERROR([plistutil can not be found. Please install plistutil for building for AirPlay 2. Maybe it's in the apt package libplist-utils.])])
  AC_CHECK_PROG(XXD_CHECK,xxd,yes)
  AS_IF([test x"$XXD_CHECK" != x"yes"], [AC_MSG_ERROR([xxd can not be found. Please install xxd for building for AirPlay 2.])])
  LIBPLIST_PACKAGE=libplist
  PKG_CHECK_EXISTS(libplist-2.0, LIBPLIST_PACKAGE=libplist-2.0)
  PKG_CHECK_MODULES(
    [libplist], [${LIBPLIST_PACKAGE} >= 2.3.0],
    [CFLAGS="${libplist_CFLAGS} ${CFLAGS}" LIBS="${libplist_LIBS} ${LIBS}" AC_DEFINE([HAVE_LIBPLIST_GE_2_3_0], 1, [libplist >= 2.3.0])],
    [PKG_CHECK_MODULES(
      [libplist], [${LIBPLIST_PACKAGE} >= 2.0.0],
      [CFLAGS="${libplist_CFLAGS} ${CFLAGS}" LIBS="${libplist_LIBS} ${LIBS}"],
      [AC_MSG_ERROR(AirPlay 2 support requires libplist 2.0.0 or later -- search for pkg libplist-dev on Debian or libplist-2.2.0 or later on FreeBSD!)]
    )]
  )
  PKG_CHECK_MODULES(
    [libsodium], [libsodium],
    [CFLAGS="${libsodium_CFLAGS} ${CFLAGS}" LIBS="${libsodium_LIBS} ${LIBS}"],
    [AC_MSG_ERROR(AirPlay 2 support requires libsodium -- libsodium-dev suggested)]
  )
  PKG_CHECK_MODULES(
    [libgcrypt], [libgcrypt],
    [CFLAGS="${libgcrypt_CFLAGS} ${CFLAGS}" LIBS="${libgcrypt_LIBS} ${LIBS}"],
    [AC_CHECK_LIB(
      [gcrypt], [gcry_control],
      [],
      [AC_MSG_ERROR([Airplay 2 support requires libgcrypt -- libgcrypt-dev suggested])]
    )
  ])
  AC_CHECK_LIB([uuid],[uuid_generate], [], [AC_MSG_ERROR([AirPlay 2 support requires the uuid library -- uuid-dev suggested])])
fi
AM_CONDITIONAL([USE_AIRPLAY_2], [test "x$with_airplay_2" = "xyes"])

# Look for ffmpeg flag

AC_ARG_WITH(ffmpeg, [AS_HELP_STRING([--with-ffmpeg],[Build with FFMPEG support (recommended)])])
if test "x$with_ffmpeg" = "xyes" -o "x$with_airplay_2" = "xyes" ; then
  using_ffmpeg=true
  AC_DEFINE([CONFIG_FFMPEG], 1, [Include support for FFmpeg decoders, resampler and utilities.])
  PKG_CHECK_MODULES(
    [libavutil], [libavutil],
    [CFLAGS="${libavutil_CFLAGS} ${CFLAGS}" LIBS="${libavutil_LIBS} ${LIBS}"],
    [AC_MSG_ERROR(AirPlay 2 support requires libavutil -- libavutil-dev suggested)]
  )
  PKG_CHECK_MODULES(
    [libavcodec], [libavcodec],
    [CFLAGS="${libavcodec_CFLAGS} ${CFLAGS}" LIBS="${libavcodec_LIBS} ${LIBS}"],
    [AC_CHECK_LIB(
      [avcodec], [avcodec_find_decoder],
      [],
      [AC_MSG_ERROR(AirPlay 2 support requires libavcodec -- libavcodec-dev suggested)]
    )]
  )
  PKG_CHECK_MODULES(
    [libavformat], [libavformat],
    [CFLAGS="${libavformat_CFLAGS} ${CFLAGS}" LIBS="${libavformat_LIBS} ${LIBS}"],
    [AC_CHECK_LIB(
      [avformat], [avformat_new_stream],
      [],
      [AC_MSG_ERROR([AirPlay 2 support requires libavformat -- libavformat-dev suggested])]
    )]
  )
  PKG_CHECK_MODULES(
    [libswresample], [libswresample],
    [CFLAGS="${libswresample_CFLAGS} ${CFLAGS}" LIBS="${libswresample_LIBS} ${LIBS}"],
    [AC_CHECK_LIB(
      [swresample], [swr_convert],
      [],
      [AC_MSG_ERROR([AirPlay 2 support requires libswresample -- libswresample-dev suggested])]
    )]
  )
  PKG_CHECK_MODULES(
    [libuuid], [uuid],
    [CFLAGS="${libuuid_CFLAGS} ${CFLAGS}" LIBS="${libuuid_LIBS} ${LIBS}"],
    [AC_CHECK_LIB(
      [uuid], [uuid_generate],
      [],
      [AC_MSG_ERROR([AirPlay 2 support requires the uuid library -- uuid-dev suggested])]
    )]
  )
else
  using_ffmpeg=false
fi

if test $using_ffmpeg = false ; then
  AC_DEFINE([CONFIG_HAMMERTON], 1, [Include support for using the ALAC Decoder by David Hammerton (deprecated)])
fi
AM_CONDITIONAL([USE_HAMMERTON], [test $using_ffmpeg = false])

# Look for Apple ALAC flag
AC_ARG_WITH(apple-alac,[AS_HELP_STRING([--with-apple-alac],[include support for the Apple ALAC decoder (deprecated)])])
if test "x${with_apple_alac}"  = "xyes" ; then
  if test $using_ffmpeg = true ; then
    AC_MSG_RESULT([Note: the FFMPEG decoder, included in the build, supercedes the Apple ALAC decoder, which is deprecated. Consider omitting the Apple ALAC decoder.])
  fi 
  AC_DEFINE([CONFIG_APPLE_ALAC], 1, [Include support for using the Apple ALAC Decoder (deprecated)])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES([alac], [alac], [LIBS="${alac_LIBS} ${LIBS}"], AC_MSG_ERROR(Apple ALAC Decoder support requires the ALAC library. See https://github.com/mikebrady/alac.))
  else
    AC_CHECK_LIB([alac], [BitBufferInit], , AC_MSG_ERROR(Apple ALAC Decoder support requires the ALAC library. See https://github.com/mikebrady/alac.))
  fi
fi
AM_CONDITIONAL([USE_APPLE_ALAC], [test "x${with_apple_alac}" = "xyes"])

if  test "x${with_systemd_startup}" = xyes ; then
  # Find systemd unit dir
  AC_ARG_WITH([systemdsystemunitdir],
       [AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files])],,
       [with_systemdsystemunitdir=auto])
  AS_IF([test "x$with_systemdsystemunitdir" = "xyes" -o "x$with_systemdsystemunitdir" = "xauto"], [
       def_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)

       AS_IF([test "x$def_systemdsystemunitdir" = "x"],
     [AS_IF([test "x$with_systemdsystemunitdir" = "xyes" -o "x$with_systemdsystemunitdir" = "xauto"],
      [AC_MSG_ERROR([pkg-config unable to query systemd package -- install systemd-dev package suggested])])
      with_systemdsystemunitdir=no],
     [with_systemdsystemunitdir="$def_systemdsystemunitdir"])])
  AS_IF([test "x$with_systemdsystemunitdir" != "xno"],
        [AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])])
fi

# Checks for header files.
AC_CHECK_HEADERS([getopt_long.h])
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h mach/mach.h memory.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_CHECK_FUNCS([atexit clock_gettime gethostname inet_ntoa memchr memmove memset mkfifo pow select socket stpcpy strcasecmp strchr strdup strerror strstr strtol strtoul])

if test "x${systemd_after_args}" != x ; then
  systemd_after_args="After=${systemd_after_args}"
fi

if test "x${systemd_requires_args}" != x ; then
  systemd_requires_args="Requires=${systemd_requires_args}"
fi

AC_SUBST([SYSTEMD_AFTER_ARGS], [${systemd_after_args}])
AC_SUBST([SYSTEMD_REQUIRES_ARGS], [${systemd_requires_args}])

# The man directory will not be recursively made unless you set the SUBDIR in Makefile.am.
# But leave this as is to enable a distclean to be done.

AC_CONFIG_FILES([Makefile man/Makefile])

AC_CONFIG_FILES([scripts/shairport-sync.service])
AC_CONFIG_FILES([scripts/shairport-sync],[chmod +x scripts/shairport-sync])
AC_OUTPUT
