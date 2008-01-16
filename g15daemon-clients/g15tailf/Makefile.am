
AM_CFLAGS = -O3 -Wall @CFLAGS@ -I$(top_builddir) -I$(top_srcdir) 
METASOURCES = AUTO
bin_PROGRAMS = g15tailf g15term

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
doc_DATA = README ChangeLog AUTHORS NEWS COPYING

LIBS = -lg15daemon_client -lg15render
IMCLUDES = -I
g15tailf_LDFLAGS = -avoid-version
g15tailf_SOURCES =  g15tailf.c
g15term_LDFLAGS = -avoid-version
g15term_SOURCES = g15term.c

