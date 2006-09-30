LLIBDIR=@LIBDIR@


SUBDIRS =  libg15daemon_client g15daemon
INCLUDES =  -I$(top_srcdir)/libg15daemon_client -I$(top_srcdir)/g15daemon
EXTRA_DIST = debian

dist-hook:
	rm -rf `find $(distdir)/debian -name .svn`
