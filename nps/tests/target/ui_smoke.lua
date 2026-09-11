-- Runs lua/nps_v2.lua on the host against stubbed calculator globals, then drives the handlers the
-- way a user does: type, run, paint, scroll.
--
-- This exists because the UI half had no test at all. Its defects were found by opening the document
-- on the calculator, and every one of them reset the device, which costs a manual keypress to
-- recover because the reset takes Ndl with it. A scope error or a nil index is catchable here in
-- under a second.
--
-- What it cannot check is anything about the real environment: whether timer is a table on the
-- device, whether D2Editor exists, what the OS does with a handler that throws. Those still need the
-- calculator. What it does check is that the file loads, the handlers install, and a realistic
-- result paints and scrolls without raising.

local failures = 0
local checks = 0

local function check(ok, what)
    checks = checks + 1
    if not ok then
        failures = failures + 1
        print("FAIL: " .. what)
    end
end

-- The calculator globals the document touches, stubbed to the shape it expects rather than to
-- something that merely does not crash.
platform = {
    apilevel = "",
    window = {
        width = function() return 320 end,
        height = function() return 240 end,
        invalidate = function() end,
    },
}
timer = { getMilliSecCounter = function() return 1234 end }
on = {}

-- A result table shaped exactly like the one src/lua_module.cc pushes, including the before and
-- after that build_lines reads. A stub that omitted them would pass while the real thing crashed.
local fake_result = {
    outcome = "solved",
    detail = "",
    solved = true,
    status = "solved and verified",
    result = "4",
    giac_tag = "exact",
    giac = "4",
    agrees = true,
    nodes = 16,
    step_count = 4,
    rewrites = 6,
    giac_calls = 2,
    steps = {
        { kind = "plan", name = "Inverse operations on a linear equation", goal = "Isolate x",
          short = "Undo what was done to x", claim = "no claim", verified = true, failed = false,
          depth = 0 },
        { kind = "transformation", name = "Collect like terms", goal = "Collect the terms in x",
          short = "Moving every term to one side keeps both sides equal", claim = "equivalent",
          verified = true, failed = false, depth = 1,
          before = "((2 * x) + 5) = 13", after = "(2 * x) = 8", action = "subtract 5" },
        { kind = "transformation", name = "Division property of equality", goal = "Isolate x",
          short = "Dividing both sides by a non-zero number keeps them equal", claim = "equivalent",
          verified = true, failed = false, depth = 1,
          before = "(2 * x) = 8", after = "x = 4", action = "divide by 2" },
        { kind = "check", name = "check", goal = "Check the answer",
          short = "Put the answer back", claim = "solution set preserved", verified = true,
          failed = false, depth = 1,
          before = "both sides agree", after = "they agree", action = "substitution" },
    },
}

local calls = { differentiate = 0, solve = 0 }
nps_split = {
    solve = function() calls.solve = calls.solve + 1 return fake_result end,
    differentiate = function() calls.differentiate = calls.differentiate + 1 return fake_result end,
    solve_local = function() return fake_result end,
    differentiate_local = function() return fake_result end,
}
nrequire = function(name)
    if name == "luagiac" then return true end
    return nps_split
end

-- Records what was drawn, so a paint can be asserted on rather than only surviving.
local drawn = {}
local gc = {
    setFont = function() end,
    setColorRGB = function() end,
    drawRect = function() end,
    drawLine = function() end,
    getStringWidth = function(_, s) return #s * 5 end,
    drawString = function(_, s) drawn[#drawn + 1] = tostring(s) end,
}

local chunk = assert(loadfile("lua/nps_v2.lua"))
local ok, err = pcall(chunk)
check(ok, "the document loads: " .. tostring(err))
check(type(on.enterKey) == "function", "enterKey is installed")
check(type(on.paint) == "function", "paint is installed")
check(type(on.tabKey) == "function", "tabKey is installed")

on.enterKey()
check(calls.differentiate == 1, "enter calls the module once, not twice")

drawn = {}
on.paint(gc)
local painted = table.concat(drawn, "\n")
check(not painted:find("paint failed"), "paint does not raise: " .. painted:sub(1, 200))
check(painted:find("Giac agrees", 1, true) ~= nil, "the trust label is drawn")
check(painted:find("Collect like terms", 1, true) ~= nil, "a step name is drawn")
check(painted:find("(2 * x) = 8", 1, true) ~= nil, "an intermediate equation is drawn")
check(painted:find("((2 * x) + 5) = 13", 1, true) ~= nil, "the starting equation is drawn once")

-- The starting form must appear once. Showing every step's before repeats the previous after.
local _, starts = painted:gsub("%(%(2 %* x%) %+ 5%) = 13", "")
check(starts == 1, "the starting equation appears once, not once per step")

for _ = 1, 40 do on.arrowDown() end
drawn = {}
on.paint(gc)
check(not table.concat(drawn, "\n"):find("paint failed"), "scrolling past the end does not raise")
for _ = 1, 60 do on.arrowUp() end
drawn = {}
on.paint(gc)
check(not table.concat(drawn, "\n"):find("paint failed"), "scrolling back past the start is safe")

on.tabKey()
on.enterKey()
check(calls.solve == 1, "tab switches which module entry point runs")

on.clearKey()
for _, ch in ipairs({ "n", "g", "b", "y" }) do on.charIn(ch) end
on.enterKey()
check(calls.solve == 2, "every letter reaches the input line, none is swallowed as a toggle")
on.backspaceKey()
on.clearKey()
on.escapeKey()
drawn = {}
on.paint(gc)
check(not table.concat(drawn, "\n"):find("paint failed"), "an empty input paints the hint safely")

print(string.format("nps_v2 ui: %d checks, %d failed", checks, failures))
os.exit(failures == 0 and 0 or 1)
