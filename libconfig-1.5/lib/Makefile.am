
lib_LTLIBRARIES = libconfig.la

if BUILDCXX
lib_LTLIBRARIES += libconfig++.la
endif

# Versioning rules ( C:R:A )
#
# Perform EACH step that applies.
#
# 1. Start with version 0:0:0.
# 2. If any of the sources have changed, increment R. This is a new revision
#    of the current interface.
# 3. If the interface has changed, increment C and set R to 0. This is the
#    first revision of a new interface.
# 4. If the interface has changed, and the new interface is a superset of the
#    previous interface (that is, if the previous interface has not been
#    broken by the changes in this new release), increment A. This release is
#    backwards compatible with the previous release.
# 5. If the new interface has removed elements with respect to the
#    previous interface, then backward compatibility is broken; set A to 0.
#    This release has a new, but backwards incompatible interface.
#
# For more info see section 6.3 of the GNU Libtool Manual.

VERINFO = -version-info 11:0:2

PARSER_PREFIX = libconfig_yy

libconfig_la_LDFLAGS = $(VERINFO) -no-undefined

libconfig___la_LDFLAGS = $(VERINFO) -no-undefined

libsrc = libconfig.c scanner.l grammar.y parsectx.h wincompat.h \
	scanctx.c scanctx.h strbuf.c strbuf.h
libinc = libconfig.h

libsrc_cpp =  $(libsrc) libconfigcpp.c++
libinc_cpp =  libconfig.h++

BUILT_SOURCES = scanner.c scanner.h grammar.c grammar.h

libconfig_la_SOURCES = $(libsrc)

libconfig___la_SOURCES = $(libsrc_cpp)

libcppflags = -D_REENTRANT
libcppxxflags = -D_REENTRANT

if GNU_WIN
libcppflags += -DLIBCONFIG_EXPORTS
libcppxxflags += -DLIBCONFIGXX_EXPORTS -DLIBCONFIG_STATIC
endif

libconfig_la_CPPFLAGS = -DPARSER_PREFIX=\"$(PARSER_PREFIX)\" $(libcppflags)
libconfig___la_CPPFLAGS = -DPARSER_PREFIX=\"$(PARSER_PREFIX)\" $(libcppxxflags)

include_HEADERS = $(libinc)

if BUILDCXX
include_HEADERS += $(libinc_cpp)
endif

AM_YFLAGS = -d -p $(PARSER_PREFIX)

AM_LFLAGS = --header-file=scanner.h --prefix=$(PARSER_PREFIX)

EXTRA_DIST = \
	$(BUILT_SOURCES) \
	libconfigcpp.cc libconfig.hh \
	*.vcproj

pkgconfigdir = $(libdir)/pkgconfig

pkgconfig_DATA = libconfig.pc

if BUILDCXX
pkgconfig_DATA += libconfig++.pc
endif
