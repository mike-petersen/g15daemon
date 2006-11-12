if HAVE_XMMSPLUGIN
lib_LTLIBRARIES = libg15daemon_xmms_spectrum.la
else
lib_LTLIBRARIES = 
endif 
libdir = @XMMS_VISUALIZATION_PLUGIN_DIR@

AM_CFLAGS = -Wall @XMMS_CFLAGS@ @CFLAGS@ -I$(top_builddir) -I$(top_srcdir) \
         @GTK_CFLAGS@

LIBS = @XMMS_LIBS@ -L$(top_builddir)/libg15daemon_client/ 
IMCLUDES = -I$(top_builddir)/libg15daemon_client/
libg15daemon_xmms_spectrum_la_LDFLAGS = -module -avoid-version
libg15daemon_xmms_spectrum_la_SOURCES =  g15daemon_xmms_spectrum.c
libg15daemon_xmms_spectrum_la_LIBADD = $(top_builddir)/libg15daemon_client/libg15daemon_client.la -lg15render



