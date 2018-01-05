all: install

install: cmds initd rcd rccommon

cmds: $(DESTDIR)/usr/bin/linitd \
	$(DESTDIR)/usr/bin/linitctl \
	$(DESTDIR)/usr/bin/linit-start \
	$(DESTDIR)/usr/bin/linit-stop \
	$(DESTDIR)/usr/bin/linit-supd \
	$(DESTDIR)/usr/bin/linit-sup \
	$(DESTDIR)/usr/bin/linit-sup-stop

$(DESTDIR)/usr/bin/linitd: linitd.o
	install -D -m 0700 linitd.o $(DESTDIR)/usr/bin/linitd
$(DESTDIR)/usr/bin/linitctl: linitctl.o
	install -D -m 0700 linitctl.o $(DESTDIR)/usr/bin/linitctl
$(DESTDIR)/usr/bin/linit-start: linit-start.sh
	install -D -m 0700 linit-start.sh $(DESTDIR)/usr/bin/linit-start
$(DESTDIR)/usr/bin/linit-stop: linit-stop.sh
	install -D -m 0700 linit-stop.sh $(DESTDIR)/usr/bin/linit-stop
$(DESTDIR)/usr/bin/linit-supd: linit-supd.o
	install -D -m 0700 linit-supd.o $(DESTDIR)/usr/bin/linit-supd
$(DESTDIR)/usr/bin/linit-sup: linit-sup.o
	install -D -m 0700 linit-sup.o $(DESTDIR)/usr/bin/linit-sup
$(DESTDIR)/usr/bin/linit-sup-stop: linit-sup-stop.o
	install -D -m 0700 linit-sup-stop.o $(DESTDIR)/usr/bin/linit-sup-stop

define initdscript
$(DESTDIR)/etc/init.d/$(basename $(1)): scripts/init.d/$(1)
	install -D -m 0700 scripts/init.d/$(1) $(DESTDIR)/etc/init.d/$(basename $(1))

initdscriptlist += $(DESTDIR)/etc/init.d/$(basename $(1))
endef

$(foreach i,$(shell ls scripts/init.d),$(eval $(call initdscript,$(i))))

initd: $(initdscriptlist)

rcd: $(DESTDIR)/etc/rc.d/welcome

$(DESTDIR)/etc/rc.d/welcome: $(DESTDIR)/etc/init.d/welcome
	(mkdir -p $(DESTDIR)/etc/rc.d && cd $(DESTDIR)/etc/rc.d && ln -s ../init.d/welcome welcome)

rccommon: $(DESTDIR)/etc/rc.common

$(DESTDIR)/etc/rc.common: scripts/rc.common.sh
	install -D -m 0700 scripts/rc.common.sh $(DESTDIR)/etc/rc.common
