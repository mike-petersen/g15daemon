
METASOURCES = AUTO
libdir = @G15DAEMON_PLUGIN_DIR@
AM_CFLAGS = -Wall

lib_LTLIBRARIES =g15plugin_uinput.la g15plugin_tcpserver.la g15plugin_clock.la
INCLUDES = -I$(top_builddir)/libg15daemon_client/ -I$(top_builddir)/g15daemon

g15plugin_uinput_la_SOURCES = g15_plugin_uinput.c
g15plugin_uinput_la_LDFLAGS = -avoid-version -module 

g15plugin_tcpserver_la_SOURCES = g15_plugin_net.c
g15plugin_tcpserver_la_LDFLAGS = -avoid-version -module 

g15plugin_clock_la_SOURCES = g15_plugin_clock.c
g15plugin_clock_la_LDFLAGS = -avoid-version -module 

