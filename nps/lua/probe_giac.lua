-- The crash needs Giac: probe_repeat ran the whole rule engine and bridge eight times with no
-- backend and did not fault. This splits the remaining question in two.
--
-- enter calls luagiac.caseval directly, with no StepCAS in the picture at all.
-- tab   calls nps_split.differentiate, which is the same work plus our cross_check around Giac.
--
-- If enter dies too, the limit is Giac's or the document's and we design around it. If enter
-- survives and tab dies, the fault is in how src/lua_module.cc drives the backend, which is ours.

platform.apilevel = '2.0'

local core_ok = pcall(nrequire, "nps_split")
local giac_ok = pcall(nrequire, "luagiac")

local raw_runs = 0
local ours_runs = 0
local last = "nothing yet"

local function raw()
    raw_runs = raw_runs + 1
    if not giac_ok or type(luagiac) ~= "table" then
        last = "luagiac is a " .. type(luagiac)
        return
    end
    -- Two calls, because our cross_check makes two per run and one per run would not reach the
    -- same count. The second asks the question the comparison asks.
    local a = luagiac.caseval("diff(((x)^(2)*sin(x)),x)")
    local b = luagiac.caseval("simplify((2*x*sin(x)+x^2*cos(x))-(2*x*sin(x)+x^2*cos(x)))")
    last = "raw " .. raw_runs .. ": " .. tostring(a):sub(1, 26) .. " | " .. tostring(b):sub(1, 8)
end

local function ours()
    ours_runs = ours_runs + 1
    if not core_ok then
        last = "no nps_split module"
        return
    end
    local r = nps_split.differentiate("x^2*sin(x)", "x")
    if not r then
        last = "ours " .. ours_runs .. ": nil"
        return
    end
    last = string.format("ours %d: %s giac=%s agrees=%s calls=%s", ours_runs,
                         tostring(r.outcome), tostring(r.giac_tag), tostring(r.agrees),
                         tostring(r.giac_calls))
end

-- ki_v2 resets on its second run even with the step list not drawn at all, and this document
-- survives six runs of the same module call. build_lines is the ingredient ki_v2 has that this does
-- not, so it goes here, on its own key, against a document already proven stable.
--
-- The result table is also kept in a local across presses, the way ki_v2 keeps state.result, because
-- holding the previous run's tables live while the module builds the next one is the other thing
-- ki_v2 does differently.
local held = nil
local lines_runs = 0

local function build_lines(r)
    local lines = {}
    local started = false
    for _, s in ipairs(r.steps or {}) do
        lines[#lines + 1] = { kind = "text", text = s.name .. ": " .. s.goal, depth = s.depth,
                              verified = s.verified, failed = s.failed }
        if not started and s.before and s.before ~= "" then
            lines[#lines + 1] = { kind = "math", text = s.before, depth = s.depth + 1 }
            started = true
        end
        if s.after and s.after ~= "" then
            lines[#lines + 1] = { kind = "math", text = s.after, depth = s.depth + 1 }
            started = true
        end
    end
    return lines
end

local function ours_with_lines()
    lines_runs = lines_runs + 1
    if not core_ok then
        last = "no nps_split module"
        return
    end
    held = nil
    local r = nps_split.differentiate("x^2*sin(x)", "x")
    if not r then
        last = "lines " .. lines_runs .. ": nil"
        return
    end
    local lines = build_lines(r)
    held = { result = r, lines = lines }
    last = string.format("lines %d: %s  lines=%d", lines_runs, tostring(r.outcome), #lines)
end

local function paint(gc)
    gc:setFont("sansserif", "r", 9)
    gc:setColorRGB(0, 0, 0)
    gc:drawString("enter: raw caseval x2   tab: differentiate", 3, 0, "top")
    gc:drawString("right: differentiate + build_lines + hold", 3, 15, "top")
    gc:drawString(string.format("raw %d  ours %d  lines %d", raw_runs, ours_runs, lines_runs),
                  3, 36, "top")
    gc:drawString(last, 3, 54, "top")
    gc:drawString("heap " .. math.floor(collectgarbage("count")) .. "k", 3, 72, "top")
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

on.enterKey = guard(raw)
on.returnKey = on.enterKey
on.tabKey = guard(ours)
on.escapeKey = guard(function() end)
on.resize = guard(function() end)

on.arrowRight = guard(ours_with_lines)
