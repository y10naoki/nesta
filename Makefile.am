noinst_PROGRAMS=nesta
nesta_SOURCES=main.c \
              config.c \
              dynlib.c \
              http_server.c \
              document.c \
              command.c \
              log.c \
              srelay_server.c \
              http_server.h \
              nesta.vcproj
nesta_CFLAGS = -I. -I@NESTALIB_HEADERS@

EXTRA_DIR = conf logs public_html samples

DISTCLEANFILES = *~

dist-hook:
	for subdir in $(EXTRA_DIR); do \
		cp -rp $$subdir $(distdir); \
		rm -f $(distdir)/$$subdir/access_log*.txt; \
		rm -f $(distdir)/$$subdir/error.txt; \
		rm -f $(distdir)/$$subdir/output.txt; \
		rm -f $(distdir)/$$subdir/*~; \
	done
