-- Does a bad argument leave the collector stopped? solve_into pauses the collector for the length
-- of the call, and luaL_checkstring raises through it. A raise is a longjmp, so the C++ destructor
-- that restarts the collector never runs if the check comes after the pause. Measured rather than
-- argued: the same garbage is allocated before and after the bad call, and the heap growth is
-- compared. A collector that is running holds the second growth near the first, and a stopped one
-- lets it climb to the whole amount allocated.

platform.apilevel = '2.0'

local shown = {}
local ran = false
local core_ok = pcall(nrequire, "nps_split")

local function t(s) shown[#shown + 1] = s end

-- 160 strings of 1 KB, dropped as they are made, so the total is about 160 KB of garbage.
local function growth()
    local before = collectgarbage("count")
    for i = 1, 160 do
        local s = string.rep(string.char(64 + i % 26), 1024)
        s = nil
    end
    return math.floor(collectgarbage("count") - before)
end

local function probe()
    t("gcpause core=" .. tostring(core_ok))
    if not core_ok or not nps_split or not nps_split.solve then
        t("nps_split.solve is missing")
        return
    end
    local first = growth()
    t("growth before bad call: " .. first .. "k")
    local ok, err = pcall(nps_split.solve, nil)
    t("solve(nil): ok=" .. tostring(ok) .. " " .. string.sub(tostring(err), 1, 40))
    local second = growth()
    t("growth after bad call: " .. second .. "k")
    local ok2, r = pcall(nps_split.solve, "2x + 5 = 13")
    t("solve after: ok=" .. tostring(ok2) .. " result=" .. tostring(ok2 and r and r.result))
    local third = growth()
    t("growth after good call: " .. third .. "k")
    t(second > first * 4 and "VERDICT: collector left stopped" or "VERDICT: collector running")
end

function on.paint(gc)
    gc:setFont("sansserif", "r", 9)
    gc:setColorRGB(0, 0, 0)
    gc:drawString(ran and "gcpause: finished" or "gcpause: press enter", 3, 0, "top")
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
