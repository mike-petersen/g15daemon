if HAVE_XMMSPLUGIN
lib_LTLIBRARIES = libg15daemon_xmms_spectrum.la
else
lib_LTLIBRARIES = 
endif 
libdir = @XMMS_VISUALIZATION_PLUGIN_DIR@

AM_CFLAGS = -O3 -Wall @XMMS_CFLAGS@ @CFLAGS@ -I$(top_builddir) -I$(top_srcdir) \
         @GTK_CFLAGS@

LIBS = @XMMS_LIBS@  
IMCLUDES = -I
libg15daemon_xmms_spectrum_la_LDFLAGS = -module -avoid-version
libg15daemon_xmms_spectrum_la_SOURCES =  g15daemon_xmms_spectrum.c
libg15daemon_xmms_spectrum_la_LIBADD = -lg15daemon_client -lg15render



