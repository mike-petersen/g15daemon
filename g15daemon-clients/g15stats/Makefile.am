
AM_CFLAGS = -O3 -Wall @CFLAGS@ -I$(top_builddir) -I$(top_srcdir) 
METASOURCES = AUTO
bin_PROGRAMS = g15stats

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
doc_DATA = README ChangeLog AUTHORS NEWS COPYING

LIBS = -lg15daemon_client -lg15render `pkg-config --libs libgtop-2.0`
IMCLUDES = -I
g15stats_LDFLAGS = -avoid-version
g15stats_SOURCES =  g15stats.c


