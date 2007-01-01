
AM_CFLAGS = -O3 -Wall @CFLAGS@ -I$(top_builddir) -I$(top_srcdir) 
METASOURCES = AUTO
bin_PROGRAMS = g15mpd

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
doc_DATA = README ChangeLog AUTHORS NEWS COPYING

LIBS = -lg15daemon_client -lg15render -lX11 -lXtst -lmpd 
IMCLUDES = -I
g15mpd_LDFLAGS = -avoid-version
g15mpd_SOURCES =  g15mpd.c


