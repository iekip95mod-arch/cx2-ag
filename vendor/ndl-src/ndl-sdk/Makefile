SUBDIRS = libsyscalls libndls thirdparty tools samples system

all: $(patsubst %, build-%, $(SUBDIRS))
clean: $(patsubst %, clean-%, $(SUBDIRS))

build-libndls: build-libsyscalls
build-tools: build-thirdparty
build-samples: build-libndls build-tools build-thirdparty build-system
build-thirdparty: build-libsyscalls

.PHONY: all clean

build-%: %
	+$(MAKE) -C $<

clean-%: %
	+$(MAKE) -C $< clean

docker: docker-arm-gcc docker-ndl-sdk

docker-arm-gcc:Dockerfile.arm-gcc
	@# works around https://github.com/docker/docker/issues/2112
	cp Dockerfile.arm-gcc Dockerfile
	sudo docker build -t ndl/arm-gcc .
	rm Dockerfile

docker-ndl-sdk: Dockerfile.ndl-sdk
	@# works around https://github.com/docker/docker/issues/2112
	cp Dockerfile.ndl-sdk Dockerfile
	sudo docker build -t ndl/sdk .
	rm Dockerfile

