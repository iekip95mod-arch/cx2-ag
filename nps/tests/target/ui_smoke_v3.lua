-- Runs lua/nps_v3.lua on the host against stubbed calculator globals, then drives it the way a
-- user does: the shell's first paint, a step request typed into the input editor, the viewer's
-- keys, the detail level, save and restore.
--
-- The same reason as ui_smoke.lua: a scope error or a nil index in a handler resets the calculator
-- and takes Ndl with it, and is catchable here in under a second. What it cannot check is the
-- real environment: D2Editor, the tool palette and the class library are stubbed to the shape the
-- document uses, which is not the same as the OS honouring that shape.

local failures = 0
local checks = 0

local function check(ok, what)
    checks = checks + 1
    if not ok then
        failures = failures + 1
        print("FAIL: " .. what)
    end
end

-- The OS class library, as the ETK widgets use it: class(parent) with init on construction.
function class(parent)
    local c = {}
    c.__index = c
    setmetatable(c, {
        __index = parent,
        __call = function(cls, ...)
            local o = setmetatable({}, cls)
            if cls.init then cls.init(o, ...) end
            return o
        end,
    })
    return c
end

string.usub = string.sub

-- Records what was drawn, so a paint can be asserted on rather than only surviving.
local drawn = {}
fills = {}
local filled = 0
local gc = {
    setFont = function() end,
    setColorRGB = function() end,
    setPen = function() end,
    drawRect = function() end,
    fillRect = function(_, x, y, w, h)
        filled = filled + 1
        fills[#fills + 1] = { x = x, y = y, w = w, h = h }
    end,
    drawLine = function() end,
    drawImage = function() end,
    getStringWidth = function(_, s) return #s * 6 end,
    getStringHeight = function() return 12 end,
    drawString = function(_, s) drawn[#drawn + 1] = tostring(s) end,
}

platform = {
    apilevel = "",
    window = {
        width = function() return 320 end,
        height = function() return 240 end,
        invalidate = function() end,
    },
    withGC = function(f, ...)
        local arguments, count = {...}, select("#", ...)
        arguments[count + 1] = gc
        return f(unpack(arguments, 1, count + 1))
    end,
    registerErrorHandler = function() end,
}
check(platform.withGC(function(context, ...) return context == gc and select("#", ...) == 0 end),
      "withGC appends the context when no arguments are supplied")
check(platform.withGC(function(first, middle, last, context)
    return first == "first" and middle == nil and last == "last" and context == gc
end, "first", nil, "last"), "withGC preserves all supplied arguments including nil")
-- A clock that moves 100 ms per reading, so consecutive presses in a test are distinct presses.
local clock = 1000
timer = { getMilliSecCounter = function() clock = clock + 100 return clock end }
cursor = { set = function() end }
image = { new = function() return {} end, width = function() return 1 end, height = function() return 1 end }
local registered_menu = nil
local edit_menu = { copy = nil, paste = nil }
toolpalette = {
    register = function(m) registered_menu = m end,
    enableCopy = function(f) edit_menu.copy = f end,
    enablePaste = function(f) edit_menu.paste = f end,
}
on = {}

-- A rich text editor with the calls the document makes, holding its expression as a string.
local editors = {}
-- Every size the document hands an editor, so the ladder can be judged on what the OS would see
-- rather than on what the shell believes fsize to be.
local font_sizes = {}
D2Editor = {
    newRichText = function()
        local e = { expr = "", visible = true, focused = false }
        function e:setFontSize(size) font_sizes[#font_sizes + 1] = size end
        function e:setFocus(f) self.focused = f end
        function e:hasFocus() return self.focused end
        function e:setExpression(s) self.expr = s end
        function e:getExpression() return self.expr end
        function e:getExpressionSelection()
            local pos = self.curpos or (#self.expr - 1)
            return self.expr, pos, pos
        end
        function e:setText(s) self.expr = s end
        function e:getText() return self.expr end
        function e:setBorder() end
        function e:setBorderColor() end
        function e:move(x, y) self.x, self.y = x, y end
        function e:resize() end
        function e:registerFilter(f) self.filter = f end
        function e:setSizeChangeListener() end
        function e:setReadOnly() end
        function e:setVisible(v) self.visible = v end
        function e:setDisable2DinRT() end
        function e:createMathBox() self.expr = "\\0el {}" end
        editors[#editors + 1] = e
        return e
    end,
}

-- A result table shaped like the one src/lua_module.cc pushes for an integral, with the fields the
-- viewer reads: detail, domain, checks, rule, and the top-level assumptions.
local fake_result = {
    outcome = "integrated",
    detail = "",
    solved = true,
    status = "solved and verified",
    result = "(ln(x) + C)",
    canonical = "(C + ln(x))",
    assumptions = "x > 0",
    giac_tag = "exact",
    giac = "x^-1",
    giac_method = "differentiated the answer",
    agrees = true,
    nodes = 12,
    step_count = 4,
    rewrites = 3,
    giac_calls = 2,
    steps = {
        { kind = "plan", name = "Integrate by rule", goal = "Integrate (x^(-1)) with respect to x",
          short = "Work outwards in", claim = "no claim", verified = true, failed = false, depth = 0 },
        { kind = "transformation", name = "Logarithmic integral", goal = "Integrate (x^(-1))",
          short = "The integral of one over the variable is its natural logarithm",
          claim = "equivalent expression", verified = true, failed = false, depth = 1,
          before = "int((x^(-1)), x)", after = "ln(x)", action = "Replace the integral",
          detail = "The logarithm is only defined for a positive argument, so the restriction is recorded.",
          domain = "x > 0", rule = "i.reciprocal",
          checks = "rule-local invariant: passed, the exponent is minus one" },
        { kind = "transformation", name = "Constant of integration",
          goal = "State the general antiderivative", short = "Any constant differentiates to zero",
          claim = "equivalent expression", verified = true, failed = false, depth = 1,
          before = "ln(x)", after = "(ln(x) + C)", action = "Add the constant C",
          rule = "i.constant-of-integration" },
        { kind = "check", name = "check", goal = "Check the answer",
          short = "Differentiate the result and compare it with the integrand",
          claim = "equivalent expression", verified = true, failed = false, depth = 1,
          before = "(x^-1)", after = "(x^-1)", action = "differentiate the result by rule" },
    },
}

-- The kinematics shape differs: no canonical form, a unit on the answer, and a cross-check that is
-- Giac solving the substituted equation rather than differentiating anything.
local fake_kinematics = {
    outcome = "solved",
    detail = "",
    solved = true,
    status = "solved and verified",
    result = "v = 17 m/s",
    value = "17",
    unit = "m/s",
    equation = "(v = (v0 + (a * t)))",
    rearranged = "v = (v0 + (a * t))",
    assumptions = "acceleration is constant",
    giac_tag = "exact",
    giac_method = "solved the substituted equation",
    agrees = true,
    nodes = 20,
    step_count = 2,
    rewrites = 5,
    giac_calls = 4,
    steps = {
        { kind = "plan", name = "Constant acceleration in one dimension",
          goal = "Find v, the final velocity", short = "Use v = v0 + a*t",
          claim = "no claim", verified = true, failed = false, depth = 0 },
        { kind = "check", name = "check", goal = "Check the dimensions",
          short = "Both sides of the equation must have the same dimension",
          claim = "solution set preserved", verified = true, failed = false, depth = 1,
          before = "(v = (v0 + (a * t)))", after = "(v = (v0 + (a * t)))",
          action = "dimensional analysis of both sides", checks = "dimensional analysis: passed" },
    },
}

local calls = { differentiate = 0, integrate = 0, solve = 0, kinematics = 0, giac = 0,
                integrity = 0, manifest = 0 }
local last_args = nil
-- The split module verifies its own package before registering, so the document reads its integrity
-- status and its manifest before calling anything. A fake without these is refused, by design.
local fake_split_manifest = {
    id = "stepcas.split.inputs-sha256.0123456789abcdef",
    artifact = "split",
    schema_version = 2,
    symbolic_backend = { name = "Giac", version = "runtime-unverified",
                         interface_id = "lua5.1.luagiac.caseval-v1",
                         deployment = "external-required-unvalidated" },
}
nps_split = {
    integrity_status = function() calls.integrity = calls.integrity + 1 return "verified" end,
    capability_manifest = function() calls.manifest = calls.manifest + 1 return fake_split_manifest end,
    differentiate = function(...) calls.differentiate = calls.differentiate + 1 last_args = { ... } return fake_result end,
    integrate = function(...) calls.integrate = calls.integrate + 1 last_args = { ... } return fake_result end,
    solve = function(...) calls.solve = calls.solve + 1 last_args = { ... } return fake_result end,
    kinematics = function(...) calls.kinematics = calls.kinematics + 1 last_args = { ... } return fake_kinematics end,
}
msgboxes = {}
msgbox_answer = 1
nps_split.os_msgbox = function(title, message, first, second, third)
    msgboxes[#msgboxes + 1] = { title = title, message = message,
                                buttons = { first, second, third } }
    return msgbox_answer
end
menus = {}
menu_answer = nil
nps_split.os_menu = function(title, items)
    menus[#menus + 1] = { title = title, items = items }
    return menu_answer
end
luagiac ={ caseval = function(s) calls.giac = calls.giac + 1 return "giac(" .. s .. ")" end }
nrequire = function(name)
    if name == "luagiac" then return luagiac end
    return nps_split
end

local function painted()
    drawn = {}
    filled = 0
    on.paint(gc)
    local text = table.concat(drawn, "\n")
    check(not text:find("paint failed", 1, true), "paint does not raise: " .. text:sub(1, 200))
    return text
end

-- Types into the input editor the way the OS would leave it: a math box holding the text.
local function type_line(s)
    fctEditor.editor:setExpression("\\0el {" .. s .. "}")
end

local chunk = assert(loadfile("lua/nps_v3.lua"))
local ok, err = pcall(chunk)
check(ok, "the document loads: " .. tostring(err))
for _, name in ipairs({ "enterKey", "paint", "escapeKey", "tabKey", "backtabKey", "arrowDown",
                        "arrowUp", "arrowLeft", "arrowRight", "charIn", "save",
                        "restore" }) do
    check(type(on[name]) == "function", name .. " is installed")
end
-- The solver's step controls are not in the palette: they live on the input line, so the only group
-- ahead of Ki V1's own is Physics, whose entries insert text like every Ki V1 entry does.
check(type(registered_menu) == "table" and registered_menu[1][1] == "Physics",
      "the Physics group leads the tool palette")
check(registered_menu[2][1] == "Actions", "and the native categories follow it")

-- The native Calculator categories, in native order, after the pinned Physics box. V3 has no Steps
-- box, so they start at 2 rather than 3. Polynomials and Plots are the two sanctioned deviations:
-- native nests the first under Algebra as a submenu the palette cannot express, and has no Plots
-- category at all because graphing is the Graphs application.
do
    local expected = { "Physics", "Actions", "Number", "Algebra", "Polynomials", "Calculus",
                       "Probability", "Statistics", "Matrix & Vector", "Plots" }
    check(#registered_menu == #expected, "the palette holds " .. #expected .. " tool boxes, not " ..
          #registered_menu)
    for i, name in ipairs(expected) do
        check(registered_menu[i] and registered_menu[i][1] == name,
              "tool box " .. i .. " is " .. name)
    end

    -- Every constraint that would otherwise be discovered on the calculator. The item and box caps
    -- are the platform's, from TI's Lua guide chapter 17. 44 is a proxy for budgets' measured width,
    -- since the palette never touches our graphics context and this stub returns the same width at
    -- every font size, so a wide-glyph label still has to be checked on hardware.
    local entries = 0
    for box = 1, #registered_menu do
        local items = 0
        for item = 2, #registered_menu[box] do
            local entry = registered_menu[box][item]
            if entry ~= "-" then
                items = items + 1
                entries = entries + 1
                local first = entry[1]:byte(1)
                check(first >= 65 and first <= 90, string.format(
                      "label %d of tool box %d opens with a command rather than a word: %s",
                      item - 1, box, entry[1]))
                check(#entry[1] <= 44, string.format(
                      "label %d of tool box %d is %d characters, over the 44 budget: %s",
                      item - 1, box, #entry[1], entry[1]))
            end
        end
        check(items <= 30, string.format("tool box %d holds %d items, over the palette cap of 30",
                                         box, items))
    end
    check(#registered_menu <= 15, "the palette holds at most 15 tool boxes")
    check(entries == 158, "every entry survives the regrouping: " .. entries .. " of 158")
end
for box = 1, #registered_menu do
    check(registered_menu[box][1] ~= "Steps", "no Steps group in tool box " .. box)
end

-- The OS checks the palette as it is registered, and a name that is defined later in the file
-- is nil at that moment: "expected function in menu item 6 of tool box 1" on the emulator.
for box = 1, #registered_menu do
    for item = 2, #registered_menu[box] do
        local entry = registered_menu[box][item]
        check(entry == "-" or (type(entry) == "table" and type(entry[2]) == "function"),
              string.format("menu item %d of tool box %d is a function", item - 1, box))
    end
end

-- The shell comes up as Ki V1 does: first paint builds the view and the input editor, and asks
-- Giac its version for the label.
local text = painted()
check(inited == true and fctEditor ~= nil, "the first paint builds the shell")
check(text:find("Giac", 1, true) ~= nil, "and draws the Giac label")
check(calls.giac == 1, "after asking Giac its version once")
-- Native leaves an empty work area bare, so the shell's instruction text is gone from it.
check(text:find("Type * to enter shell", 1, true) == nil,
      "the work area carries no instruction text")

-- The step surface has its own refusal paths and none of them are Giac's, so one has to reach the
-- launch screen even though Giac itself loaded. It did not before: it waited for a typed request.
do
    local saved_error = stepSurfaceError
    stepSurfaceError = "StepCAS unavailable (integrity: sidecar missing)"
    local screen = painted()
    check(screen:find("integrity: sidecar missing", 1, true) ~= nil,
          "a step surface refusal reaches the launch screen while Giac is fine: " .. screen:sub(1, 120))
    stepSurfaceError = saved_error
end

-- The label says what the entry finds and the command lands in the input editor, so a beginner
-- reads the physics on the menu and sees the syntax only once it is theirs to edit.
for item = 2, #registered_menu[1] do
    local label, action = registered_menu[1][item][1], registered_menu[1][item][2]
    fctEditor.editor:setExpression("")
    action()
    check(fctEditor.editor:getExpression():find("!k find ", 1, true) ~= nil,
          "the Physics entry inserts a runnable kinematics line: " .. label)
    check(label:sub(1, 1) ~= "!" and label:find(";", 1, true) == nil and
          label:find("=", 1, true) == nil,
          "and its label is English rather than the command: " .. label)
end
fctEditor.editor:setExpression("")

-- A plain line still goes to Giac, and lands in the history as before.
type_line("factor(x^2-1)")
on.enterKey()
check(calls.giac == 2, "a plain line is evaluated by Giac")
check(#histME1 == 1 and #steps.histText == 1, "and added to the history")
check(steps.active == false, "without opening the viewer")

-- A step request goes to the module with the text after the prefix and the current variable.
type_line("!i 1/x")
on.enterKey()
check(calls.integrate == 1, "!i runs the integrate entry point")
check(last_args and last_args[1] == "1/x" and last_args[2] == "x", "with the text and the variable")
check(calls.giac == 2, "and not Giac")
check(#steps.histText == 2 and steps.histText[2][2]:find("ln(x)", 1, true) ~= nil,
      "the answer goes into the history like a Giac answer")
check(steps.active == true, "and the viewer opens")
check(fctEditor.editor.visible == false and histME1[1].editor.visible == false,
      "with the editors hidden underneath it")
check(histME1[1].editor.x == -10000 and fctEditor.editor.x == -10000, "and parked off screen")
check(edit_menu.copy == false and edit_menu.paste == false,
      "and the Edit menu stops offering Copy and Paste, which would act on those editors")

-- probe/native-ui/13-catalog.png: the native list carries a scrollbar when it overflows, and four
-- steps fit, so the overflow case is forced rather than waited for.
do
    local real_rows = steps.rows
    local long = {}
    for i = 1, 40 do
        long[i] = { text = "row " .. i, depth = 0, kind = "text", verified = true, step = i }
    end
    steps.rows = long
    fills = {}
    on.paint(gc)
    local bars = 0
    for _, r in ipairs(fills) do
        if r.w == 3 and r.x + r.w == 320 then bars = bars + 1 end
    end
    check(bars == 2, "the step list draws a scrollbar once the derivation runs past the screen: " ..
                     tostring(bars) .. " of the 2 fills expected")
    steps.rows = { long[1], long[2], long[3] }
    fills = {}
    on.paint(gc)
    bars = 0
    for _, r in ipairs(fills) do
        if r.w == 3 and r.x + r.w == 320 then bars = bars + 1 end
    end
    check(bars == 0, "and none when the whole derivation already fits")
    steps.rows = real_rows
end
-- The shell's size-change listeners call reposME after an entry lands, and on the emulator that
-- put every editor back over the viewer.
reposME()
check(histME2[2].editor.x == -10000, "and a reposition while the viewer is up keeps them parked")

text = painted()
check(text:find("Giac's derivative agrees", 1, true) ~= nil, "the trust label is drawn")
check(text:find("assumes: x > 0", 1, true) ~= nil, "the answer's assumptions are drawn")
check(text:find("Logarithmic integral", 1, true) ~= nil, "a step name is drawn in the list")
check(filled == 2, "the background and exactly one focused step are filled")

on.arrowDown()
on.enterKey()
text = painted()
check(text:find("Step 2 of 4: Logarithmic integral", 1, true) ~= nil, "enter opens the focused step")
check(text:find("From: int((x^(-1)), x)", 1, true) ~= nil, "with what it started from")
check(text:find("To: ln(x)", 1, true) ~= nil, "and what it produced")
check(text:find("only defined for a positive", 1, true) == nil,
      "the standard level leaves the fuller explanation out")

on.backtabKey()
text = painted()
check(text:find("only defined for a positive", 1, true) ~= nil,
      "the beginner level shows the fuller explanation")
check(text:find("of %d+ lines") ~= nil, "which is more than fits on one screen")
-- Down scrolls the open step, so the rest of it is reachable.
local seen = text
for _ = 1, 6 do
    on.arrowDown()
    seen = seen .. "\n" .. painted()
end
check(seen:find("Assumes: x > 0", 1, true) ~= nil, "and scrolling reaches the step's own restriction")
check(seen:find("Checked by: rule-local invariant", 1, true) ~= nil, "and what checked it")
check(seen:find("Rule i.reciprocal", 1, true) ~= nil, "and which rule it was")
for _ = 1, 20 do on.arrowUp() end
text = painted()
check(text:find("Step 2 of 4", 1, true) ~= nil, "scrolling up past the top is safe")

on.arrowRight()
text = painted()
check(text:find("Step 3 of 4", 1, true) ~= nil, "right moves to the next step while one is open")
for _ = 1, 10 do on.arrowRight() end
text = painted()
check(text:find("Step 4 of 4", 1, true) ~= nil, "and stops at the last one")
for _ = 1, 10 do on.arrowLeft() end
text = painted()
check(text:find("Step 1 of 4", 1, true) ~= nil, "and at the first")

-- Measured on the emulator: one press is one call to one named handler, so two presses move twice
-- however close together they land.
on.arrowRight()
text = painted()
check(text:find("Step 2 of 4", 1, true) ~= nil, "the named right handler moves too")
local saved_clock = timer.getMilliSecCounter
clock = clock + 1000
timer.getMilliSecCounter = function() return clock end
on.arrowRight()
on.arrowRight()
timer.getMilliSecCounter = saved_clock
text = painted()
check(text:find("Step 4 of 4", 1, true) ~= nil, "and two presses at the same instant move twice")
for _ = 1, 5 do on.arrowLeft() end

on.escapeKey()
text = painted()
check(text:find("Step 1 of 4", 1, true) == nil, "esc closes the step")
check(text:find("Work outwards in", 1, true) ~= nil, "back to the list at the beginner level")
on.escapeKey()
check(steps.active == false, "esc again closes the viewer")
check(edit_menu.copy == true and edit_menu.paste == true,
      "and offers Copy and Paste again once the input line is back")

-- Native esc backs out one level and never drops the document, so at the shell it does nothing.
do
    local shell_history = #steps.histText
    on.escapeKey()
    on.escapeKey()
    check(steps.active == false and fctEditor.editor.visible == true and
          #steps.histText == shell_history,
          "esc at the shell is a no-op rather than a way out of the document")
end
check(fctEditor.editor.visible == true and histME1[1].editor.visible == true,
      "and the editors come back")
check(histME1[1].editor.x ~= -10000 and fctEditor.editor.x ~= -10000, "to their places on screen")
text = painted()
check(text:find("Giac's derivative agrees", 1, true) ~= nil, "the shell shows the last verdict")

-- A prefix with nothing after it sets the mode, and every line is then a request until !g. This is
-- the input line's job now that the palette has no Steps group.
type_line("!d")
on.enterKey()
check(steps.mode == "differentiate", "a bare !d sets the d/dx mode")
check(calls.differentiate == 0, "without running anything")
type_line("x^2")
on.enterKey()
check(calls.differentiate == 1 and last_args[1] == "x^2", "the d/dx mode sends a bare line to differentiate")
on.escapeKey()
type_line("!g")
on.enterKey()
check(steps.mode == nil, "!g turns the mode off")
type_line("x^2")
on.enterKey()
check(calls.giac == 3, "and a plain line goes to Giac again")

-- Kinematics takes a structured problem rather than an expression, and its cross-check is Giac
-- solving the substituted equation, which the trust label has to name for what it is.
type_line("!k find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s")
on.enterKey()
check(calls.kinematics == 1, "!k runs the kinematics entry point")
check(last_args and last_args[1] == "find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s",
      "with everything after the prefix as one problem")
check(steps.active == true, "and the viewer opens")
check(steps.histText[#steps.histText][2]:find("v = 17 m/s", 1, true) ~= nil,
      "the answer with its unit goes into the history")
text = painted()
check(text:find("Giac's own solve agrees", 1, true) ~= nil,
      "the trust label names the check Giac ran rather than a derivative")
check(text:find("Constant acceleration in one dimension", 1, true) ~= nil,
      "and the kinematics plan is in the step list")
on.escapeKey()
on.escapeKey()

type_line("!k")
on.enterKey()
check(steps.mode == "kinematics" and calls.kinematics == 1, "a bare !k sets the kinematics mode")
type_line("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2")
on.enterKey()
check(calls.kinematics == 2, "and the mode sends a bare line to kinematics")
on.escapeKey()
on.escapeKey()

-- !! reopens the last derivation, which the palette's "show the last steps" used to do.
check(steps.active == false, "the viewer is closed")
type_line("!!")
on.enterKey()
check(steps.active == true, "!! reopens the last steps")
on.escapeKey()

type_line("!g")
on.enterKey()

-- !v changes the variable, and the next request carries it.
type_line("!v t")
on.enterKey()
check(steps.variable == "t", "!v sets the variable")
type_line("!d t^3")
on.enterKey()
check(last_args[2] == "t", "and the next request uses it")
on.escapeKey()

-- Save and restore carry the history and the reading choices, and a restore validates its input.
local saved = on.save()
check(type(saved.history) == "table" and #saved.history == #steps.histText, "save records the history")
check(saved.variable == "t", "and the variable")
check(saved.expression == nil, "and nothing for an input line the last enter already emptied")

fctEditor.editor:setExpression("\\0el {2*x+1}")
saved = on.save()
check(saved.expression == "2*x+1",
      "a line typed but not entered is saved too, unwrapped the way the enter key reads it")
fctEditor.editor:setText("")
fctEditor:fixContent()
saved = on.save()
check(saved.expression == nil, "and an empty math box is not a line worth keeping")

on.restore("not a table")
on.restore({ variable = 42, mode = "nonsense", detail = 0.5, history = "junk", expression = 42 })
check(steps.variable == "t" and steps.detail == 2, "a bad restore changes nothing")
check(steps.pendingExpression == nil, "and puts nothing on the input line either")

-- The editors' own key filter, which this harness stubbed away until now, so nothing inside it had
-- ever run here. Del removing a history entry is the shell's behaviour rather than a new binding.
check(type(fctEditor.editor.filter) == "table", "the input editor registers a key filter")
check(type(histME1[1].editor.filter) == "table", "and so does a history entry")

local entries = #steps.histText
check(histME1[1].editor.filter.deleteKey() == true, "del on a history entry is taken by the editor")
check(#steps.histText == entries - 1 and #histME1 == entries - 1 and #histME2 == entries - 1,
      "and removes the entry and its result together")

entries = #steps.histText
check(histME1[1].editor.filter.backspaceKey() == true, "backspace on a history entry is taken too")
check(#steps.histText == entries - 1, "and removes one entry")

-- On the input line those two keys are the OS's, so the history must not move under them.
entries = #steps.histText
fctEditor.editor:setExpression("\\0el {x+1}")
check(fctEditor.editor.filter.deleteKey() == true, "del at the end of the input line is swallowed")
fctEditor.editor.curpos = 7
check(fctEditor.editor.filter.deleteKey() == false, "del inside it is left to the OS")
check(fctEditor.editor.filter.backspaceKey() == false, "and so is backspace")
fctEditor.editor.curpos = nil
check(#steps.histText == entries, "and neither of them touches the history")

entries = #steps.histText
check(histME1[1].editor.filter.clearKey() == true, "clear on a history entry is taken by the editor")
check(#steps.histText == entries - 1, "and removes that entry the way backspace does")

fctEditor.editor:setExpression("\\0el {x+1}")
check(fctEditor.editor.filter.clearKey() == true, "clear on the input line is taken too")
check(fctEditor.editor:getExpression() == "\\0el {}", "and leaves an empty box rather than no box")

-- A native form cycles focus and wraps at both ends rather than dead-ending on the last field.
check(steps.active == false and #theView.focusList > 1,
      "the shell owns the screen and has more than one focusable widget")
tabStart = theView:getFocus()
on.tabKey()
check(theView:getFocus() ~= tabStart, "tab moves the focus off the widget holding it")
for _ = 2, #theView.focusList do on.tabKey() end
check(theView:getFocus() == tabStart, "and a full lap wraps back to where it started")
on.backtabKey()
check(theView:getFocus() ~= tabStart, "backtab moves it the other way")
for _ = 2, #theView.focusList do on.backtabKey() end
check(theView:getFocus() == tabStart, "and a full lap backwards wraps too")

reset()
check(#histME1 == 0 and #steps.histText == 0, "clear history empties both lists")
on.restore({ variable = "y", mode = "solve", detail = 1, history = { { " a", " b" }, { 1, 2 }, "x" },
             expression = "3*x" })
check(steps.variable == "y" and steps.mode == "solve" and steps.detail == 1,
      "a good restore brings back the choices")
check(steps.pendingHistory and #steps.pendingHistory == 1, "and keeps only the well formed history")
check(steps.pendingExpression == "3*x", "and the line the student had not entered yet")
check(fctEditor:getExpression() == "", "which is not on screen before a paint has run")
text = painted()
check(#histME1 == 1 and steps.pendingHistory == nil, "which the next paint puts back on screen")
check(steps.pendingExpression == nil and fctEditor:getExpression() == "3*x",
      "and the same paint puts the unentered line back where the student left it")

type_line("!?")
on.enterKey()
check(steps.histText[#steps.histText][2]:find("v0 is the starting speed", 1, true) ~= nil,
      "the help says what the kinematics symbols mean, for a student who does not know them")

steps.status = "StepCAS module is outdated or incomplete (missing caseval)"
painted()
do
    local corner = nil
    for _, line in ipairs(drawn) do
        if line:sub(1, 7) == "StepCAS" then corner = line end
    end
    check(corner ~= nil and #corner == 50 and corner:sub(-3) == "...",
          "a status too long for the corner is cut instead of running off the edge: " ..
              tostring(corner))
end
steps.status = nil

-- Clear History is the one destructive entry in the palette, so it asks first. Escape returns the
-- last button and focus opens on the first, both measured, so only the middle one clears.
function menuAction(menu, box, label)
    for _, group in ipairs(menu) do
        if group[1] == box then
            for i = 2, #group do
                if type(group[i]) == "table" and group[i][1] == label then return group[i][2] end
            end
        end
    end
end

clearAction = menuAction(registered_menu, "Actions", "Clear History")
check(type(clearAction) == "function", "the Actions box carries a Clear History entry")

reset()
addME(" 1+1", " 2")
addME(" 2+2", " 4")
check(#steps.histText == 2, "two entries to lose")

msgbox_answer = 1
msgbox_count = #msgboxes
clearAction()
check(#steps.histText == 2 and #msgboxes == msgbox_count + 1,
      "Keep History, which is where the focus opens, leaves the history alone")
check(msgboxes[#msgboxes].buttons[1] == "Keep History" and
      msgboxes[#msgboxes].buttons[2] == "Clear History" and
      msgboxes[#msgboxes].buttons[3] == "Cancel",
      "and the destructive answer sits between the two buttons a reflex reaches")

msgbox_answer = 3
clearAction()
check(#steps.histText == 2, "escape, which the OS returns as the last button, leaves it alone too")

msgbox_answer = 2
clearAction()
check(#steps.histText == 0, "and the middle button, one deliberate tab away, clears it")

-- A module too old to carry the dialog cannot ask, and what it cannot ask about it must not destroy.
nps_split.os_msgbox = nil
addME(" 3+3", " 6")
msgbox_count = #msgboxes
clearAction()
check(#msgboxes == msgbox_count, "a module with no dialog asks nothing")
check(#steps.histText == 2 and steps.histText[1][1] == " 3+3",
      "and clears nothing, since a build with no confirm is the build that needed one")
check(steps.histText[2][2]:find("nothing was cleared", 1, true) ~= nil,
      "and says so where the student is already looking")

-- Ctrl+menu on a history entry, the one native key the document was silent on. The framework menu
-- opens its focus on item 1 and returns nil for a dismissal, both measured on the emulator.
reset()
addME(" 5+5", " 10")
addME(" 6+6", " 12")
theView:setFocus(histME1[1])
menu_count = #menus
menu_answer = nil
on.contextMenu()
check(#menus == menu_count + 1, "ctrl+menu on a history entry opens a menu")
check(menus[#menus] and menus[#menus].items[1] == "Copy to Entry Line" and
      menus[#menus].items[2] == "Delete Entry",
      "with the reversible action on the item the focus opens on")
check(#steps.histText == 2 and fctEditor:getExpression() == "",
      "and a dismissal, which the framework returns as nil, does nothing at all")

menu_answer = 1
theView:setFocus(histME1[1])
on.contextMenu()
check(fctEditor:getExpression() ~= "", "copy puts the entry on the input line")
check(#steps.histText == 2, "and leaves the history where it was")

fctEditor.editor:setText("")
fctEditor:fixContent()
menu_answer = 2
theView:setFocus(histME1[1])
on.contextMenu()
check(#steps.histText == 1, "delete removes the entry the menu was opened on")

-- The input line already has the OS's own cut, copy and paste, so a menu there would duplicate them.
theView:setFocus(fctEditor)
menu_count = #menus
on.contextMenu()
check(#menus == menu_count, "ctrl+menu on the input line opens nothing")

nps_split.os_menu = nil
theView:setFocus(histME1[1])
menu_count = #menus
on.contextMenu()
check(#menus == menu_count and #steps.histText == 1,
      "and a module too old to draw a menu neither opens one nor acts as though it had")

-- The traversal V4 bounded in #9 was here too, and tabBackward was worse: it read getFocus().visible
-- with no nil guard, so an empty focus list raised rather than recursing.
do
    local function focusable()
        local o = { acceptsFocus = true, visible = true, x = 0, y = 0, w = 10, h = 10, dx1 = 0, dy1 = 0 }
        function o:repos() end
        function o:resize() end
        function o:setFocus() self.focused = true end
        function o:releaseFocus() self.focused = false end
        function o:contains() return false end
        return o
    end
    local tabs = View({ invalidate = function() end })
    tabs:add(focusable())
    tabs:add(focusable())
    tabs:add(focusable())
    -- Read the order out of the list rather than assuming it. View:add inserts at the front.
    local one, two, three = tabs.focusList[1], tabs.focusList[2], tabs.focusList[3]
    tabs:setFocus(one)
    tabs:hide(two)
    check(not two.visible, "View:hide is what writes visible, and it is reachable in V3 too")
    tabs:tabForward()
    check(tabs:getFocus() == three, "tabbing forward steps over a hidden widget rather than onto it")
    tabs:show(two)
    tabs:setFocus(one)
    tabs:tabForward()
    check(tabs:getFocus() == two, "and lands on it again once it is shown")
    tabs:setFocus(three)
    tabs:hide(two)
    tabs:tabBackward()
    check(tabs:getFocus() == one, "tabbing backward steps over it too")
    tabs:hide(one)
    tabs:hide(three)
    tabs:setFocus(one)
    check(pcall(tabs.tabForward, tabs) and pcall(tabs.tabBackward, tabs),
          "tabbing with every widget hidden returns instead of recursing forever")
    local empty = View({ invalidate = function() end })
    check(pcall(empty.tabForward, empty) and pcall(empty.tabBackward, empty),
          "and an empty focus list is refused quietly rather than indexing a nil focus")
end

-- TI documents the handheld as accepting 7, 9, 10, 11, 12, 16 or 24 and nothing else:
-- https://education.ti.com/html/eguides/nspire/EG_Nspire/EN/content/eg_lua/m_libraries/2deditorlib/setfontsize.HTML
do
    local accepted = { [7] = true, [9] = true, [10] = true, [11] = true, [12] = true, [16] = true, [24] = true }
    local walked = #font_sizes

    local climbed = {}
    fsize = 7
    for _ = 1, 8 do fontUp() climbed[#climbed + 1] = fsize end
    check(table.concat(climbed, ",") == "9,10,11,12,16,24,24,24",
          "Increase Font Size climbs the accepted sizes and stops at the largest: " .. table.concat(climbed, ","))

    local fell = {}
    fsize = 24
    for _ = 1, 8 do fontDown() fell[#fell + 1] = fsize end
    check(table.concat(fell, ",") == "16,12,11,10,9,7,7,7",
          "Decrease Font Size falls through the accepted sizes and stops at the smallest: " .. table.concat(fell, ","))

    fsize = 13
    fontDown()
    local off_ladder = fsize
    fsize = 13
    fontUp()
    check(off_ladder == 12 and fsize == 16,
          "a size off the accepted ladder steps onto it rather than past it: " ..
          off_ladder .. " and " .. fsize)

    -- The walk above is worth nothing unless something reached the editor, so count the calls as well
    -- as read them. Every earlier paint and history row has already put sizes in this list.
    check(#font_sizes > walked, "the font menu reaches the editor at all")
    local rejected = nil
    for _, size in ipairs(font_sizes) do
        if size ~= nil and not accepted[size] then rejected = size end
    end
    check(not rejected, "every size this document hands an editor is one the handheld accepts, got " ..
          tostring(rejected))
    fsize = 12
end

print(string.format("nps_v3 ui: %d checks, %d failed", checks, failures))
os.exit(failures == 0 and 0 or 1)
