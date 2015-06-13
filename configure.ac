#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.50])
AC_INIT([shairport-sync], [2.3.4], [mikebrady@eircom.net])
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

##### Some build systems are not fully using pkg-config, so we can use the flag ${with_pkg_config} on a case-by-case basis
##### to control how to deal with them

AC_ARG_WITH([pkg_config],
[  --with-pkg-config = use pkg-config to find libraries], ,[with_pkg_config=1])

AC_CHECK_LIB([daemon],[daemon_log], , AC_MSG_ERROR(libdaemon needed))
AC_CHECK_LIB([pthread],[pthread_create], , AC_MSG_ERROR(pthread library needed))
AC_CHECK_LIB([m],[exp], , AC_MSG_ERROR(maths library needed))

AC_MSG_RESULT(>>Including libpopt)
if  test "x${with_pkg_config}" = x1 ; then
  PKG_CHECK_MODULES(
      [POPT], [popt],
      [LIBS="${POPT_LIBS} ${LIBS}"
       AC_DEFINE([HAVE_LIBPOPT],[1],[Define to 1 if you have popt])])
else
  AC_CHECK_LIB([popt],[poptGetContext], , AC_MSG_ERROR(libpopt needed))
fi

AC_ARG_WITH([dummy],[  --with-dummy = include the dummy audio back end ],[AC_MSG_RESULT(>>Including the dummy audio back end) AC_DEFINE([CONFIG_DUMMY], 1, [Needed by the compiler.]) ], )
AM_CONDITIONAL([USE_DUMMY], [test "x$with_dummy" = "x1" ])

AC_ARG_WITH([stdout],[  --with-stdout = include the stdout audio back end ],[ AC_MSG_RESULT(>>Including the stdout audio back end)  AC_DEFINE([CONFIG_STDOUT], 1, [Needed by the compiler.]) ], )
AM_CONDITIONAL([USE_STDOUT], [test "x$with_stdout" = "x1" ])

AC_ARG_WITH([pipe],[  --with-pipe = include the pipe audio back end ],[ AC_MSG_RESULT(>>Including the pipe audio back end)  AC_DEFINE([CONFIG_PIPE], 1, [Needed by the compiler.]) ], )
AM_CONDITIONAL([USE_PIPE], [test "x$with_pipe" = "x1" ])

AC_ARG_WITH([initscript],
[  --with-initscript = include a startup script], ,[with_initscript=1])
AM_CONDITIONAL([INSTALL_INITSCRIPT], [test "x$with_initscript" = "x1"])

# Check if we want to support reading arguments from the command line     
AC_ARG_WITH([command_line_argument_support],
[  --with-command-line-argument-support = read configuration from command line arguments], ,[with_command_line_argument_support=1])

if test "x${with_command_line_argument_support}" = x1 ; then
  AC_MSG_RESULT(>>Including command line argument support)
  AC_DEFINE([COMMAND_LINE_ARGUMENT_SUPPORT],[1],[Define to 1 if you want to support command line arguments])
fi    

# Check if we want to support reading from a configuration file chosen -- i.e. take parameters from /etc/shairport-sync.conf     
AC_ARG_WITH([configfile_support],
[  --with-configfile-support = support reading of configuration from a configuration file], ,[with_configfile_support=1])

if test "x${with_configfile_support}" = x1 ; then
  AC_MSG_RESULT(>>Including configuration file support)
  PKG_CHECK_MODULES(
      [LIBCONFIG], [libconfig],
      [LIBS="${LIBCONFIG_LIBS} ${LIBS}"])
  AC_DEFINE([SUPPORT_CONFIG_FILES],[1],[Define to 1 if you want to support configuration files])
fi

AC_ARG_WITH([configfiles],
[  --with-configfiles = include configuration files in installation ], ,[with_configfiles=1])
AM_CONDITIONAL([INSTALL_CONFIG_FILES], [test "x$with_configfiles" = "x1" -a  "x$with_configfile_support" = "x1" ])

   
# Check to see if we should include the initscript

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
  fi
  if test "x${with_ssl}" = xopenssl ; then
    PKG_CHECK_MODULES(
        [SSL], [libssl,libcrypto],
        [LIBS="${SSL_LIBS} ${LIBS}"
        AC_DEFINE([HAVE_LIBCRYPTO],[1],[Define to 1 if you have libcrypto])
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

# Look for metadata flag -- set flag for conditional compilation
AC_ARG_WITH(metadata, [  --with-metadata = include support for a metadata feed], [
  AC_MSG_RESULT(>>Including metadata support)
  HAS_METADATA=1
  AC_DEFINE([CONFIG_METADATA], 1, [Needed by the compiler.])], )
AM_CONDITIONAL([USE_METADATA], [test "x$HAS_METADATA" = "x1"])

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
AC_ARG_WITH(pulseaudio, [  --with-pulseaudio = choose native PulseAudio API support -- N.B. no synchronisation -- so underflow or overflow is inevitable!], [
  AC_MSG_RESULT(>>Including a PulseAudio back end -- N.B. no synchronisation -- so underflow or overflow is inevitable!)
  HAS_PULSE=1
  AC_DEFINE([CONFIG_PULSE], 1, [Needed by the compiler.])
  AC_CHECK_LIB([pulse-simple], [pa_simple_new], , AC_MSG_ERROR(PulseAudio support requires the pulse-simple library!))
  AC_CHECK_LIB([pulse], [pa_stream_peek], , AC_MSG_ERROR(PulseAudio support requires the libpulse-dev library!))], )
AM_CONDITIONAL([USE_PULSE], [test "x$HAS_PULSE" = "x1"])

# Look for dns_sd flag
AC_ARG_WITH(dns_sd, [  --with-dns_sd = choose dns_sd mDNS support], [
  AC_MSG_RESULT(>>Including dns_sd for mDNS support)
  HAS_DNS_SD=1
  AC_DEFINE([CONFIG_HAVE_DNS_SD_H], 1, [Needed by the compiler.])
  AC_CHECK_LIB([dns_sd], [DNSServiceRefDeallocate], , AC_MSG_ERROR(dns_sd support requires the dns_sd library!))], )
AM_CONDITIONAL([USE_DNS_SD], [test "x$HAS_DNS_SD" = "x1"])

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
