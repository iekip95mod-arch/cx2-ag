-- Ki V2. The UI half of PRD section 12.2's native-core split: this draws and takes keys, and every
-- piece of mathematics happens in nps_split.luax.tns. No expression is parsed here and no Giac command
-- is built here, which is section 12.3's trust boundary drawn at the language edge.

-- Without this the OS runs the script at its oldest API level, where platform.window is not the
-- object the rest of this file assumes, and the first paint takes the calculator down with it.
platform.apilevel = '2.0'

local core_ok = pcall(nrequire, "nps_split")
local giac_ok = pcall(nrequire, "luagiac")

local MODES = {
    { key = "differentiate", label = "d/dx", hint = "x^2*sin(x)" },
    { key = "solve",         label = "solve", hint = "2x + 5 = 13" },
}

local state = {
    input = "x^2*sin(x)",
    variable = "x",
    mode = 1,
    result = nil,
    error = nil,
    scroll = 0,
    editing = true,
}

local LINE = 15
local MARGIN = 3

-- The OS's millisecond clock, checked rather than assumed. khicas.lua calls
-- timer.getMilliSecCounter() at this same apilevel, but reading that in another file is not evidence
-- that the global is a table here, and indexing the wrong thing unwinds into the OS and resets the
-- calculator instead of raising something a pcall can catch. clock_kind is carried to the screen so
-- one run says what the global actually is.
local clock_kind = type(timer)
local clock = nil
if clock_kind == "table" and type(timer.getMilliSecCounter) == "function" then
    clock = timer.getMilliSecCounter
end

local function now()
    if clock then return clock() end
    return -1
end

local function mode() return MODES[state.mode] end

-- A step contributes more than one line: its rule and goal, then the expression it produced. The
-- record is a tree of steps and the screen is a list of lines, so flatten once when the result
-- arrives rather than deriving rows from steps on every paint.
--
-- Each line says whether it is prose or an expression. That is what lets the expression lines go to
-- the 2D math renderer while the prose lines stay ordinary strings.
--
-- Only the first step's before is shown. Every later one repeats the previous step's after, so
-- printing both would show each intermediate form twice and read as a list of pairs rather than as
-- a derivation running down the page.
local function build_lines(r)
    local lines = {}
    local started = false
    for _, s in ipairs(r.steps or {}) do
        lines[#lines + 1] = { kind = "text", text = s.name .. ": " .. s.goal, depth = s.depth,
                              verified = s.verified, failed = s.failed }
        if not started and s.before and s.before ~= "" then
            lines[#lines + 1] = { kind = "math", text = s.before, depth = s.depth + 1,
                                  verified = s.verified, failed = s.failed }
            started = true
        end
        if s.after and s.after ~= "" then
            lines[#lines + 1] = { kind = "math", text = s.after, depth = s.depth + 1,
                                  verified = s.verified, failed = s.failed }
            started = true
        end
    end
    return lines
end

local function run()
    state.error = nil
    state.result = nil
    state.lines = nil
    state.scroll = 0

    if not core_ok then
        state.error = "nps_split.luax.tns is not installed in the ndl folder"
        return
    end
    if state.input == "" then
        state.error = "nothing to work on yet"
        return
    end

    -- PERF-006 wants total latency on target hardware, and PERF-011 says an isolated microbenchmark
    -- cannot qualify it: it has to be the release configuration with the UI, the parser, Giac and
    -- the derivation all resident. That is this, so the clock goes around the call a user waits on.
    --
    -- PERF-005's first-step number came from calling the _local entry point alongside this one and
    -- timing it separately. That is measured and recorded, and the second call is gone, because
    -- doubling the work on every press to re-measure a number that does not change is a cost the
    -- release path should not carry.
    local t0 = now()
    local r, why = nps_split[mode().key](state.input, state.variable)
    local t1 = now()
    if not r then
        state.error = why
        return
    end
    r.total_ms = t1 - t0
    r.heap_kb = math.floor(collectgarbage("count"))
    state.runs = (state.runs or 0) + 1
    r.runs = state.runs

    state.result = r
    state.lines = build_lines(r)
end

-- The answer line carries its own trust, because section 17 wants an unverified result labelled
-- rather than presented like a checked one.
local function verdict(r)
    if r.agrees == true then return "Giac agrees" end
    if r.agrees == false then return "GIAC DISAGREES" end
    if r.giac_tag == "unavailable" then return "not cross-checked" end
    -- A reply the adapter could not use is worth showing verbatim. "malformed result" on its own
    -- says the check did not happen and not why, which is what makes it unfixable from the screen.
    if r.giac_tag and r.giac_raw and r.giac_raw ~= "" then
        return "Giac: " .. r.giac_tag .. " <" .. r.giac_raw .. ">"
    end
    if r.giac_tag then return "Giac: " .. r.giac_tag end
    return "not cross-checked"
end

local function visible_rows(h)
    return math.max(1, math.floor((h - 6 * LINE) / LINE))
end

local function paint(gc)
    local w = platform.window:width()
    local h = platform.window:height()
    gc:setFont("sansserif", "r", 9)

    gc:setColorRGB(0, 0, 0)
    gc:drawString("Ki V2  " .. mode().label .. "  (tab switches, enter runs)", MARGIN, 0, "top")

    -- Input line
    local y = LINE
    gc:setColorRGB(150, 150, 150)
    gc:drawRect(MARGIN, y, w - 2 * MARGIN, LINE)
    gc:setColorRGB(0, 0, 0)
    local shown = state.input
    if state.editing then shown = shown .. "|" end
    gc:drawString(shown, MARGIN + 3, y + 1, "top")

    y = y + LINE + 2

    if state.error then
        gc:setColorRGB(180, 0, 0)
        gc:drawString(state.error, MARGIN, y, "top")
        gc:setColorRGB(0, 0, 0)
        gc:drawString("hint: try " .. mode().hint, MARGIN, y + LINE, "top")
        return
    end

    local r = state.result
    if not r then
        gc:setColorRGB(90, 90, 90)
        gc:drawString("type an expression and press enter", MARGIN, y, "top")
        if not giac_ok then
            gc:drawString("luagiac not loaded, so nothing will be cross-checked", MARGIN, y + LINE, "top")
        end
        return
    end

    local answer = r.canonical or r.result or r.outcome
    gc:setColorRGB(0, 0, 0)
    gc:drawString(answer, MARGIN, y, "top")
    y = y + LINE

    if r.agrees == false then gc:setColorRGB(180, 0, 0) else gc:setColorRGB(90, 90, 90) end
    local note = r.outcome .. "  |  " .. verdict(r)
    if not r.solved and r.detail and r.detail ~= "" then note = note .. "  |  " .. r.detail end
    gc:drawString(note, MARGIN, y, "top")
    y = y + LINE

    -- The measurement line. total is the whole call including the cross-check, and the counts are
    -- the ones PERF-010 freezes budgets against.
    gc:setColorRGB(60, 60, 140)
    local timing = clock and string.format("%d ms", r.total_ms or -1)
                         or ("no clock, timer is a " .. clock_kind)
    gc:drawString(string.format("run %d  %s  nodes %d  steps %d  giac %d  heap %dk",
                                r.runs or -1, timing, r.nodes or -1, r.step_count or -1,
                                r.giac_calls or -1, r.heap_kb or -1),
                  MARGIN, y, "top")
    y = y + LINE

    gc:setColorRGB(200, 200, 200)
    gc:drawLine(MARGIN, y, w - MARGIN, y)
    y = y + 2

    local rows = visible_rows(h)
    local lines = state.lines or {}
    for i = 1, rows do
        local ln = lines[i + state.scroll]
        if not ln then break end
        local indent = MARGIN + ln.depth * 10
        if ln.failed then
            gc:setColorRGB(180, 0, 0)
        elseif ln.kind == "math" then
            gc:setColorRGB(0, 0, 140)
        elseif ln.verified then
            gc:setColorRGB(0, 0, 0)
        else
            gc:setColorRGB(120, 120, 120)
        end
        local mark = (ln.kind == "text" and not ln.verified) and "  (unverified)" or ""
        gc:drawString(ln.text .. mark, indent, y, "top")
        y = y + LINE
    end

    if r.steps_truncated then
        gc:setColorRGB(180, 0, 0)
        gc:drawString("step list cut short: the record was too deep to hand over", MARGIN, y, "top")
    end

    if #lines > rows then
        gc:setColorRGB(120, 120, 120)
        gc:drawString(string.format("%d-%d of %d", state.scroll + 1,
                                    math.min(state.scroll + rows, #lines), #lines),
                      w - 70, h - LINE, "top")
    end
end

-- An error inside a handler resets the calculator, which leaves nothing to read and loses the
-- message that says where it happened. Trapped, it becomes a line on screen instead.
function on.paint(gc)
    local ok, err = pcall(paint, gc)
    if not ok then
        gc:setFont("sansserif", "r", 7)
        gc:setColorRGB(180, 0, 0)
        gc:drawString("paint failed: " .. tostring(err), 2, 2, "top")
    end
end

-- Every handler is installed through this. An error inside one unwinds into the OS and takes the
-- calculator down with it, and a reset leaves nothing on screen to read.
local function guard(f)
    return function(...)
        local ok, err = pcall(f, ...)
        if not ok then
            state.result = nil
            state.error = tostring(err)
        end
        platform.window:invalidate()
    end
end

on.backspaceKey = guard(function()
    state.input = string.sub(state.input, 1, -2)
    state.editing = true
end)

on.deleteKey = on.backspaceKey

on.clearKey = guard(function()
    state.input = ""
    state.editing = true
end)

on.enterKey = guard(function()
    state.editing = false
    run()
end)

on.returnKey = on.enterKey

on.tabKey = guard(function()
    state.mode = state.mode % #MODES + 1
    state.input = mode().hint
    state.result = nil
    state.error = nil
    state.editing = true
end)

on.escapeKey = guard(function()
    state.result = nil
    state.error = nil
    state.editing = true
end)

on.arrowDown = guard(function()
    local lines = state.lines or {}
    local rows = visible_rows(platform.window:height())
    if state.scroll + rows < #lines then
        state.scroll = state.scroll + 1
    end
end)

on.arrowUp = guard(function()
    if state.scroll > 0 then
        state.scroll = state.scroll - 1
    end
end)

on.charIn = guard(function(ch)
    state.editing = true
    state.input = state.input .. ch
end)
