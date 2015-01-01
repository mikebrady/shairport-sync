#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.50])
AC_INIT([shairport-sync], [2.1.15], [mikebrady@eircom.net])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([shairport.c])
AC_CONFIG_HEADERS([config.h])

# Derived from the Avahi configure.ac file
#
# Specifying the OS type, defaulting to linux.
#
AC_ARG_WITH(os_type, AS_HELP_STRING([--with-os-type=OSType],[Specify the distribution to target: One of linux darwin]))
if test "z$with_os_type" = "z"; then
  with_os_type="linux"
fi
with_os_type=`echo ${with_os_type} | tr '[[:upper:]]' '[[:lower:]]' `

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

PKG_PROG_PKG_CONFIG([0.9.0])

# Checks for libraries.
if test "x${with_os_type}" = xlinux; then
 AC_CHECK_LIB([rt],[clock_gettime], , AC_MSG_ERROR(librt needed))
fi

AC_CHECK_LIB([daemon],[daemon_log], , AC_MSG_ERROR(libdaemon needed))
AC_CHECK_LIB([pthread],[pthread_create], , AC_MSG_ERROR(pthread library needed))
AC_CHECK_LIB([m],[exp], , AC_MSG_ERROR(maths library needed))
PKG_CHECK_MODULES(
    [POPT], [popt],
    [LIBS="${POPT_LIBS} ${LIBS}"
     AC_DEFINE([HAVE_LIBPOPT],[1],[Define to 1 if you have popt])])

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
AC_ARG_WITH(ssl, [ choose --with-ssl=openssl or --with-ssl=polarssl for encryption services], [
  AC_MSG_CHECKING(encryption libraries chosen)
  if test "x${with_ssl}" = x -o "x${with_ssl}" = xyes ; then
    AC_MSG_RESULT(not found)
    AC_MSG_ERROR(choose either "openssl" or "polarssl" encryption)
  elif test "x${with_ssl}" = xopenssl ; then
    PKG_CHECK_MODULES(
        [CRYPTO], [libcrypto],
        [LIBS="${CRYPTO_LIBS} ${LIBS}"
         AC_DEFINE([HAVE_LIBCRYPTO],[1],[Define to 1 if you have libcrypto])])
    PKG_CHECK_MODULES(
        [SSL], [libssl],
        [LIBS="${SSL_LIBS} ${LIBS}"
         AC_DEFINE([HAVE_LIBSSL],[1],[Define to 1 if you have libssl])])
  elif test "x${with_ssl}" = xpolarssl ; then
    AC_CHECK_LIB([polarssl],[ssl_init], , AC_MSG_ERROR(PolarSSL selected but the library cannot be found!))
  else
    AC_MSG_ERROR(unknown option "${with_ssl}"." Please choose with "openssl" or "polarssl")
  fi
], )

# Look for soxr flag
AC_ARG_WITH(soxr, [  --with-soxr = choose libsoxr for high-quality interpolation], [
  AC_MSG_RESULT(>>Including support for soxr-based interpolation)
  AC_CHECK_LIB([soxr],[soxr_create], , AC_MSG_ERROR(soxr support requested but libsoxr not found!))], )

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
AC_ARG_WITH(alsa, [  --with-alsa = choose native ALSA API support (GNU/Linux only)],
  [AC_MSG_RESULT(>>Including an ALSA back end)
  HAS_ALSA=1
  AM_CONDITIONAL([USE_ALSA], [test 0])
  AC_DEFINE([CONFIG_ALSA], 1, [Needed by the compiler.])
  PKG_CHECK_MODULES(
    [ALSA], [alsa],
    [LIBS="${ALSA_LIBS} ${LIBS}"
     AC_DEFINE([HAVE_LIBASOUND],[1],[Define to 1 if you have ALSA])])])
AM_CONDITIONAL([USE_ALSA], [test "x$HAS_ALSA" = "x1"])

# Look for SNDIO flag
AC_ARG_WITH(sndio, [  --with-sndio = choose native SNDIO API support (FreeBSD) -- probably broken], [
  AC_MSG_RESULT(>>Including a SNDIO back end --- N.B. this is probably broken!)
  HAS_SNDIO=1
  AC_DEFINE([CONFIG_SNDIO], 1, [Needed by the compiler.])
  AC_CHECK_LIB([sndio], [sio_open], , AC_MSG_ERROR(SNDIO support requires the sndio library!))], )
AM_CONDITIONAL([USE_SNDIO], [test "x$HAS_SNDIO" = "x1"])
  
# Look for AO flag
AC_ARG_WITH(ao, [  --with-ao = choose native AO (Audio Output?) API support -- probably broken], [
  AC_MSG_RESULT(>>Including an AO back end --- N.B. this is probably broken!)
  HAS_AO=1
  AC_DEFINE([CONFIG_AO], 1, [Needed by the compiler.])
  AC_CHECK_LIB([ao], [ao_initialize], , AC_MSG_ERROR(AO support requires the ao library!))], )
AM_CONDITIONAL([USE_AO], [test "x$HAS_AO" = "x1"])

# Look for pulseaudio flag
AC_ARG_WITH(pulseaudio, [  --with-pulseaudio = choose native PulseAudio API support -- probably broken], [
  AC_MSG_RESULT(>>Including a PulseAudio back end --- N.B. this is probably broken!)
  HAS_PULSE=1
  AC_DEFINE([CONFIG_PULSE], 1, [Needed by the compiler.])
  AC_CHECK_LIB([pulse-simple], [pa_simple_new], , AC_MSG_ERROR(PulseAudio support requires the pulse-simple library!))], )
AM_CONDITIONAL([USE_PULSE], [test "x$HAS_PULSE" = "x1"])

# Look for dns_sd flag
AC_ARG_WITH(dns_sd, [  --with-dns_sd = choose dns_sd mDNS support], [
  AC_MSG_RESULT(>>Including dns_sd for mDNS support)
  HAS_DNS_SD=1
  AC_DEFINE([CONFIG_HAVE_DNS_SD_H], 1, [Needed by the compiler.])
  AC_CHECK_LIB([dns_sd], [DNSServiceRefDeallocate], , AC_MSG_ERROR(dns_sd support requires the dns_sd library!))], )
AM_CONDITIONAL([USE_DNS_SD], [test "x$HAS_DNS_SD" = "x1"])

# Check --with-init=argument
AC_ARG_WITH(init, [ choose --with-init=sysv or --with-init=systemd for init type ], [
  if test "x${with_init}" = x -o "x${with_init}" = xyes -o "x${with_init}" = xsysv ; then
    AC_MSG_RESULT(>>Using SysV init script)
    init=sysv
  elif test "x${with_init}" = xsystemd ; then
    AC_MSG_RESULT(>>Using Systemd init script)
    init=systemd
  else
    AC_MSG_ERROR(unknown option "${with_init}"." Please choose with "sysv" or "systemd")
  fi
], )
AM_CONDITIONAL([USE_SYSTEMD], [test "x$init" = "xsystemd"])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([getopt_long.h])
AC_CHECK_HEADERS([arpa/inet.h fcntl.h mach/mach.h memory.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h syslog.h unistd.h])

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
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([clock_gettime gethostname gettimeofday inet_ntoa memchr memmove memset pow select socket stpcpy strcasecmp strchr strdup strerror strstr strtol strtoul])

AC_CONFIG_FILES([Makefile man/Makefile])
AC_OUTPUT
