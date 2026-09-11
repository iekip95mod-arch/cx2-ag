GCC := nspire-gcc
GPP := nspire-g++
AS := nspire-as
LD := "$(shell (which arm-elf-ld arm-none-eabi-ld arm-linux-gnueabi-ld | head -1) 2>/dev/null)"
OBJCOPY := "$(shell (which arm-elf-objcopy arm-none-eabi-objcopy arm-linux-gnueabi-objcopy | head -1) 2>/dev/null)"

GCCFLAGS := -Os -Wall -W -Wno-strict-aliasing -nostdlib -DSTAGE1 -DNDL_36 -fPIE -I ../resources
GPPFLAGS := $(GCCFLAGS) -std=c++11 -fno-exceptions -fno-rtti
LDFLAGS := -nostdlib -Tldscript
DISTDIR := ../../calcbin
vpath %.tns $(DISTDIR)

all: ndl_installer_3.6.tns

%.o: %.c
	$(GCC) $(GCCFLAGS) -c $<

%.o: %.cpp
	$(GPP) $(GPPFLAGS) -c $<

%.o: %.S
	$(AS) $(GCCFLAGS) -c $<

%.elf: %.o
	$(LD) $(LDFLAGS) $^ -o $@

ndl_installer.elf: stage1.o stage2.o utils.o
	$(LD) $(LDFLAGS) stage2.o utils.o -o $@

ndl_installer.bin: ndl_installer.elf
	$(OBJCOPY) -O binary $< $@

stage0.bin: stage0.o
	$(OBJCOPY) -O binary $< $@

ndl_installer.lua: ndl_installer.bin stage0.bin
	../tools/EscapeInstaller/EscapeInst $< $(<:.bin=-escaped.bin)
	cat stage0.bin $(<:.bin=-escaped.bin) > $(<:.bin=-escaped.bin.tmp)
	mv $(<:.bin=-escaped.bin.tmp) $(<:.bin=-escaped.bin)
	../tools/MakeLuaInst/MakeLuaInst $(<:.bin=-escaped.bin) $(<:.bin=-escaped.lua.tmp)
	@# skip the UTF-8 BOM marker
	cat ndl_version.lua ndl_installer.lua.tpl $(<:.bin=-escaped.lua.tmp) > $@
	rm $(<:.bin=-escaped.lua.tmp) $(<:.bin=-escaped.bin)

ndl_installer_3.6.tns: ndl_installer.lua
	@mkdir -p $(DISTDIR)
	../../../ndl-sdk/tools/luna/luna $< $(DISTDIR)/$@

clean:
	rm -rf ndl_installer.elf $(INST_OBJS)
	rm -rf *.o *.elf stage0.bin ndl_installer.bin ndl_installer.lua *.tmp
