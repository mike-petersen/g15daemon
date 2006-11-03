LLIBDIR=@LIBDIR@


SUBDIRS =  libg15daemon_client g15daemon
INCLUDES =  -I$(top_srcdir)/libg15daemon_client -I$(top_srcdir)/g15daemon
EXTRA_DIST = debian contrib Documentation g15daemon_xmms lang-bindings patches rpm README.usage FAQ LICENSE

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
doc_DATA = FAQ README.usage README ChangeLog TODO AUTHORS NEWS LICENSE

man1_MANS = $(top_srcdir)/Documentation/g15daemon.1
man3_MANS = $(top_srcdir)/Documentation/g15daemon_client_devel.3

dist-hook:
	rm -rf `find $(distdir)/debian -name .svn`

