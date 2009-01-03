
AM_CFLAGS = -O3 -Wall @CFLAGS@ -I$(top_builddir) -I$(top_srcdir) 
METASOURCES = AUTO
bin_PROGRAMS = g15stats
noinst_HEADERS = g15stats.h

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
doc_DATA = README ChangeLog AUTHORS NEWS COPYING

LIBS = -lg15daemon_client -lg15render 
IMCLUDES = -I
g15stats_LDFLAGS = -avoid-version
g15stats_SOURCES =  g15stats.c g15stats.h


