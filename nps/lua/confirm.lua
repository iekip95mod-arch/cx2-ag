-- Confirmation that the resident-module fix works: luagiac.caseval must return instead of resetting.
--
-- Before the fix, luagiac.c returned from main, so crt0.S ran __cpp_fini and destroyed giac's static
-- globals right after registration. The first caseval on a fresh document reset the calculator, and
-- the UI died on its second cross-checked run. So this loads luagiac and calls caseval several times,
-- mixing trivial and real expressions, and writes each result to flash through nps_split.trace so the
-- outcome survives even a reset. If every C line reports ok=true, the fix holds across repeated calls.

platform.apilevel = '2.0'

local shown = {}
local ran = false
local core_ok = pcall(nrequire, "nps_split")

local function t(s)
    shown[#shown + 1] = s
    if core_ok and nps_split and nps_split.trace then
        pcall(nps_split.trace, s)
    end
end

local function probe()
    t("confirm start core=" .. tostring(core_ok))
    local giac_ok = luagiac ~= nil or pcall(nrequire, "luagiac")
    t("luagiac loaded=" .. tostring(giac_ok))
    if not luagiac or not luagiac.caseval then
        t("luagiac.caseval is missing")
        return
    end

    local tests = {
        "1+1",
        "2+2",
        "diff(x^2*sin(x),x)",
        "diff(x^3,x)",
        "1+1",
        "diff(sin(x),x)",
    }
    for i, cmd in ipairs(tests) do
        local ok, out = pcall(luagiac.caseval, cmd)
        t("C" .. i .. " " .. cmd .. " => ok=" .. tostring(ok) ..
          " out=" .. string.sub(tostring(out), 1, 30))
    end
    t("confirm done")
end

function on.paint(gc)
    gc:setFont("sansserif", "r", 9)
    gc:setColorRGB(0, 0, 0)
    gc:drawString(ran and "confirm: finished" or "confirm: press enter", 3, 0, "top")
    local y = 15
    for i = math.max(1, #shown - 13), #shown do
        gc:drawString(shown[i], 3, y, "top")
        y = y + 15
    end
end

function on.enterKey()
    ran = true
    local ok, err = pcall(probe)
    if not ok then
        t("probe raised: " .. tostring(err))
    end
    platform.window:invalidate()
end

on.returnKey = on.enterKey
