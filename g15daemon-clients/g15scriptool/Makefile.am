
AM_CFLAGS = -O3 -Wall @CFLAGS@ @GLIB_CFLAGS@ -I$(top_builddir) -I$(top_srcdir) 
METASOURCES = AUTO
bin_PROGRAMS = g15scriptool

docdir = $(prefix)/share/doc/$(PACKAGE)-$(VERSION)
doc_DATA = README ChangeLog AUTHORS NEWS COPYING

LIBS = -lg15daemon_client -lg15render @GLIB_LIBS@
IMCLUDES = -I
g15scriptool_LDFLAGS = -avoid-version
g15scriptool_SOURCES =  g15scriptool.c

install-exec-hook:
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15bar
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15bignum
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15box
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15circle
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15clear
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15flush
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15inkey
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15line
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15new
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15print
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15quit
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15roundbox
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15setpixel
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15waitkey
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15wbmpicon
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15wbmploadbuf
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15wbmpsplash
	ln -sf ${prefix}/bin/g15scriptool ${prefix}/bin/g15wbmpsprite
	
uninstall-hook:
	rm -f ${prefix}/bin/g15bar
	rm -f ${prefix}/bin/g15bignum
	rm -f ${prefix}/bin/g15box
	rm -f ${prefix}/bin/g15circle
	rm -f ${prefix}/bin/g15clear
	rm -f ${prefix}/bin/g15flush
	rm -f ${prefix}/bin/g15inkey
	rm -f ${prefix}/bin/g15line
	rm -f ${prefix}/bin/g15new
	rm -f ${prefix}/bin/g15print
	rm -f ${prefix}/bin/g15quit
	rm -f ${prefix}/bin/g15roundbox
	rm -f ${prefix}/bin/g15setpixel
	rm -f ${prefix}/bin/g15waitkey
	rm -f ${prefix}/bin/g15wbmpicon
	rm -f ${prefix}/bin/g15wbmploadbuf
	rm -f ${prefix}/bin/g15wbmpsplash
	rm -f ${prefix}/bin/g15wbmpsprite



	

