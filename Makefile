name = wiipresent
version = $(shell awk '/^Version: / {print $$2}' $(name).spec)

prefix = /usr
sysconfdir = /etc
bindir = $(prefix)/bin
datadir = $(prefix)/share
mandir = $(datadir)/man

CC = cc
CFLAGS = -Wall -O2
OPTFLAGS = -I /usr/include/libcwiimote -D_ENABLE_TILT -D_ENABLE_FORCE -D_DISABLE_BLOCKING_UPDATE
LDFLAGS= -lm -lX11 -lXtst -lcwiimote -lbluetooth

.PHONY: all install docs clean

all: wiipresent docs

wiipresent: wiipresent.c
	$(CC) $(CFLAGS) $(OPTFLAGS) $(LDFLAGS) wiipresent.c -o wiipresent

install:
	install -Dp -m0755 wiipresent $(DESTDIR)$(bindir)/wiipresent
	install -Dp -m0644 docs/wiipresent.1 $(DESTDIR)$(mandir)/man1/wiipresent.1

docs:
	$(MAKE) -C docs docs

docs-install:
	$(MAKE) -C docs install

clean:
	rm -f wiipresent
	$(MAKE) -C docs clean

dist: clean
	$(MAKE) -C docs dist
	find . ! -wholename '*/.svn*' | pax -d -w -x ustar -s ,^,$(name)-$(version)/, | bzip2 >../$(name)-$(version).tar.bz2

rpm: dist
	rpmbuild -tb --clean --rmsource --rmspec --define "_rpmfilename %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm" --define "_rpmdir ../" --define "debug_package %nil" ../$(name)-$(version).tar.bz2

srpm: dist
	rpmbuild -ts --clean --rmsource --rmspec --define "_rpmfilename %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm" --define "_srcrpmdir ../" --define "debug_package %nil" ../$(name)-$(version).tar.bz2
