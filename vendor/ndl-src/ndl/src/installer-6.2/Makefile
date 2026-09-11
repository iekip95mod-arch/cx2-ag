AS := nspire-as
LD := "$(shell (which arm-elf-ld arm-none-eabi-ld arm-linux-gnueabi-ld | head -1) 2>/dev/null)"
OBJCOPY := "$(shell (which arm-elf-objcopy arm-none-eabi-objcopy arm-linux-gnueabi-objcopy | head -1) 2>/dev/null)"

LFLAGS := -nostdlib
DISTDIR := ../../calcbin

all: $(DISTDIR)/ndl_installer_4.5.5-6.2.0-6.4.0.tns

%.o: %.S
	$(AS) $(GCCFLAGS) -c $< -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary $^ $@

ndl_installer.elf: stage0.o
	$(LD) $(LFLAGS) $^ -o $@

installer_payload.lua: ndl_installer.bin
	../tools/LuaBin/luabin ndl_installer.bin - installer noescape > $@

Problem1.xml: installer_payload.lua installer.lua gui.lua ipc.lua template.sed Problem1_template.xml
	sed -f template.sed Problem1_template.xml | sed -f template.sed | sed -f template.sed > Problem1.xml

$(DISTDIR)/ndl_installer_%.tns: Problem1.xml
	mkdir -p $(DISTDIR)
	../../../ndl-sdk/tools/luna/luna Problem1.xml $@

test: $(DISTDIR)/ndl_installer_4.5.5-6.2.0-6.4.0.tns
	firebird-send $^ ndl

clean:
	rm -f Problem1.xml installer_payload.lua *.elf *.o *.bin $(DISTDIR)/ndl_installer_4.5.5-6.2.0.tns

.PHONY: test clean
