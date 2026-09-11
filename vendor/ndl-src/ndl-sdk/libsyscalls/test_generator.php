<?php
$sdk = sys_get_temp_dir() . "/ndl-generator-" . bin2hex(random_bytes(8));
mkdir($sdk . "/include", 0700, true);
mkdir($sdk . "/libsyscalls", 0700);
copy(__DIR__ . "/../include/syscall-list.h", $sdk . "/include/syscall-list.h");
copy(__DIR__ . "/mkStubs.php", $sdk . "/libsyscalls/mkStubs.php");

$failures = array();
function check_generator($condition, $message)
{
	global $failures;
	if(!$condition)
		$failures[] = $message;
}

$command = escapeshellarg(PHP_BINARY) . " " . escapeshellarg($sdk . "/libsyscalls/mkStubs.php") .
	" > " . escapeshellarg($sdk . "/include/syscall-decls.h");
passthru($command, $status);
check_generator($status === 0, "generator failed");
$declarations = file_get_contents($sdk . "/include/syscall-decls.h");
$stubs = file_get_contents($sdk . "/libsyscalls/stubs.cpp");
$list = file_get_contents($sdk . "/include/syscall-list.h");
check_generator(file_get_contents(__DIR__ . "/../include/syscall-decls.h") === $declarations,
	"checked-in declarations differ from generator output");
check_generator(file_get_contents(__DIR__ . "/stubs.cpp") === $stubs,
	"checked-in stubs differ from generator output");
$functions = array(
	"usbd_do_request_flags" => array(246, "usbd_status usbd_do_request_flags(usbd_device_handle p1, usb_device_request_t *p2, void *p3, uint16_t p4, int *p5, uint32_t p6)", true),
	"usbd_do_request_flags_pipe" => array(247, "usbd_status usbd_do_request_flags_pipe(usbd_device_handle p1, usbd_pipe_handle p2, usb_device_request_t *p3, void *p4, uint16_t p5, int *p6, uint32_t p7)", true),
	"device_get_softc" => array(259, "void* device_get_softc(device_t p1)", false),
	"device_get_ivars" => array(260, "void* device_get_ivars(device_t p1)", false),
	"luaL_checknumber" => array(107, "lua_Number luaL_checknumber(lua_State *p1, int p2)", true),
	"luaL_optnumber" => array(108, "lua_Number luaL_optnumber(lua_State *p1, int p2, lua_Number p3)", true),
	"lua_tonumber" => array(153, "lua_Number lua_tonumber(lua_State *p1, int p2)", true),
	"lua_pushnumber" => array(163, "void lua_pushnumber(lua_State *p1, lua_Number p2)", true)
);

foreach($functions as $name => $expected)
{
	list($number, $signature, $direct) = $expected;
	check_generator(preg_match('/^#define e_' . $name . ' ' . $number . ' /m', $list) === 1,
		$name . " syscall number changed");
	check_generator(strpos($declarations, $signature . ";\n") !== FALSE,
		$name . " declaration differs");
	$definition = ($direct ? "__attribute__((naked)) " : "") . $signature;
	$matched = preg_match('/^' . preg_quote($definition, '/') . '\n\{\n(.*?)^\}/ms', $stubs, $body);
	check_generator($matched === 1, $name . " definition differs");
	if($matched !== 1)
		continue;
	if($direct)
	{
		check_generator(strpos($body[1], '"swi %[nr]\\n"') !== FALSE &&
			strpos($body[1], '[nr] "i" (e_' . $name . ' | __SYSCALLS_ISVAR)') !== FALSE &&
			strpos($body[1], '"push {r0-r4, lr}\\n"') !== FALSE &&
			strpos($body[1], '"pop {r0-r4, lr}\\n"') !== FALSE &&
			strpos($body[1], '"bx r12\\n"') !== FALSE &&
			strpos($body[1], 'syscall<') === FALSE, $name . " direct ABI dispatch differs");
	}
	else
	{
		check_generator(trim($body[1]) === 'return syscall<e_' . $name . ', void*>(p1);',
			$name . " dispatch differs");
	}
}

check_generator(substr($declarations, -1) === "\n", "declarations lack final newline");
check_generator(substr($stubs, -1) === "\n", "stubs lack final newline");
passthru($command, $status);
check_generator($status === 0, "second generation failed");
check_generator(file_get_contents($sdk . "/include/syscall-decls.h") === $declarations,
	"declarations changed on second generation");
check_generator(file_get_contents($sdk . "/libsyscalls/stubs.cpp") === $stubs,
	"stubs changed on second generation");

$files = array($sdk . "/include/syscall-list.h", $sdk . "/include/syscall-decls.h",
	$sdk . "/libsyscalls/mkStubs.php", $sdk . "/libsyscalls/stubs.cpp");
echo "Removing test files:\n" . implode("\n", $files) . "\n";
foreach($files as $file)
	unlink($file);
echo "Removing test directories:\n" . $sdk . "/include\n" . $sdk . "/libsyscalls\n" . $sdk . "\n";
rmdir($sdk . "/include");
rmdir($sdk . "/libsyscalls");
rmdir($sdk);
foreach($failures as $failure)
	fwrite(STDERR, "FAIL: " . $failure . "\n");
if(count($failures))
	exit(1);
echo "Generator signatures, dispatch, Lua number ABI and repeat output pass\n";
