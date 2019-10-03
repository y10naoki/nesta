SAMPLES_SRC = hello.c \
              tpl_hello.c \
              tpl_hello2.c \
              session.c \
              sendmail.c \
              oci_univ.c

noinst_PROGRAMS = nxsamples.so
nxsamples_so_SOURCES = $(SAMPLES_SRC)
nxsamples_so_CFLAGS = -fPIC -DPIC -I. -I@NESTALIB_HEADERS@ -I@OCI_HEADERS@

EXTRA_DIR = templates testdata

DISTCLEANFILES = *~

dist-hook:
	for subdir in $(EXTRA_DIR); do \
		cp -rp $$subdir $(distdir); \
		rm -f $(distdir)/$$subdir/*~; \
	done
