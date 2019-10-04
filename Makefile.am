noinst_PROGRAMS=nesta

nesta_SOURCES=src/main.c \
              src/config.c \
              src/dynlib.c \
              src/http_server.c \
              src/document.c \
              src/command.c \
              src/log.c \
              src/srelay_server.c \
              src/http_server.h

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
