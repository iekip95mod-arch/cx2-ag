SDK ?= $(abspath ../../vendor/ndl-src/ndl-sdk)
export PATH := $(SDK)/bin:$(PATH)
export NDL_ZEHN_PATH := $(SDK)/tools/zehn_loader

CFLAGS := -Os -Wall -Wextra -marm
HOST_CC ?= cc
HOST_FLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
LUA ?= luajit
PROGRAMS := Prepare Hide Unlock Status Proof Inventory HideAll RestoreAll Relocate ArmBoot BridgeUpdate LayoutSwitch calc_helpers.luax

.PHONY: all test
.SECONDARY:

all: $(addprefix build/,$(addsuffix .tns,$(PROGRAMS))) build/current_document.tns build/test_app.tns build/BridgeProbe.tns

build:
	mkdir -p $@

build/Prepare.elf: MODE = 0
build/Hide.elf: MODE = 1
build/Unlock.elf: MODE = 2
build/Status.elf: MODE = 3

build/Prepare.elf build/Hide.elf build/Unlock.elf build/Status.elf: hide_probe.c | build
	$(SDK)/bin/nspire-gcc $(CFLAGS) -DMODE=$(MODE) $< -o $@

build/Proof.elf: proof.c | build
	$(SDK)/bin/nspire-gcc $(CFLAGS) $< -o $@

build/Inventory.elf: ACTION = 0
build/HideAll.elf: ACTION = 1
build/RestoreAll.elf: ACTION = 2

build/Inventory.elf build/HideAll.elf build/RestoreAll.elf: storage_tool.c storage.c storage.h factory-6.4/factory_manifest.h | build
	$(SDK)/bin/nspire-gcc $(CFLAGS) -DACTION=$(ACTION) storage_tool.c storage.c -o $@

build/Relocate.elf: relocate.c | build
	$(SDK)/bin/nspire-gcc $(CFLAGS) $< -o $@

build/ArmBoot.elf: arm_boot.c | build
	$(SDK)/bin/nspire-gcc $(CFLAGS) $< -o $@

build/BridgeUpdate.elf: PROBE_MODE = 0
build/LayoutSwitch.elf: PROBE_MODE = 1

build/BridgeUpdate.elf build/LayoutSwitch.elf: runtime_probe.c | build
	$(SDK)/bin/nspire-gcc $(CFLAGS) -DPROBE_MODE=$(PROBE_MODE) $< -o $@

build/calc_helpers.luax.elf: calc_helpers.c storage.c storage.h factory-6.4/factory_manifest.h | build
	$(SDK)/bin/nspire-gcc $(CFLAGS) calc_helpers.c storage.c -o $@

build/%.zehn: build/%.elf
	$(SDK)/bin/genzehn --input $< --output $@ --name $* --240x320-support true

build/%.tns: build/%.zehn
	$(SDK)/bin/make-prg $< $@

build/current_document.tns: current_document.lua | build
	$(SDK)/tools/luna/luna $< $@

build/test_app.tns: test_app.lua | build
	$(SDK)/tools/luna/luna $< $@

build/BridgeProbe.tns: bridge_probe.lua | build
	$(SDK)/tools/luna/luna $< $@

build/storage_test: storage.c storage.h storage_test.c factory-6.4/factory_manifest.h | build
	$(HOST_CC) $(HOST_FLAGS) storage.c storage_test.c -o $@

build/relocate_test: relocate_test.c relocate.c tests/os.h | build
	$(HOST_CC) $(HOST_FLAGS) -Itests relocate_test.c -o $@

build/arm_boot_test: arm_boot_test.c arm_boot.c tests/os.h | build
	$(HOST_CC) $(HOST_FLAGS) -Itests arm_boot_test.c -o $@

build/bridge_update_test: PROBE_MODE = 0
build/layout_switch_test: PROBE_MODE = 1

build/bridge_update_test build/layout_switch_test: runtime_probe_test.c runtime_probe.c tests/os.h | build
	$(HOST_CC) $(HOST_FLAGS) -DPROBE_MODE=$(PROBE_MODE) -Itests runtime_probe_test.c -o $@

test: build/storage_test build/relocate_test build/arm_boot_test build/bridge_update_test build/layout_switch_test
	./build/storage_test $(CURDIR)/factory-6.4
	./build/relocate_test
	./build/arm_boot_test
	./build/bridge_update_test
	./build/layout_switch_test
	$(LUA) calculator_test.lua
