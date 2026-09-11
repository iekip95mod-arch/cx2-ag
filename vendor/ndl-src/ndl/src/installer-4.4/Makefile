GCC := nspire-gcc
GPP := nspire-g++
AS := nspire-as
LD := "$(shell (which arm-elf-ld arm-none-eabi-ld arm-linux-gnueabi-ld | head -1) 2>/dev/null)"
OBJCOPY := "$(shell (which arm-elf-objcopy arm-none-eabi-objcopy arm-linux-gnueabi-objcopy | head -1) 2>/dev/null)"

GCCFLAGS := -Os -Wall -Wextra -nostdlib -fPIE -DSTAGE1 -D NDL_440
GXXFLAGS := $(GCCFLAGS) -std=c++11 -fno-exceptions -fno-rtti
LFLAGS := -nostdlib -Tldscript
DISTDIR := ../../calcbin

all: $(DISTDIR)/ndl_installer_4.4.0.tns

%.o: %.c
	$(GCC) $(GCCFLAGS) -c $< -o $@
	
%.o: %.cpp
	$(GPP) $(GXXFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(GCCFLAGS) -c $< -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary $^ $@

%.bin: %.o
	$(OBJCOPY) -O binary $^ $@

ndl_installer_%.elf: stage0_%.o stage1.o utils.o
	$(LD) $(LFLAGS) $^ -o $@

Problem1_mid.xml: ndl_installer_cx.bin ndl_installer_cxcas.bin
	../tools/LuaBin/luabin ndl_installer_cx.bin - cx_s > $@
	../tools/LuaBin/luabin ndl_installer_cxcas.bin - cxcas_s >> $@

Problem1.xml: Problem1_top.xml Problem1_mid.xml Problem1_bot.xml
	cat $^ > $@

$(DISTDIR)/ndl_installer_4.4.0.tns: Problem1.xml
	mkdir -p $(DISTDIR)
	../../../ndl-sdk/tools/luna/luna Problem1.xml $@

test: $(DISTDIR)/ndl_installer_4.4.0.tns
	firebird-send $^ ndl

clean:
	rm -f Problem1.xml Problem1_mid.xml *.elf *.o *.bin $(DISTDIR)/ndl_installer_4.4.0.tns

.PHONY: test clean
