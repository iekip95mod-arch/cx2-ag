# ndl API surface

Source: https://www.hackspire.org/Libndls/ plus the headers in ndl-src/ndl-sdk/include/.

## Headers available

```
ndl-src/ndl-sdk/include/
  libndls.h        the main convenience API
  os.h             OS types
  syscall.h  syscall-list.h  syscall-decls.h
  nucleus.h        Nucleus RTOS bits the OS is built on
  lua.h  lauxlib.h  lualib.h  luaconf.h
  keys.h  memory.h  hook.h  dirent.h  bsdcompat.h  usb.h  usbdi.h  ngc.h
  zehn.h
  SDL/
```

## Screen

```
scr_type_t lcd_type(void)
bool       lcd_init(scr_type_t type)
void       lcd_blit(void *buffer, scr_type_t type)
```

Screen types: SCR_320x240_4, SCR_320x240_8, SCR_320x240_16, SCR_320x240_565, SCR_240x320_565.

CX II is 320x240 16-bit, so SCR_320x240_565 is the mode of interest. Call lcd_init before any
blitting. Genzehn has a uses-lcd-blit flag to declare use of the newer API.

## Dialogs

```
unsigned int show_msgbox(const char *title, const char *msg)
unsigned int show_msgbox_2b(...)
unsigned int show_msgbox_3b(...)
int          show_msg_user_input(const char *title, const char *msg,
                                 const char *defaultvalue, char **value_ref)
int          show_1numeric_input(...)
int          show_2numeric_input(...)
void         refresh_osscr(void)
```

These are OS-native dialogs. They can support the PRD's short clarification prompts in section 26.10. Ki V4 uses the native MathEditor for two-dimensional mathematics. The earlier bespoke-renderer proposal is historical.

## Keyboard and touchpad

```
BOOL any_key_pressed(void)
BOOL isKeyPressed(key)
BOOL on_key_pressed(void)
void wait_key_pressed(void)
void wait_no_key_pressed(void)

touchpad_info_t *touchpad_getinfo(void)
int              touchpad_scan(touchpad_report_t *report)
```

## Events

```
int  get_event(struct s_ns_event *)
void send_key_event(struct s_ns_event *, unsigned short keycode, BOOL is_key_up, BOOL unknown)
void send_click_event(...)      0xFB00 single, 0xAC00 drag
void send_pad_event(...)
```

## Filesystem, memory, timing

```
int  enable_relative_paths(char **argv)
void clear_cache(void)
void idle(void)
unsigned msleep(unsigned ms)
void cfg_register_fileext(const char *ext, const char *prgm)
void bkpt(void)
void assert_ndl_rev(unsigned required_rev)
const char *get_documents_dir(void)
```

clear_cache matters if code is ever generated or patched at runtime. bkpt is an emulator
breakpoint, useful during host-side and emulator work.

NDLESS_DIR was removed from libndls.h. Use get_documents_dir() to obtain the document root and construct the application's own location from it.

## Hardware detection

```
BOOL     is_classic
BOOL     is_cm
BOOL     is_cx2
BOOL     has_colors
BOOL     is_touchpad
unsigned hwtype(void)     0 classic, 1 CX
IO()                      hardware-aware port selector macro
```

Use is_cx2 for CX II detection. The public macro checks nl_hwsubtype() against 2. hwtype() selects the older classic-versus-CX hardware grouping used by IO().

## Syscalls

359 syscalls are defined, counted from #define e_ lines in
ndl-src/ndl-sdk/include/syscall-list.h.

Addresses are per OS build. The generator mkSyscalls.php consumes one IDA .idc file per supported
OS and emits a multidimensional table used by ndl/src/resources/utils.c. The order of that
array is significant and must not be shuffled, per the comment at the top of the file.

Numbers that matter early:

```
e_malloc   5
e_free     6
e_realloc 50
```

Every allocation goes to the TI OS heap. There is no private newlib heap.

## Zehn container

Defined in ndl-src/ndl-sdk/include/zehn.h. Three executable formats exist historically: the
PRG loader with no relocation, bFLT with basic relocation and deprecated, and Zehn with full
relocation, flags and C++ exception support. Zehn is the one to use.

genzehn options, read from ndl-src/ndl-sdk/tools/genzehn/ around lines 49 to 73:

```
required:  --input <ELF>   --output <Zehn>
metadata:  --name --author --version --notice
version gating:
  --ndl-min      min ndl version times 10, so 3.1 is 31
  --ndl-max
  --ndl-rev-min  --ndl-rev-max
hardware declarations:
  --color-support      default true
  --clickpad-support   default true
  --touchpad-support   default true
  --32MB-support       default true
  --240x320-support
  --uses-lcd-blit
other: --info --include-bss --compress --verbose --help
```

--compress is worth measuring given the PRD's frozen installed-size budget in PERF-010.

Setting --32MB-support false is correct for StepCAS. The target is CX II, which has 64 MiB, and
declaring the requirement stops the program loading on a device that cannot hold it.

## The Lua extension mechanism

This is how luagiac works and it is directly relevant to the section 12.2 architecture decision.

A Lua extension is an ordinary ndl C program built to a name ending in .luax.tns. It calls
nl_lua_getstate to obtain the running OS Lua state, registers a module table into it, and then
exits as a resident program so its code stays mapped.

Complete sample, ndl-src/ndl-sdk/samples/luaext/luaextdemo.c:

```c
#include <os.h>
#include <lauxlib.h>
#include <nucleus.h>

static int hello(lua_State *L) {
	const char *param = luaL_checkstring(L, 1);
	printf("hello %s!\n", param);
	return 0;
}

static const luaL_reg lualib[] = {
	{"hello", hello},
	{NULL, NULL}
};

int main(void) {
	lua_State *L = nl_lua_getstate();
	if (!L) return 0; // not being called as Lua module
	luaL_register(L, "luaextdemo", lualib);

	// Skip cleanup on exit, this is a resident program
	_exit(0);
}
```

Its Makefile sets EXE to luaextdemo.luax and passes --240x320-support true to genzehn.

The on-device Lua is **Lua 5.1**, from ndl-src/ndl-sdk/include/lua.h lines 15 and 16, where
LUA_VERSION is "Lua 5.1" and LUA_VERSION_NUM is 501. Device scripts use the 5.1 dialect, which has no goto, integer division operator or bitwise operators. This native extension registers its functions with luaL_register. Lua 5.1 also allows a module loader to return a table, as described by [require in the Lua manual](https://www.lua.org/manual/5.1/manual.html#pdf-require).

TI's own Lua API is documented outside hackspire at http://wiki.inspired-lua.org. The hackspire
Lua page defers to it and carries little detail itself.

Lua scripts are packaged into .tns documents by luna, which is present at
ndl-src/ndl-sdk/tools/luna. The XML wrapper carries TARAL and REQAL API level attributes.
