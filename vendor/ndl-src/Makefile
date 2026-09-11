# Check for submodules
ifneq ($(words $(wildcard ndl-sdk/thirdparty/nspire-io/Makefile) \
               $(wildcard ndl-sdk/thirdparty/zlib/configure) \
               $(wildcard ndl-sdk/tools/luna/Makefile) \
               $(wildcard ndl-sdk/thirdparty/freetype2/Makefile)),4)
        $(error Run `git submodule init' and `git submodule update' to checkout the submodules)
endif

SUBDIRS = ndl-sdk ndl

all: $(patsubst %, build-%, $(SUBDIRS))
clean: $(patsubst %, clean-%, $(SUBDIRS))

build-ndl: build-ndl-sdk

.PHONY: all clean

build-%: %
	$(MAKE) -C $<

clean-%: %
	$(MAKE) -C $< clean

dist: all
	rm -rf dist
	mkdir -p dist/ndl/samples
	mkdir -p dist/ndl/persistent
	cp Mozilla-Public-License-v1.1.html README.md dist/ndl/
	cp ndl/calcbin/* dist/ndl/
	mv dist/ndl/persistent_*.tns dist/ndl/persistent/
	rm dist/ndl/downgradefix_3.9*.tns dist/ndl/ndl_installer_3.9.0_classic.tns
	cp ndl-sdk/samples/*/*.tns dist/ndl/samples/
	rm dist/ndl/samples/freetype_demo.tns
	cd dist/ndl && 7z a ../ndl.zip .
