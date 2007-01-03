
AM_CFLAGS = -O3 -Wall @CFLAGS@ -I$(top_builddir) -I$(top_srcdir) 
METASOURCES = AUTO
bin_PROGRAMS = g15message

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
doc_DATA = README ChangeLog AUTHORS NEWS COPYING

LIBS = -lg15daemon_client -lg15render
IMCLUDES = -I
g15message_LDFLAGS = -avoid-version
g15message_SOURCES =  g15message.c


