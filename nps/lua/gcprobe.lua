-- Does a raise inside nps_split.solve leave the collector stopped? solve pauses the collector for the
-- length of the call and restarts it from a destructor, and luaL_checkstring raising on a non-string
-- argument is a longjmp that skips destructors. So: make it raise, then allocate a known amount of
-- garbage and watch the heap count. A running collector keeps the count near where it started; a
-- stopped one lets it climb by the whole amount allocated.

platform.apilevel = '2.0'

local shown = {}
local ran = false
local core_ok = pcall(nrequire, "nps_split")

local function t(s) shown[#shown + 1] = s end

local function churn()
    local before = collectgarbage("count")
    local peak = before
    for i = 1, 4000 do
        local s = string.rep("x", 60) .. i
        local c = collectgarbage("count")
        if c > peak then peak = c end
    end
    return before, peak
end

local function probe()
    t("core loaded=" .. tostring(core_ok))
    if not core_ok then return end

    local b0, p0 = churn()
    t(string.format("baseline: start %dk peak %dk rise %dk", b0, p0, p0 - b0))

    local ok, err = pcall(nps_split.solve, nil)
    t("solve(nil) raised=" .. tostring(not ok) .. " " .. string.sub(tostring(err), 1, 40))

    local b1, p1 = churn()
    t(string.format("after raise: start %dk peak %dk rise %dk", b1, p1, p1 - b1))

    local ok2, r = pcall(nps_split.solve_local, "2x + 5 = 13")
    t("solve after raise ok=" .. tostring(ok2) .. " result=" .. tostring(ok2 and r.result))
    t("gcprobe done")
end

function on.paint(gc)
    gc:setFont("sansserif", "r", 9)
    gc:setColorRGB(0, 0, 0)
    gc:drawString(ran and "gcprobe: finished" or "gcprobe: press enter", 3, 0, "top")
    local y = 15
    for i = math.max(1, #shown - 13), #shown do
        gc:drawString(shown[i], 3, y, "top")
        y = y + 15
    end
end

function on.enterKey()
    ran = true
    local ok, err = pcall(probe)
    if not ok then t("probe raised: " .. tostring(err)) end
    platform.window:invalidate()
end

on.returnKey = on.enterKey
