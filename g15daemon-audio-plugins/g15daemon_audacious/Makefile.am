lib_LTLIBRARIES = libg15daemon_audacious_spectrum.la
libdir = `pkg-config --variable=visualization_plugin_dir audacious`

AM_CFLAGS = -O3 -Wall @AUDACIOUS_CFLAGS@ @CFLAGS@ @GTK_CFLAGS@

LIBS = @AUDACIOUS_LIBS@ 
libg15daemon_audacious_spectrum_la_LDFLAGS = -module -avoid-version
libg15daemon_audacious_spectrum_la_SOURCES =  g15daemon_audacious_spectrum.c
libg15daemon_audacious_spectrum_la_LIBADD = -lg15daemon_client -lg15render



