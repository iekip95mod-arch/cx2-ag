package.cpath = arg[1] .. ";" .. package.cpath
local nps = require("nps_split")
local capabilities = {}
for _, entry in ipairs(nps.capability_manifest().installed_modules) do capabilities[entry.id] = true end
assert(capabilities["calculus.derivative.single-variable"], "failure diagnostics must retain actual solver capabilities")
if arg[2] == "disabled" then
    assert(nps.ui_menu_test_failure == nil and nps.ui_menu_test_status == nil,
           "release interface must not expose failure controls")
    assert(not capabilities["ui.retained.failure-injection"], "release manifest must not advertise failure controls")
    print("retained failure controls absent")
    return
end
assert(capabilities["ui.retained.failure-injection"], "diagnostic manifest must identify its failure controls")
assert(type(nps.ui_menu_test_failure) == "function" and type(nps.ui_menu_test_status) == "function",
       "diagnostic interface must expose failure controls")
for _, invalid in ipairs({"", "unknown", 1, {}, true, "resource\0ignored"}) do
    assert(not pcall(nps.ui_menu_test_failure, invalid), "invalid failure mode must be rejected")
end
assert(nps.ui_menu_test_status() == 0, "invalid arguments must leave the renderer healthy")
nps.ui_menu_test_failure(arg[2])
assert(not nps.ui_menu_open(320, 240, "Templates", {"Fraction"}), "injected creation failure must return normally")
local status, attempts = nps.ui_menu_test_status()
assert(status == (arg[2] == "assert" and 4 or 3), "failure classification must survive the Lua boundary")
local encoded, healthy = nps.ui_menu_frame()
assert(encoded == nil and healthy == false, "failed renderer must not publish a frame")
assert(not nps.ui_menu_select(1) and not nps.ui_menu_scroll(48), "failed renderer must refuse navigation")
nps.ui_menu_close()
assert(not nps.ui_menu_open(320, 240, "Templates", {"Fraction"}), "failed pool must remain inert")
local later_status, later_attempts = nps.ui_menu_test_status()
assert(later_status == status and later_attempts == attempts, "cleanup and retry must not allocate in a failed pool")
assert(nps.canonical("x") == "x", "native expression handling must survive renderer failure")
print("retained " .. arg[2] .. " failure contained through the diagnostic Lua interface")
