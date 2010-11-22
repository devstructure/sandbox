VERSION=2.0.0-$(LSB_RELEASE_CODENAME)1

prefix=/usr/local
bindir=${prefix}/bin
sysconfdir=${prefix}/etc
libdir=${prefix}/lib
mandir=${prefix}/share/man

DEB_BUILD_ARCH=$(shell dpkg --print-architecture)
LSB_RELEASE_CODENAME=$(shell lsb_release -c | cut -f2)

CFLAGS=-Wall -O2 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -D_FILE_OFFSET_BITS=64 -D_ATFILE_SOURCE
LDFLAGS=-lglib-2.0
PROGRAMSOURCES=\
	src/bin/sandbox-list.c \
	src/bin/sandbox-which.c \
	src/bin/sandbox-create.c \
	src/bin/sandbox-clone.c \
	src/bin/sandbox-use.c \
	src/bin/sandbox-mark.c \
	src/bin/sandbox-destroy.c
PROGRAMOBJECTS=$(PROGRAMSOURCES:.c=.o)
PROGRAMS=\
	sandbox-list \
	sandbox-which \
	sandbox-create \
	sandbox-clone \
	sandbox-use \
	sandbox-mark \
	sandbox-destroy
LIBSOURCES=\
	src/dir.c \
	src/file.c \
	src/message.c \
	src/sandbox.c \
	src/services.c \
	src/sudo.c \
	src/util.c
LIBOBJECTS=$(LIBSOURCES:.c=.o)

all: $(PROGRAMSOURCES) $(LIBSOURCES) $(PROGRAMS) sandboxfs

.c.o:
	gcc -c $(CFLAGS) $< -o $@

$(PROGRAMS): $(PROGRAMOBJECTS) $(LIBOBJECTS)
	gcc src/bin/$@.o $(LIBOBJECTS) $(LDFLAGS) -o bin/$@


sandboxfs:
	gcc $(CFLAGS) -I/usr/include/fuse src/bin/sandboxfs.c \
		-lpthread -lfuse -lrt -ldl -o bin/sandboxfs

clean:
	rm -f $(PROGRAMOBJECTS) $(LIBOBJECTS) \
		$(PROGRAMS) bin/sandboxfs

install:
	install -d $(DESTDIR)$(bindir)
	install bin/sandbox bin/sandbox-* bin/sandboxfs $(DESTDIR)$(bindir)/
	install -d $(DESTDIR)$(mandir)/man1
	install -m644 \
		man/man1/sandbox.1 man/man1/sandbox-*.1 man/man1/sandboxfs.1 \
		$(DESTDIR)$(mandir)/man1/
	install -d $(DESTDIR)$(sysconfdir)/bash_completion.d
	install -m644 etc/bash_completion.d/sandbox \
		$(DESTDIR)$(sysconfdir)/bash_completion.d/
	install -d $(DESTDIR)$(sysconfdir)/cron.d
	install -m644 etc/cron.d/sandbox $(DESTDIR)$(sysconfdir)/cron.d/
	install -d $(DESTDIR)$(sysconfdir)/profile.d
	install -m644 etc/profile.d/sandbox_prompt.sh \
		$(DESTDIR)$(sysconfdir)/profile.d/

uninstall:
	rm -f \
		$(DESTDIR)$(bindir)/sandbox \
		$(DESTDIR)$(bindir)/sandbox-* \
		$(DESTDIR)$(bindir)/sandboxfs \
		$(DESTDIR)$(mandir)/man1/sandbox.1 \
		$(DESTDIR)$(mandir)/man1/sandbox-*.1 \
		$(DESTDIR)$(mandir)/man1/sandboxfs.1 \
		$(DESTDIR)$(sysconfdir)/bash_completion.d/sandbox \
		$(DESTDIR)$(sysconfdir)/cron.d/sandbox \
		$(DESTDIR)$(sysconfdir)/profile.d/sandbox_prompt.sh
	rmdir -p --ignore-fail-on-non-empty \
		$(DESTDIR)$(bindir) \
		$(DESTDIR)$(mandir)/man1 \
		$(DESTDIR)$(sysconfdir)/bash_completion.d \
		$(DESTDIR)$(sysconfdir)/cron.d \
		$(DESTDIR)$(sysconfdir)/profile.d

deb:
	[ "$$(whoami)" = "root" ] || false
	m4 \
		-D__VERSION__=$(VERSION) \
		-D__DEB_BUILD_ARCH__=$(DEB_BUILD_ARCH) \
		control-sandbox.m4 >control
	debra create debian control
	cp postinst debian/DEBIAN/
	git archive --format=tar --prefix=debian/ HEAD | gzip >debian.tar.gz
	debra sourceinstall debian debian.tar.gz -p /usr -f --sysconfdir=/etc
	rm debian.tar.gz
	chown -R root:root debian
	debra build debian sandbox_$(VERSION)_$(DEB_BUILD_ARCH).deb
	debra destroy debian

deploy:
	scp -i ~/production.pem sandbox_$(VERSION)_$(DEB_BUILD_ARCH).deb ubuntu@packages.devstructure.com:
	ssh -i ~/production.pem -t ubuntu@packages.devstructure.com "sudo freight add sandbox_$(VERSION)_$(DEB_BUILD_ARCH).deb apt/$(LSB_RELEASE_CODENAME) && rm sandbox_$(VERSION)_$(DEB_BUILD_ARCH).deb && sudo freight cache apt/$(LSB_RELEASE_CODENAME)"

man:
	find man -name \*.ronn | xargs -n1 $(RONN) \
		--manual=Sandbox --organization=DevStructure --style=toc

gh-pages: man
	mkdir -p gh-pages
	find man -name \*.html | xargs -I__ mv __ gh-pages/
	git checkout -q gh-pages
	cp -R gh-pages/* ./
	rm -rf gh-pages
	git add .
	git commit -m "Rebuilt manual."
	git push origin gh-pages
	git checkout -q master

.PHONY: all sandboxfs clean install uninstall deb deploy man gh-pages
