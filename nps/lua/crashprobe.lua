-- Diagnostic document, not part of the product. Deleted once the reset is understood.
--
-- Probe 4 killed the heap theory: the allocator reported 8192 kb free at every sample including the
-- one taken immediately before the fatal call, with Lua at 58 k. There is no budget being spent.
--
-- Across five runs the fatal cross-checked call has been anywhere from the first to the fourth, and
-- which one it is moves with incidental things like whether earlier results were kept. That is not a
-- pattern our code produces. So this asks the question that separates our module from Giac entirely.
--
-- Phase A calls luagiac.caseval straight from Lua. Our module is not on the stack at all, no C
-- function of ours is running, and nothing re-enters the interpreter. Phase B then does the same
-- work through our module, which reaches caseval by lua_pcall from inside our own C function.
--
-- If A survives ten calls and B dies, the fault is in calling back into Lua from a C function, and
-- it is ours to fix. If A dies, luagiac is unreliable on its own and our module is a bystander.

platform.apilevel = '2.0'

local core_ok = pcall(nrequire, "nps_split")
local giac_ok = pcall(nrequire, "luagiac")

local shown = {}
local ran = false

-- luabridge.cc:80-83 masks IRQ and FIQ for the whole of caseval and restores them after, so the
-- duration of a call is also the length of the window in which the scheduler tick and the watchdog
-- go unserviced. The UI reported 10 ms for a whole differentiate including two of these, which would
-- put the window far below anything a watchdog would notice. That number is worth checking directly
-- before any more is built on it.
local clock = nil
if type(timer) == "table" and type(timer.getMilliSecCounter) == "function" then
    clock = timer.getMilliSecCounter
end

local function now()
    if clock then return clock() end
    return -1
end

local function t(s)
    shown[#shown + 1] = s
    if core_ok and nps_split and nps_split.trace then
        pcall(nps_split.trace, s)
    end
end

local function probe()
    t("probe5 start core=" .. tostring(core_ok) .. " giac=" .. tostring(giac_ok))
    if not giac_ok or not luagiac or not luagiac.caseval then
        t("luagiac is not loaded, phase A cannot run")
    else
        -- Phase A. Direct, from Lua, with none of our code involved. These are the same two commands
        -- our adapter builds, so Giac does the same work it does on a cross-checked call.
        for i = 1, 5 do
            local t0 = now()
            local ok, out = pcall(luagiac.caseval, "diff(x^2*sin(x),x)")
            local t1 = now()
            t("A" .. i .. " direct diff ok=" .. tostring(ok) .. " ms=" .. (t1 - t0) .. " len=" ..
              tostring(ok and out and #tostring(out) or -1))
        end
        for i = 1, 5 do
            local ok, out = pcall(luagiac.caseval, "simplify(x^2*cos(x)+2*x*sin(x)-(x^2*cos(x)+2*x*sin(x)))")
            t("A" .. (i + 5) .. " direct simplify ok=" .. tostring(ok) .. " out=" ..
              tostring(ok and out or "-"))
        end
    end

    if not core_ok then
        t("nps_split.luax.tns is not installed, phase B cannot run")
        return
    end

    -- Phase B. The same Giac work, reached by lua_pcall from inside our C function.
    for i = 1, 4 do
        local t0 = now()
        local r = nps_split.differentiate("x^2*sin(x)", "x")
        local t1 = now()
        t("B" .. i .. " via module ms=" .. (t1 - t0) .. " outcome=" .. tostring(r and r.outcome))
    end

    t("probe5 done")
end

function on.paint(gc)
    gc:setFont("sansserif", "r", 9)
    gc:setColorRGB(0, 0, 0)
    gc:drawString(ran and "crash probe 5: finished" or "crash probe 5: press enter", 3, 0, "top")
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
