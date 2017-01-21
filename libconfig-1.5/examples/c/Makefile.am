
noinst_PROGRAMS = example1 example2 example3

example1_SOURCES = example1.c

example2_SOURCES = example2.c

example3_SOURCES = example3.c

AM_CPPFLAGS = -I $(top_srcdir)/lib

LDADD = $(top_builddir)/lib/libconfig.la

EXTRA_DIST = \
	example.cfg \
	*.vcproj
