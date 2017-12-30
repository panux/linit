all:
	$(MAKE) -C src

install: all
	$(MAKE) -C src -f inst.mk install

clean:
	$(MAKE) -C src clean
