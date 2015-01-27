## Process this file with automake to produce Makefile.in

bin_PROGRAMS = file-apng

bindir = $(PLUGINDIR)/plug-ins

file_apng_SOURCES = \
	plugin-intl.h	\
	file-apng.c

file_apng_CPPFLAGS = \
	-I$(top_srcdir)		\
	$(PNG_CFLAGS)		\
	$(GIMP_CFLAGS)		\
	-I$(includedir)		\
	-DLOCALEDIR=\""$(LOCALEDIR)"\"		\
	-DDATADIR=\""$(DATADIR)"\"

file_apng_LDADD = \
	$(PNG_LIBS)	\
	$(GIMP_LIBS)

AM_CPPFLAGS =\
	-I$(top_srcdir)		\
	$(GIMP_CFLAGS)		\
	-I$(includedir)

LDADD = $(GIMP_LIBS)

