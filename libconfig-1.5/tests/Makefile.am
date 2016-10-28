
noinst_PROGRAMS = libconfig_tests

libconfig_tests_SOURCES = tests.c

libconfig_tests_CPPFLAGS = -I$(top_srcdir)/tinytest -I../lib

libconfig_tests_LDADD = -L$(top_builddir)/tinytest -ltinytest \
	-L$(top_builddir)/lib/.libs -lconfig


EXTRA_DIST = \
	tests.vcproj \
	testdata/*.cfg \
	testdata/*.txt
