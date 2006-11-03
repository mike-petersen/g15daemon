LLIBDIR=@LIBDIR@


SUBDIRS =  libg15daemon_client g15daemon
INCLUDES =  -I$(top_srcdir)/libg15daemon_client -I$(top_srcdir)/g15daemon
EXTRA_DIST = debian

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
doc_DATA = FAQ README.usage README ChangeLog TODO AUTHORS NEWS LICENSE 

man1_MANS = Documentation/g15daemon.1

dist-hook:
	rm -rf `find $(distdir)/debian -name .svn`

