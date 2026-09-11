-- Ki V2 crashes the calculator on the second consecutive run. This isolates the one variable that
-- matters first: whether the crash needs Giac.
--
-- Each enter press calls differentiate_local, which parses, applies the rules and builds the
-- derivation with no backend call at all. Everything else is held the same: same module, same
-- expression, same step walk. If this survives many presses the fault is in the Giac path, and if it
-- dies on the second press the fault is in ours.
--
-- Deliberately plain. A probe that draws a step list or keeps history could be the thing that
-- crashes, and then it has proved nothing.

platform.apilevel = '2.0'

local core_ok, core_err = pcall(nrequire, "nps_split")

local runs = 0
local last = "not run yet"
local worst = ""

local function step_total(r)
    local n = 0
    for _ in pairs(r.steps or {}) do n = n + 1 end
    return n
end

local function once()
    runs = runs + 1
    if not core_ok then
        last = "no module: " .. tostring(core_err)
        return
    end
    local r = nps_split.differentiate_local("x^2*sin(x)", "x")
    if not r then
        last = "returned nil"
        return
    end
    -- Read every field the real UI reads, including the ones added most recently, so this exercises
    -- the same bridge code rather than a cheaper path through it.
    local touched = 0
    for _, s in ipairs(r.steps or {}) do
        touched = touched + #tostring(s.name) + #tostring(s.goal) + #tostring(s.before or "")
                  + #tostring(s.after or "") + #tostring(s.action or "")
    end
    last = string.format("ok  steps %d  nodes %s  chars %d", step_total(r), tostring(r.nodes),
                         touched)
    worst = tostring(r.result)
end

local function paint(gc)
    gc:setFont("sansserif", "r", 10)
    gc:setColorRGB(0, 0, 0)
    gc:drawString("probe: differentiate_local, no Giac", 3, 0, "top")
    gc:drawString("press enter repeatedly", 3, 18, "top")
    gc:drawString("runs: " .. runs, 3, 40, "top")
    gc:drawString(last, 3, 58, "top")
    gc:drawString("heap " .. math.floor(collectgarbage("count")) .. "k", 3, 76, "top")
    gc:drawString(worst, 3, 94, "top")
end

function on.paint(gc)
    local ok, err = pcall(paint, gc)
    if not ok then
        gc:setColorRGB(180, 0, 0)
        gc:drawString("paint failed: " .. tostring(err), 2, 2, "top")
    end
end

local function guard(f)
    return function(...)
        local ok, err = pcall(f, ...)
        if not ok then last = "error: " .. tostring(err) end
        platform.window:invalidate()
    end
end

on.enterKey = guard(once)
on.returnKey = on.enterKey
on.escapeKey = guard(function() end)
on.resize = guard(function() end)
