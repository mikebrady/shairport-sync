#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.50])
AC_INIT([shairport-sync], [3.3d3], [mikebrady@eircom.net])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([shairport.c])
AC_CONFIG_HEADERS([config.h])

# Derived from the Avahi configure.ac file
# Specifying the OS type, defaulting to linux.
#
AC_ARG_WITH(os, AS_HELP_STRING([--with-os=OSType],[Specify the distribution to target: One of linux freebsd or darwin]))
if test "z$with_os" = "z"; then
  with_os="linux"
fi
with_os=`echo ${with_os} | tr '[[:upper:]]' '[[:lower:]]' `

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL

PKG_PROG_PKG_CONFIG([0.9.0])

# Checks for libraries.
if test "x${with_os}" = xlinux -o "x${with_os}" = xfreebsd ; then
 AC_CHECK_LIB([rt],[clock_gettime], , AC_MSG_ERROR(librt needed))
fi

if test "x${with_os}" = xopenbsd ; then
 AC_CHECK_LIB([c],[clock_gettime], , AC_MSG_ERROR(libc needed))
fi

AM_CONDITIONAL([BUILD_FOR_FREEBSD], [test "x${with_os}" = xfreebsd ])

AM_CONDITIONAL([BUILD_FOR_OPENBSD], [test "x${with_os}" = xopenbsd ])


##### Some build systems are not fully using pkg-config, so we can use the flag ${with_pkg_config} on a case-by-case basis
##### to control how to deal with them

AC_ARG_WITH([pkg_config],
[  --with-pkg-config = use pkg-config to find libraries], ,[with_pkg_config=yes])

AC_MSG_RESULT(>>Including libdaemon)
if  test "x${with_pkg_config}" = xyes ; then
  PKG_CHECK_MODULES(
      [DAEMON], [libdaemon],
      [LIBS="${DAEMON_LIBS} ${LIBS}"])
else
  AC_CHECK_LIB([daemon],[daemon_fork], , AC_MSG_ERROR(libdaemon needed))
fi

##### The following check for the pthreads library doesn't put the compiler into the correct pthread mode
##### so we add the -pthread compilation flag in AMFLAGS in the Makefile.am as well.

AC_CHECK_LIB([pthread],[pthread_create], , AC_MSG_ERROR(pthread library needed))
AC_CHECK_LIB([m],[exp], , AC_MSG_ERROR(maths library needed))

AC_MSG_RESULT(>>Including libpopt)
if  test "x${with_pkg_config}" = xyes ; then
  PKG_CHECK_MODULES(
      [POPT], [popt],
      [CFLAGS="${POPT_CFLAGS} ${CFLAGS}"
      LIBS="${POPT_LIBS} ${LIBS}"
       AC_DEFINE([HAVE_LIBPOPT],[1],[Define to 1 if you have popt])])
else
  AC_CHECK_LIB([popt],[poptGetContext], , AC_MSG_ERROR(libpopt needed))
  AC_DEFINE([HAVE_LIBPOPT],[1],[Define to 1 if you have popt])
fi

AC_ARG_WITH([dummy],[  --with-dummy = include the dummy audio back end ],[AC_MSG_RESULT(>>Including the dummy audio back end) AC_DEFINE([CONFIG_DUMMY], 1, [Needed by the compiler.]) ], )
AM_CONDITIONAL([USE_DUMMY], [test "x$with_dummy" = "xyes" ])

AC_ARG_WITH([stdout],[  --with-stdout = include the stdout audio back end ],[ AC_MSG_RESULT(>>Including the stdout audio back end)  AC_DEFINE([CONFIG_STDOUT], 1, [Needed by the compiler.]) ], )

AM_CONDITIONAL([USE_STDOUT], [test "x$with_stdout" = "xyes" ])

AC_ARG_WITH([pipe],[  --with-pipe = include the pipe audio back end ],[ AC_MSG_RESULT(>>Including the pipe audio back end)  AC_DEFINE([CONFIG_PIPE], 1, [Needed by the compiler.]) ], )
AM_CONDITIONAL([USE_PIPE], [test "x$with_pipe" = "xyes" ])

# Check to see if we should include the System V initscript

AC_ARG_WITH([systemv],
[  --with-systemv = install a System V startup script during a make install], , )
AM_CONDITIONAL([INSTALL_SYSTEMV], [test "x$with_systemv" = "xyes"])

# Check to see if we should include the systemd stuff to define it as a service

AC_ARG_WITH([systemd],
[  --with-systemd = install a systemd startup script during a make install], , )
AM_CONDITIONAL([INSTALL_SYSTEMD], [test "x$with_systemd" = "xyes"])

AC_ARG_WITH([freebsd-service],
[  --with-freebsd-service = install a FreeBSD startup script during a make install], , )
AM_CONDITIONAL([INSTALL_FREEBSD_SERVICE], [test "x$with_freebsd_service" = "xyes"])

# Add the libconfig package
if  test "x${with_pkg_config}" = xyes ; then
  PKG_CHECK_MODULES(
      [LIBCONFIG], [libconfig],
      [LIBS="${LIBCONFIG_LIBS} ${LIBS}"])
else
  AC_CHECK_LIB([config],[config_init], , AC_MSG_ERROR([libconfig library needed]))
fi

AC_ARG_WITH([configfiles],
[  --with-configfiles = install configuration files during a make install ], ,[with_configfiles=yes])
AM_CONDITIONAL([INSTALL_CONFIG_FILES], [test "x$with_configfiles" = "xyes"])

# Look for Apple ALAC flag
AC_ARG_WITH(apple-alac, [  --with-apple-alac = include support for the Apple ALAC decoder],
  [AC_MSG_RESULT(>>Including the Apple ALAC Decoder)
  HAS_APPLE_ALAC=1
  AM_CONDITIONAL([USE_APPLE_ALAC], [test 0])
  AC_DEFINE([CONFIG_APPLE_ALAC], 1, [Needed by the compiler.])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
      [ALAC], [alac],
      [LIBS="${ALAC_LIBS} ${LIBS}"
       AC_DEFINE([HAVE_APPLE_ALAC],[1],[Define to 1 if you are using the Apple ALAC Decoder])])
  else
    AC_CHECK_LIB([alac], [BitBufferInit], , AC_MSG_ERROR(Apple ALAC Decoder support requires the alac library!))
    AC_DEFINE([HAVE_APPLE_ALAC],[1],[Define to 1 if you have the Apple ALAC library])
  fi ])
AM_CONDITIONAL([USE_APPLE_ALAC], [test "x$HAS_APPLE_ALAC" = "x1"])

# Look for piddir flag
AC_ARG_WITH(piddir, [ --with-piddir=<pathname> Specify a pathname to a directory in which to write the PID file.], [
	AC_MSG_CHECKING(--with-piddir argument)
	if test "x${with_piddir}" = x -o "x${with_piddir}" = xyes ; then
		AC_MSG_RESULT(not found)
		AC_MSG_ERROR(when you use the --with-piddir directive you must specify the pathname of the directory into which the PID file will be written)
	fi
	AC_MSG_RESULT(${with_piddir})
  HAS_CUSTOMPIDDIR=1
  AC_SUBST(CUSTOM_PID_DIR,["${with_piddir}"])
  AC_DEFINE([USE_CUSTOM_PID_DIR],1,[Hook up special proc to libdaemon to point to this directory])], )
AM_CONDITIONAL([USE_CUSTOMPIDDIR], [test "x$HAS_CUSTOMPIDDIR" = "x1"])

# Check --with-ssl=argument
AC_ARG_WITH(ssl, [ choose --with-ssl=openssl, --with-ssl=mbedtls or --with-ssl=polarssl (deprecated) for encryption services  ], [
  AC_MSG_CHECKING(encryption libraries chosen)
  if test "x${with_ssl}" = x -o "x${with_ssl}" = xyes ; then
    AC_MSG_RESULT(not found)
    AC_MSG_ERROR(choose "openssl", "mbedtls" or "polarssl" encryption)
  fi
  if test "x${with_ssl}" = xopenssl ; then
    if  test "x${with_pkg_config}" = xyes ; then
      PKG_CHECK_MODULES(
          [SSL], [libssl,libcrypto],
          [LIBS="${SSL_LIBS} ${LIBS}"
          AC_DEFINE([HAVE_LIBCRYPTO],[1],[Define to 1 if you have libcrypto])
          AC_DEFINE([HAVE_LIBSSL],[1],[Define to 1 if you have libssl])])
    else
      AC_CHECK_LIB([crypto], [main], , AC_MSG_ERROR(libcrypto selected but the library cannot be found!))
      AC_CHECK_LIB([ssl], [main], , AC_MSG_ERROR(libssl selected but the library cannot be found!))
      AC_DEFINE([HAVE_LIBCRYPTO],[1],[Define to 1 if you have libcrypto])
      AC_DEFINE([HAVE_LIBSSL],[1],[Define to 1 if you have libssl])
    fi
  elif test "x${with_ssl}" = xmbedtls ; then
    AC_CHECK_LIB([mbedtls],[mbedtls_ssl_init],,
		[AC_MSG_ERROR([mbed tls support requires the mbedtls library -- libmbedtls-dev suggested],1)])
    AC_CHECK_LIB([mbedcrypto], [mbedtls_entropy_func],,
                [AC_MSG_ERROR([mbed tls support requires the mbedcrypto library -- libmbedcrypto0 suggested],1)])
    AC_CHECK_LIB([mbedx509], [mbedtls_pk_init],,
                [AC_MSG_ERROR([mbed tls support requires the mbedx509 library -- libmbedx509-0 suggested],1)])
  elif test "x${with_ssl}" = xpolarssl ; then
    AC_CHECK_LIB([polarssl],[ssl_init], , AC_MSG_ERROR(PolarSSL is selected but the library cannot be found and is deprecated. Consider selecting mbed TLS instead using --with-ssl=mbedtls.))
  else
    AC_MSG_ERROR(unknown option "${with_ssl}"." Please choose with "openssl", "mbedtls" or "polarssl")
  fi
], )

# Look for soxr flag
AC_ARG_WITH(soxr, [  --with-soxr = choose libsoxr for high-quality interpolation], [
  AC_MSG_RESULT(>>Including support for soxr-based interpolation)
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
        [SOXR], [soxr],
        [LIBS="${SOXR_LIBS} ${LIBS}"])
  else
    AC_CHECK_LIB([avutil],[av_get_cpu_flags])
    if  test "x${ac_cv_lib_avutil_av_get_cpu_flags}" = xyes ; then
      # soxr may link against libavutil, depending on the architecture, but for the sake of simplicity link with it if it is found
      AC_CHECK_LIB([soxr],[soxr_create], , AC_MSG_ERROR(soxr support requested but libsoxr not found!), [-lavutil])
    else
      AC_CHECK_LIB([soxr],[soxr_create], , AC_MSG_ERROR(soxr support requested but libsoxr not found!))
    fi
  fi
], )

# Look for metadata flag -- set flag for conditional compilation
AC_ARG_WITH(metadata, [  --with-metadata = include support for a metadata feed], [
  AC_MSG_RESULT(>>Including metadata support)
  AC_DEFINE([CONFIG_METADATA], 1, [Needed by the compiler.])], )
AM_CONDITIONAL([USE_METADATA], [test "x$with_metadata" = "xyes"])

# What follows is a bit messy, because if the relevant library is requested, a compiler flag is defined, a file is included in the compilation
# and the relevant link files are added.

# Look for avahi flag
AC_ARG_WITH(avahi, [  --with-avahi = choose Avahi-based mDNS support], [
  AC_MSG_RESULT(>>Including Avahi mDNS support)
  HAS_AVAHI=1
  AC_DEFINE([CONFIG_AVAHI], 1, [Needed by the compiler.])
  AC_CHECK_LIB([avahi-client], [avahi_client_new], , AC_MSG_ERROR(Avahi support requires the avahi-client library!))
  AC_CHECK_LIB([avahi-common],[avahi_strerror], , AC_MSG_ERROR(Avahi support requires the avahi-common library!))], )
AM_CONDITIONAL([USE_AVAHI], [test "x$HAS_AVAHI" = "x1"])

# Look for tinysvcmdns flag
AC_ARG_WITH(tinysvcmdns, [  --with-tinysvcmdns = choose tinysvcmdns-based mDNS support], [
  AC_MSG_RESULT(>>Including tinysvcmdns mDNS support)
  HAS_TINYSVCMDNS=1
  AC_DEFINE([CONFIG_TINYSVCMDNS], 1, [Needed by the compiler.])], )
AM_CONDITIONAL([USE_TINYSVCMDNS], [test "x$HAS_TINYSVCMDNS" = "x1"])

# Look for ALSA flag
AC_ARG_WITH(alsa, [  --with-alsa = choose ALSA API support (GNU/Linux only)],
  [AC_MSG_RESULT(>>Including an ALSA back end)
  HAS_ALSA=1
  AM_CONDITIONAL([USE_ALSA], [test 0])
  AC_DEFINE([CONFIG_ALSA], 1, [Needed by the compiler.])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
      [ALSA], [alsa],
      [LIBS="${ALSA_LIBS} ${LIBS}"
       AC_DEFINE([HAVE_LIBASOUND],[1],[Define to 1 if you have ALSA])])
  else
    AC_CHECK_LIB([asound], [snd_pcm_open], , AC_MSG_ERROR(ALSA support requires the asound library!))
    AC_DEFINE([HAVE_LIBASOUND],[1],[Define to 1 if you have ALSA])
  fi ])
AM_CONDITIONAL([USE_ALSA], [test "x$HAS_ALSA" = "x1"])

# Look for SNDIO flag
AC_ARG_WITH(sndio, [  --with-sndio = choose SNDIO API support], [
  AC_MSG_RESULT(>>Including a SNDIO back end)
  HAS_SNDIO=1
  AC_DEFINE([CONFIG_SNDIO], 1, [Needed by the compiler.])
  AC_CHECK_LIB([sndio], [sio_open], , AC_MSG_ERROR(SNDIO support requires the sndio library -- libsndio-dev suggested))], )
AM_CONDITIONAL([USE_SNDIO], [test "x$HAS_SNDIO" = "x1"])

# Look for AO flag
AC_ARG_WITH(ao, [  --with-ao = choose AO (Audio Output?) API support. N.B. no synchronisation -- so underflow or overflow is inevitable!], [
  AC_MSG_RESULT(>>Including an AO back end. N.B. no synchronisation -- so underflow or overflow is inevitable!)
  HAS_AO=1
  AC_DEFINE([CONFIG_AO], 1, [Needed by the compiler.])
  AC_CHECK_LIB([ao], [ao_initialize], , AC_MSG_ERROR(AO support requires the ao library -- libao-dev suggested))], )
AM_CONDITIONAL([USE_AO], [test "x$HAS_AO" = "x1"])

# Look for Soundio flag
AC_ARG_WITH(soundio, [  --with-soundio = choose soundio API support.], [
  AC_MSG_RESULT(>>Including an soundio back end)
  HAS_SOUNDIO=1
  AC_DEFINE([CONFIG_SOUNDIO], 1, [Needed by the compiler.])
  AC_CHECK_LIB([soundio], [soundio_create], , AC_MSG_ERROR(soundio support requires the soundio library -- libsoundio-dev suggested))], )
AM_CONDITIONAL([USE_SOUNDIO], [test "x$HAS_SOUNDIO" = "x1"])

# Look for pulseaudio flag
AC_ARG_WITH(pa, [  --with-pa = choose PulseAudio support.], [
  AC_MSG_RESULT(>>Including a PulseAudio back end.)
  HAS_PA=1
  AC_DEFINE([CONFIG_PA], 1, [Needed by the compiler.])
  if  test "x${with_pkg_config}" = xyes ; then
    PKG_CHECK_MODULES(
      [PULSEAUDIO], [libpulse >= 0.9.2],
      [LIBS="${PULSEAUDIO_LIBS} ${LIBS}"],[AC_MSG_ERROR(PulseAudio support requires the libpulse-dev library!)])
  else
    AC_CHECK_LIB([pulse-simple], [pa_simple_new], , AC_MSG_ERROR(PulseAudio support requires the libpulse library!))
    AC_CHECK_LIB([pulse], [pa_stream_peek], , AC_MSG_ERROR(PulseAudio support requires the libpulse-dev library.))
  fi ])
AM_CONDITIONAL([USE_PA], [test "x$HAS_PA" = "x1"])

# Look for Convolution flag
AC_ARG_WITH(convolution, [  --with-convolution = choose audio DSP convolution support], [
  AC_MSG_RESULT(>>Including convolution support)
  HAS_CONVOLUTION=1
  AM_INIT_AUTOMAKE([subdir-objects])
  AC_DEFINE([CONFIG_CONVOLUTION], 1, [Needed by the compiler.])
  AC_CHECK_LIB([sndfile], [sf_open], , AC_MSG_ERROR(Convolution support requires the sndfile library!))], )
AM_CONDITIONAL([USE_CONVOLUTION], [test "x$HAS_CONVOLUTION" = "x1"])

# Look for dns_sd flag
AC_ARG_WITH(dns_sd, [  --with-dns_sd = choose dns_sd mDNS support], [
  AC_MSG_RESULT(>>Including dns_sd for mDNS support)
  HAS_DNS_SD=1
  AC_DEFINE([CONFIG_HAVE_DNS_SD_H], 1, [Needed by the compiler.])
  AC_SEARCH_LIBS([DNSServiceRefDeallocate], [dns_sd], , AC_MSG_ERROR(dns_sd support requires the dns_sd library!))], )
AM_CONDITIONAL([USE_DNS_SD], [test "x$HAS_DNS_SD" = "x1"])

# Look for dbus flag
AC_ARG_WITH(dbus-interface, [  --with-dbus-interface = include support for the native Shairport Sync D-Bus interface], [
  AC_MSG_RESULT(>>Including dbus support)
  HAS_DBUS=1
  AC_DEFINE([HAVE_DBUS], 1, [Needed by the compiler.])
  PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0 >= 2.30.0],[CFLAGS="${GIO_UNIX_CFLAGS} ${CFLAGS}" LIBS="${GIO_UNIX_LIBS} ${LIBS}"],[AC_MSG_ERROR(dbus messaging support requires the glib 2.0 library -- libglib2.0-dev suggested!)])
  ], )
AM_CONDITIONAL([USE_DBUS], [test "x$HAS_DBUS" = "x1"])

# Look for dbus test client flag
AC_ARG_WITH(dbus-test-client, [  --with-dbus-test-client = compile dbus test client], [
  AC_MSG_RESULT(>>Including dbus test client)
  HAS_DBUS_CLIENT=1
  AC_DEFINE([HAVE_DBUS_CLIENT], 1, [Needed by the compiler.])
  PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0 >= 2.30.0],[CFLAGS="${GIO_UNIX_CFLAGS} ${CFLAGS}" LIBS="${GIO_UNIX_LIBS} ${LIBS}"],[AC_MSG_ERROR(dbus client support requires the glib 2.0 library -- libglib2.0-dev suggested!)])
  ], )
AM_CONDITIONAL([USE_DBUS_CLIENT], [test "x$HAS_DBUS_CLIENT" = "x1"])

# Look for mpris flag
AC_ARG_WITH(mpris-interface, [  --with-mpris-interface = include support for a Shairport Sync D-Bus interface conforming to the MPRIS standard], [
  AC_MSG_RESULT(>>Including the MPRIS D-Bus Interface)
  HAS_MPRIS=1
  AC_DEFINE([HAVE_MPRIS], 1, [Needed by the compiler.])
  PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0 >= 2.30.0],[CFLAGS="${GIO_UNIX_CFLAGS} ${CFLAGS}" LIBS="${GIO_UNIX_LIBS} ${LIBS}"],[AC_MSG_ERROR(dbus messaging support for mpris requires the glib 2.0 library -- libglib2.0-dev suggested!)])
  ], )
AM_CONDITIONAL([USE_MPRIS], [test "x$HAS_MPRIS" = "x1"])

# Look for mpris test client flag
AC_ARG_WITH(mpris-test-client, [  --with-mpris-test-client = compile mpris test client], [
  AC_MSG_RESULT(>>Including mpris test client)
  HAS_MPRIS_CLIENT=1
  AC_DEFINE([HAVE_MPRIS_CLIENT], 1, [Needed by the compiler.])
  PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0 >= 2.30.0],[CFLAGS="${GIO_UNIX_CFLAGS} ${CFLAGS}" LIBS="${GIO_UNIX_LIBS} ${LIBS}"],[AC_MSG_ERROR(mpris client support requires the glib 2.0 library -- libglib2.0-dev suggested!)])
  ], )
AM_CONDITIONAL([USE_MPRIS_CLIENT], [test "x$HAS_MPRIS_CLIENT" = "x1"])

#AM_CONDITIONAL([USE_DBUS_CORE_AND_DACP], [test "x$HAS_MPRIS" = "x1" -o "x$HAS_DBUS" = "x1"])

# Look for mqtt flag
AC_ARG_WITH(mqtt-client, [  --with-mqtt-client = include support for the native Shairport Sync MQTT interface], [
  AC_MSG_RESULT(>>Including MQTT support)
  HAS_MQTT=1
  AC_DEFINE([HAVE_MQTT], 1, [Needed by the compiler.])
  AC_DEFINE([CONFIG_MQTT], 1, [Needed by the compiler.])
  AC_CHECK_LIB([mosquitto], [mosquitto_lib_init], , AC_MSG_ERROR(MQTT support requires the mosquitto library -- libmosquitto-dev suggested!))
  AC_CHECK_HEADERS([mosquitto.h],,[AC_MSG_ERROR([<mosquitto.h> header missing])])
  AC_DEFINE([HAVE_MOSQUITTO],[1],[Define to 1 if you have the mosquitto library])
  
  AC_MSG_RESULT(>>Enabling metadata as it is required by mqtt)
  AC_DEFINE([CONFIG_METADATA], 1, [Needed by the compiler.])
  AC_DEFINE([USE_METADATA], 1, [Needed by the compiler.])
  PKG_CHECK_MODULES([GIO_UNIX], [gio-unix-2.0 >= 2.30.0],[CFLAGS="${GIO_UNIX_CFLAGS} ${CFLAGS}" LIBS="${GIO_UNIX_LIBS} ${LIBS}"],[AC_MSG_ERROR(dbus messaging support for mpris requires the glib 2.0 library -- libglib2.0-dev suggested!)])
  ],)
AM_CONDITIONAL([USE_MQTT], [test "x$HAS_MQTT" = "x1"])


if test "x$HAS_MPRIS" = "x1" || test "x$HAS_DBUS" = "x1" || test "x$HAS_MQTT" = "x1"; then
  AC_MSG_RESULT(>>Including the metadata hub)
  HAS_METADATA_HUB=1
  AC_DEFINE([HAVE_METADATA_HUB], 1, [Needed by the compiler.])
  AC_MSG_RESULT(>>Including the DACP client)
  HAS_DACP_CLIENT=1
  AC_DEFINE([HAVE_DACP_CLIENT], 1, [Needed by the compiler.])
fi
AM_CONDITIONAL([USE_METADATA_HUB], [test "x$HAS_METADATA_HUB" = "x1"])
AM_CONDITIONAL([USE_DACP_CLIENT], [test "x$HAS_DACP_CLIENT" = "x1"])


if  test "x${with_systemd}" = xyes ; then
  # Find systemd unit dir
  AC_ARG_WITH([systemdsystemunitdir],
       [AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files])],,
       [with_systemdsystemunitdir=auto])
  AS_IF([test "x$with_systemdsystemunitdir" = "xyes" -o "x$with_systemdsystemunitdir" = "xauto"], [
       def_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)

       AS_IF([test "x$def_systemdsystemunitdir" = "x"],
     [AS_IF([test "x$with_systemdsystemunitdir" = "xyes"],
      [AC_MSG_ERROR([systemd support requested but pkg-config unable to query systemd package])])
      with_systemdsystemunitdir=no],
     [with_systemdsystemunitdir="$def_systemdsystemunitdir"])])
  AS_IF([test "x$with_systemdsystemunitdir" != "xno"],
        [AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])])
  AM_CONDITIONAL([HAVE_SYSTEMD], [test "x$with_systemdsystemunitdir" != "xno"])
else
  AM_CONDITIONAL([HAVE_SYSTEMD], false)  
fi

# Look for xmltoman
AC_CHECK_PROGS([XMLTOMAN], [xmltoman])
if test -z "$XMLTOMAN"; then
  AC_MSG_WARN([>>xmltoman not found - not rebuilding man pages])
fi
AM_CONDITIONAL([HAVE_XMLTOMAN], [test -n "$XMLTOMAN"])

# Checks for header files.
AC_HEADER_STDC
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

AC_CONFIG_FILES([Makefile man/Makefile scripts/shairport-sync.service])
AC_CONFIG_FILES([scripts/shairport-sync],[chmod +x scripts/shairport-sync])
AC_OUTPUT
