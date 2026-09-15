-- Runs lua/nps_v4.lua on the host against stubbed calculator globals, then drives it the way a
-- user does: the shell's first paint, a step request typed into the input editor, the viewer's
-- keys, hint progression, the detail level, save and restore.
--
-- The one thing this checks that ui_smoke_v3.lua cannot is that V4 asks for a single module. The
-- fake nrequire refuses every name but "nps_nspire", so a document that still reached for luagiac or for
-- nps_split would fall back to the Nspire engine and fail the checks below rather than quietly
-- working here and failing on a calculator that has neither file.
--
-- The same reason as ui_smoke.lua: a scope error or a nil index in a handler resets the calculator
-- and takes Ndl with it, and is catchable here in under a second. What it cannot check is the
-- real environment: D2Editor, the tool palette and the class library are stubbed to the shape the
-- document uses, which is not the same as the OS honouring that shape.
--
-- Two traps in this file, both of which pass while proving nothing.
--
-- Every document loaded through loadIsolated writes its handlers into the same global `on` table,
-- because its environment chains to _G. So `some_env.on.paint` is whichever document loaded last,
-- not the one the variable is named after. A check that paints an isolated document has to run
-- immediately after the load it judges. A loop over four saved environments painted the last one
-- four times and matched its message once by luck, for three green checks that tested nothing.
--
-- This chunk sits at LuaJIT's ceiling of 200 locals in one function. A new top-level `local` fails
-- the whole file with "main function has more than 200 local variables", which names neither the
-- line that added it nor the limit's cause. Scope new work in do...end or hang it on a global.

local failures = 0
local checks = 0
local evidence_records = {}

local function check(ok, what)
    checks = checks + 1
    if not ok then
        failures = failures + 1
        print("FAIL: " .. what)
    end
end

local function evidence(requirement, ok, what)
    check(ok, what)
    evidence_records[#evidence_records + 1] = {
        requirement = requirement, passed = ok, what = what,
    }
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

function string.usub(s, first, last)
    local offsets = {}
    for offset in s:gmatch("()[^\128-\191]") do offsets[#offsets + 1] = offset end
    local count = #offsets
    first, last = first or 1, last or count
    if first < 0 then first = count + first + 1 end
    if last < 0 then last = count + last + 1 end
    first, last = math.max(1, first), math.min(count, last)
    if first > last then return "" end
    return s:sub(offsets[first], (offsets[last + 1] or #s + 1) - 1)
end

-- Records what was drawn, so a paint can be asserted on rather than only surviving.
local drawn = {}
local draw_calls = {}
fills = {}
local filled = 0
local font_styles = {}
local gc = {
    setFont = function(_, _, style) font_styles[#font_styles + 1] = style end,
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
    drawString = function(_, s, x, y)
        drawn[#drawn + 1] = tostring(s)
        draw_calls[#draw_calls + 1] = { text = tostring(s), x = x, y = y }
    end,
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
edit_menu = { copy = nil, paste = nil }
toolpalette = {
    register = function(m) registered_menu = m end,
    enableCopy = function(f) edit_menu.copy = f end,
    enablePaste = function(f) edit_menu.paste = f end,
}
on = {}

-- A rich text editor with the calls the document makes, holding its expression as a string.
--
-- The size change listener is driven here rather than left inert, because the viewer's two-pass
-- layout, its font step-down and its fall back to text all hang off the size the OS reports, and a
-- stub that reports nothing leaves every one of those paths unrun while the checks still pass. The
-- model is a caricature of what the emulator measured: a fraction is two lines tall, everything
-- else is one, and width goes with the character count and the font.
local editors = {}
D2Editor = {
    newRichText = function()
        local e = { expr = "", visible = true, focused = false, font = 9 }
        -- Inlined rather than hoisted to a local: this chunk is at LuaJIT's ceiling of 200 locals
        -- and one more fails the whole file.
        --
        -- Measured on the emulator (benchmarks/d2_probe5.lua): a 250 wide box holding a long unit
        -- quantity reported 446 by 36 at font 9 and 237 by 56 at font 7, so whether the OS breaks
        -- an expression across rows depends on where it can break it and is not something a caller
        -- gets to rely on. This reports the unbroken width every time, which is the pessimistic
        -- half of what the OS does. A case the OS would have broken is sent to the text fallback
        -- here rather than shown, which costs a box that could have been drawn and never certifies
        -- one that would have been cut off.
        function e:announceSize()
            if not self.onSize then return end
            if self.deferSize then return end
            local body = self.expr
            if not self.plain and body:sub(1, 6) == "\\0el {" and body:sub(-1) == "}" then
                body = body:sub(7, -2)
            end
            local scale = (self.font or 9) / 9
            self.natural = math.floor(#body * 6 * scale)
            self.needH = math.floor((body:find("/", 1, true) and 26 or 13) * scale)
            if self.plain and self.wrapWidth == 0 and self.boxw then
                local line, widest, rows = 0, 0, 1
                for word in body:gmatch("%S+%s*") do
                    local width = #word * 6 * scale
                    if line > 0 and line + width > self.boxw then
                        widest, line, rows = math.max(widest, line), 0, rows + 1
                    end
                    line = line + width
                end
                self.natural = math.max(widest, line)
                self.needH = math.ceil(rows * 13 * scale)
            elseif not self.plain and self.mathHeight then
                self.needH = self.mathHeight
            end
            self.measuredAt = self.boxw
            self.onSize(self, self.natural, self.needH)
        end
        function e:setFontSize(n)
            self.font = n or self.font
            self:announceSize()
        end
        function e:setFocus(f) self.focused = f end
        function e:hasFocus() return self.focused end
        -- The caret position is modelled, not discarded. A template is only a template if it puts
        -- the caret inside the shape it made, and a stub that always answered "at the end" would
        -- let a template that forgot to do that pass.
        function e:setExpression(s, pos)
            self.plain = false
            self.expr = s
            self.curpos = pos
            self:announceSize()
        end
        function e:getExpression() return self.expr end
        function e:getExpressionSelection()
            local pos = self.curpos or (string.ulen(self.expr) - 1)
            return self.expr, pos, pos
        end
        function e:setText(s) self.expr, self.plain = s, true self:announceSize() end
        function e:setWordWrapWidth(w) self.wrapWidth = w self:announceSize() end
        function e:getText() return self.expr end
        function e:setBorder(border) self.border = border end
        function e:setBorderColor(color) self.borderColor = color end
        function e:move(x, y) self.x, self.y = x, y end
        -- Records the width but does not announce from here. The OS reports when the content's
        -- size changes, not because a frame was resized, and the viewer always sets the width
        -- before the expression so the announcement that matters already sees it.
        function e:resize(w, h)
            self.boxw, self.boxh = w, h
            if w <= 0 or h <= 0 then self.invalidResize = true end
        end
        function e:registerFilter(f) self.filter = f end
        function e:setSizeChangeListener(f) self.onSize = f end
        function e:setReadOnly(v) self.readOnly = v end
        function e:setVisible(v) self.visible = v end
        function e:setDisable2DinRT() end
        function e:createMathBox() self.expr = "\\0el {}" end
        editors[#editors + 1] = e
        return e
    end,
}

-- Finds a read-only math box that is on screen and holds this text. Global rather than local
-- because of the 200-local ceiling above. The position test is load bearing: the shell's history
-- editors are read-only too and hold the same wrapper, and the viewer parks them at -10000 while
-- it is up, so a box that answers here is one the viewer placed rather than one it failed to hide.
-- A box smaller than what it reported does not answer here, because on the emulator that box is
-- truncated rather than scrolled or ellipsised. 0.00000250 m^3 reported 81 wide in a 240 wide box
-- and placed at 81 it drew as 0.00000250 with the m^3 pushed onto a row the height then cut off.
-- Without this a check can pass on a box the reader cannot read.
-- The width it was measured at is a separate condition from the width it reported. The reported
-- width is the ink, and the emulator drew 0.00000250 m^3 as 0.00000250 in a box of exactly that
-- ink, so a frame that was big enough to measure in is the only frame the measurement describes.
function mathBoxFits(e)
    return e.boxw and e.boxh and e.natural and e.needH and e.measuredAt
           and e.boxw >= e.natural and e.boxh >= e.needH and e.boxw >= e.measuredAt
end

function mathBoxShowing(text)
    for _, e in ipairs(editors) do
        if e.readOnly and e.visible and e.x and e.x > -1000 and e.expr
           and mathBoxFits(e) and e.expr:find(text, 1, true) then
            return e
        end
    end
    return nil
end

-- The whole content of a box rather than a substring. Three boxes are on screen at once and the
-- answer often contains the same subexpression as the step's, so a substring match can be
-- satisfied by the wrong one and prove nothing about the box under test.
function mathBoxExact(body)
    for _, e in ipairs(editors) do
        if e.readOnly and e.visible and e.x and e.x > -1000 and mathBoxFits(e)
           and e.expr == "\\0el {" .. body .. "}" then
            return e
        end
    end
    return nil
end

-- A result table shaped like the one src/lua_module.cc pushes for an integral, with the fields the
-- viewer reads: detail, domain, checks, rule, and the top-level assumptions.
local fake_result = {
    outcome = "integrated",
    detail = "",
    solved = true,
    status = "solved and verified",
    result_form = "elementary closed form",
    result = "(ln(x) + C)",
    canonical = "(C + ln(x))",
    original_expression = "1/x",
    normalized_expression = "(x^(-1))",
    assumptions = "x > 0",
    giac_tag = "exact",
    giac = "x^-1",
    giac_method = "differentiated the answer",
    agrees = true,
    nodes = 12,
    child_slots = 7,
    step_count = 4,
    rewrites = 3,
    giac_calls = 2,
    steps = {
        { kind = "plan", phase = "plan", name = "Integrate by rule", goal = "Integrate (x^(-1)) with respect to x",
          short = "Work outwards in", claim = "no claim", verified = true, failed = false, depth = 0 },
        { kind = "transformation", phase = "solve", name = "Logarithmic integral", goal = "Integrate (x^(-1))",
          short = "The integral of one over the variable is its natural logarithm",
          claim = "equivalent expression", verified = true, failed = false, depth = 1,
          before = "int((x^(-1)), x)", after = "ln(x)", action = "Replace the integral",
          detail = "The logarithm is only defined for a positive argument, so the restriction is recorded.",
          domain = "x > 0", rule = "i.reciprocal",
          checks = "rule-local invariant: passed, the exponent is minus one" },
        { kind = "transformation", phase = "solve", name = "Constant of integration",
          goal = "State the general antiderivative", short = "Any constant differentiates to zero",
          claim = "equivalent expression", verified = true, failed = false, depth = 1,
          before = "ln(x)", after = "(ln(x) + C)", action = "Add the constant C",
          rule = "i.constant-of-integration" },
        { kind = "check", phase = "check", name = "check", goal = "Check the answer",
          short = "Differentiate the result and compare it with the integrand",
          claim = "equivalent expression", verified = true, failed = false, depth = 1,
          before = "(x^-1)", after = "(x^-1)", action = "differentiate the result by rule" },
    },
}

local fake_answer_only = {
    outcome = "unsupported form",
    detail = "the integrand contains a form with no rule",
    solved = false,
    answer_only = true,
    status = "unsupported",
    result = "answer-from-giac",
    result_form = "elementary closed form",
    giac_tag = "exact",
    giac_form = "elementary closed form",
    giac_raw = "answer-from-giac",
    nodes = 8,
    step_count = 0,
    rewrites = 0,
    giac_calls = 1,
    steps = {},
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
    precision = { kind = "exact", significant_digits = 0 },
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

local fake_vector_addition = {
    outcome = "solved",
    detail = "",
    solved = true,
    answer_only = false,
    status = "solved and verified",
    result = "(6.2 i + 7.0 j) m",
    value = "(6.2 i + 7.0 j) m",
    precision = { kind = "measured", significant_digits = 2 },
    nodes = 18,
    step_count = 4,
    rewrites = 3,
    giac_calls = 0,
    steps = {
        { kind = "plan", name = "Vector addition", goal = "Add two Cartesian vectors",
          short = "Add matching components", claim = "no claim", verified = true,
          failed = false, depth = 0 },
    },
}

local fake_relative_motion = {
    outcome = "solved", detail = "", solved = true, answer_only = false,
    status = "solved and verified", result = "(7 i - 6 j) m/s", value = "(7 i - 6 j) m/s",
    exact_x = "7", exact_y = "-6", unit = "m/s", frame = "ground", direction = "southeast",
    interpretation = "drone moves southeast relative to wind in the declared east-north axes",
    precision = { kind = "exact", significant_digits = 0 },
    giac_tag = "exact", giac_method = "Giac Simplify and local canonical comparison",
    agrees = true, nodes = 28, step_count = 11, rewrites = 4, giac_calls = 2,
    steps = {
        { kind = "plan", name = "Cartesian relative-motion plan",
          goal = "Find the subject velocity relative to the reference",
          short = "Subtract matching components after checking frame and units",
          claim = "no claim", verified = true, failed = false, depth = 0 },
        { kind = "check", name = "Relative-motion vector rank", goal = "Check both velocity ranks",
          short = "Both inputs must be two-dimensional", claim = "same rank",
          verified = true, failed = false, depth = 1 },
        { kind = "check", name = "Declared velocity frames", goal = "Check both frames are declared",
          short = "Each component vector needs a frame", claim = "declared frame",
          verified = true, failed = false, depth = 1 },
        { kind = "check", name = "Matching velocity frames", goal = "Check both frames match",
          short = "Components can only be subtracted in one frame", claim = "same frame",
          verified = true, failed = false, depth = 1 },
        { kind = "check", name = "Velocity dimensions", goal = "Check both quantities are velocities",
          short = "Both inputs need dimension L T^-1", claim = "same dimension",
          verified = true, failed = false, depth = 1 },
        { kind = "transformation", name = "Relative velocity definition",
          goal = "Apply the relative velocity relation", short = "Subtract reference from subject",
          claim = "definition", verified = true, failed = false, depth = 1 },
        { kind = "transformation", name = "Exact SI conversion",
          goal = "Convert both velocity vectors to SI", short = "Apply each exact unit scale",
          claim = "equivalent expression", verified = true, failed = false, depth = 1 },
        { kind = "check", name = "Relative velocity dimension", goal = "Check the result dimension",
          short = "Velocity subtraction stays a velocity", claim = "same dimension",
          verified = true, failed = false, depth = 1 },
        { kind = "transformation", name = "Relative velocity component",
          goal = "Subtract the i velocity components", short = "Subtract reference i from subject i",
          claim = "equivalent expression", verified = true, failed = false, depth = 1 },
        { kind = "transformation", name = "Relative velocity component",
          goal = "Subtract the j velocity components", short = "Subtract reference j from subject j",
          claim = "equivalent expression", verified = true, failed = false, depth = 1 },
        { kind = "check", name = "Relative direction", goal = "Interpret the component signs",
          short = "Read direction from the east-north axes", claim = "verified",
          verified = true, failed = false, depth = 1 },
    },
}

local fake_unit_conversion = {
    outcome = "converted", detail = "", solved = true, answer_only = false,
    status = "solved and verified", result = "0.00000250 m^3", nodes = 10,
    precision = { kind = "measured", significant_digits = 3 },
    child_slots = 6, step_count = 3, rewrites = 2, giac_calls = 0,
    steps = {
        { kind = "plan", name = "Unit conversion", goal = "Convert cubic centimetres to cubic metres",
          short = "Apply the exact prefix factor to every dimension", claim = "no claim",
          verified = true, failed = false, depth = 0 },
    },
}

local fake_density = {
    outcome = "solved", detail = "", solved = true, answer_only = false,
    status = "solved and verified", result = "mass = 0.006 kg", nodes = 14,
    precision = { kind = "exact", significant_digits = 0 },
    step_count = 4, rewrites = 3, giac_calls = 0,
    steps = {
        { kind = "plan", name = "Density", goal = "Find mass from density and volume",
          short = "Use the density definition", claim = "no claim", verified = true,
          failed = false, depth = 0 },
    },
}

local fake_work = {
    outcome = "solved", detail = "", solved = true, answer_only = false,
    status = "solved and verified", result = "-14 kg m^2/s^2", nodes = 16,
    precision = { kind = "exact", significant_digits = 0 },
    step_count = 4, rewrites = 3, giac_calls = 1, agrees = true,
    interpretation = "the force has a component opposite the displacement direction",
    steps = {
        { kind = "plan", name = "Work", goal = "Evaluate the force and displacement dot product",
          short = "Multiply matching components and add", claim = "no claim", verified = true,
          failed = false, depth = 0 },
    },
}

-- Global, since this file is at Lua's ceiling of 200 top-level locals.
fake_forces = {
    outcome = "solved", detail = "", solved = true, answer_only = false,
    status = "solved and verified", result = "acceleration = 3.5 m/s^2", nodes = 24,
    value = "3.5", exact_value = "7/2", unit = "m/s^2", unknown = "acceleration",
    surface = "horizontal", friction_model = "kinetic",
    along_equation = "12 - 5 = 2*a", across_equation = "N - 20 = 0",
    consistency = "the assumed sliding motion is consistent with the solution",
    precision = { kind = "exact", significant_digits = 0 },
    step_count = 7, rewrites = 5, giac_calls = 0,
    inventory = {
        { kind = "weight", label = "Weight", agent = "Earth", magnitude = "20",
          along = "0", across = "-20", known = true },
        { kind = "normal", label = "Normal force", agent = "table", magnitude = "20",
          along = "0", across = "20", known = true },
        { kind = "applied", label = "Applied force", agent = "hand", magnitude = "12",
          along = "12", across = "0", known = true },
        { kind = "friction", label = "Kinetic friction", agent = "table", magnitude = "5",
          along = "-5", across = "0", known = true },
    },
    pairs = {
        { kind = "normal", on_body = "block", by_body = "table",
          reaction_on = "table", reaction_by = "block", magnitude = "20" },
    },
    steps = {
        { kind = "plan", name = "Free-body plan", goal = "Find the acceleration on block",
          short = "Inventory the forces and sum each axis", claim = "no claim", verified = true,
          failed = false, depth = 0 },
    },
}

local fake_components = {
    outcome = "solved", detail = "", solved = true, answer_only = false,
    status = "solved and verified", has_components = true, nodes = 20,
    precision = { kind = "exact", significant_digits = 0 },
    step_count = 5, rewrites = 4, giac_calls = 8,
    components = { x = "5*sqrt(3)", y = "5", rank = 2, frame = "lab", unit = "m/s",
                   precision = { kind = "exact", significant_digits = 0 } },
    steps = {
        { kind = "plan", name = "Resolve vector", goal = "Find Cartesian components",
          short = "Project the magnitude onto each axis", claim = "no claim", verified = true,
          failed = false, depth = 0 },
    },
}

local fake_dimension_mismatch = {
    outcome = "dimension mismatch", detail = "length cannot be converted to time", solved = false,
    answer_only = false, status = "failed verification", nodes = 4, step_count = 2,
    rewrites = 0, giac_calls = 0,
    steps = {
        { kind = "check", name = "Dimension check", goal = "Compare source and target dimensions",
          short = "Only equal dimensions can be converted", claim = "same dimension",
          verified = false, failed = true, depth = 0 },
    },
}

local exact_catch_up_precision = { kind = "exact", significant_digits = 0 }
local fake_catch_up = {
    outcome = "solved", detail = "Atlas and Boreal meet at 10 s and position 20 m",
    solved = true, has_result = true, answer_only = false, status = "solved and verified",
    result = "Atlas and Boreal meet at 10 s and position 20 m",
    precision = exact_catch_up_precision,
    event_time = { value = "10", exact_value = "10", unit = "s",
                   precision = exact_catch_up_precision },
    event_position = { value = "20", exact_value = "20", unit = "m",
                       precision = exact_catch_up_precision },
    shared_active_start = { value = "5", exact_value = "5", unit = "s",
                            precision = exact_catch_up_precision },
    equation = "0 + 2*(t_event - 0) = 0 + 4*(t_event - 5)",
    active_domain = "t_event >= 5",
    substituted = "20 = 20",
    assumptions = "Atlas and Boreal use constant velocity in frame track",
    nodes = 23, step_count = 10, rewrites = 16, giac_calls = 0,
    steps = {
        { kind = "plan", name = "Constant-velocity active-interval model",
          goal = "Find when Atlas and Boreal meet", short = "Build both position laws",
          claim = "no claim", verified = true, failed = false, depth = 0 },
        { kind = "check", name = "Dimensional analysis",
          goal = "Check both position-law dimensions", short = "Both laws produce length",
          claim = "same dimension", verified = true, failed = false, depth = 0 },
        { kind = "transformation", name = "Equal-position event",
          goal = "Create equal-position event equation", short = "Set the positions equal",
          claim = "equivalent", verified = true, failed = false, depth = 0 },
        { kind = "check", name = "Shared active interval",
          goal = "Check shared active-time domain", short = "Require t_event >= 5 s",
          claim = "domain", verified = true, failed = false, depth = 0 },
        { kind = "check", name = "Original position-law substitution",
          goal = "Evaluate Atlas at the event", short = "Atlas is at 20 m",
          claim = "verified", verified = true, failed = false, depth = 0 },
        { kind = "check", name = "Original position-law substitution",
          goal = "Evaluate Boreal at the event", short = "Boreal is at 20 m",
          claim = "verified", verified = true, failed = false, depth = 0 },
        { kind = "plan", name = "Inverse operations on a linear equation",
          goal = "Isolate t_event", short = "Solve the exact linear equation",
          claim = "no claim", verified = true, failed = false, depth = 0 },
        { kind = "transformation", name = "Collect like terms",
          goal = "Collect terms in t_event", short = "Move event-time terms together",
          claim = "equivalent", verified = true, failed = false, depth = 0 },
        { kind = "transformation", name = "Division property of equality",
          goal = "Isolate t_event", short = "Divide both sides by the coefficient",
          claim = "equivalent", verified = true, failed = false, depth = 0 },
        { kind = "check", name = "Check answer", goal = "Verify the event",
          short = "Both original positions equal 20 m", claim = "verified",
          verified = true, failed = false, depth = 0 },
    },
}

local calls = {
    differentiate = 0, integrate = 0, solve = 0, kinematics = 0, giac = 0, manifest = 0, integrity = 0,
    device_identity = 0,
    unit_conversion = 0, density = 0, vector_addition = 0, work = 0, components = 0,
    forces = 0,
    catch_up = 0, relative_motion = 0, resource_profile_begin = 0, resource_profile_finish = 0,
}
local profile_events = {}
local profile_finishes = {}
local last_args = nil
local next_step_result = nil
local function take_step_result(original_expression)
    local r = next_step_result or fake_result
    next_step_result = nil
    r.original_expression = original_expression
    if type(r.normalized_expression) ~= "string" then r.normalized_expression = original_expression end
    return r
end
-- The build names its sidecars at test registration and passes them in, so the mock carries the
-- build's names rather than a copy that goes stale. A standalone run has no build to ask and spells
-- them with CMake's rule from nps_sidecar_name at CMakeLists.txt:93. Global, since this file is at
-- Lua's ceiling of 200 top-level locals.
function sidecarName(variable, package)
    return os.getenv(variable) or (package:sub(1, -5) .. ".sha256.tns")
end
local fake_manifest = {
    id = "stepcas.unified.inputs-sha256.14dc93ecb3d3262c5ddf75fd3d6c6ab9bf9bfbaa02b8a842e880b4e7fb007643",
    artifact = "unified",
    schema_version = 2,
    stepcas_version = "nps 0.2",
    supported_targets = {
        { calculator_model = "TI-Nspire CX II non-CAS", os_version = "6.2.0.333", ndl_version = "r2022" },
        { calculator_model = "TI-Nspire CX II non-CAS", os_version = "6.4.0.74", ndl_version = "r2022" },
    },
    symbolic_backend = {
        name = "Giac",
        version = "1.9.0",
        interface_id = "nps.giac.typed-v1",
        deployment = "bundled-static",
    },
    installed_modules = {
        { kind = "solver", id = "algebra.linear-equation.one-unknown" },
        { kind = "solver", id = "algebra.quadratic.pure-square.one-unknown" },
        { kind = "solver", id = "algebra.formula-rearrangement.single-occurrence" },
        { kind = "solver", id = "algebra.polynomial-rewrite.single-expression" },
        { kind = "solver", id = "number.integer-method.literal" },
        { kind = "solver", id = "calculus.derivative.single-variable" },
        { kind = "solver", id = "calculus.integral.indefinite.single-variable" },
        { kind = "solver", id = "physics.kinematics.constant-acceleration.one-dimension" },
        { kind = "solver", id = "physics.kinematics.catch-up.equal-position" },
        { kind = "solver", id = "physics.density.mass-volume" },
        { kind = "solver", id = "physics.vectors.cartesian-addition.two-dimension" },
        { kind = "solver", id = "physics.kinematics.relative-motion.components.two-dimension" },
        { kind = "solver", id = "physics.vectors.magnitude-components.two-dimension" },
        { kind = "solver", id = "physics.forces.newton-second-law" },
        { kind = "solver", id = "physics.work.constant-force-dot-product" },
        { kind = "solver", id = "units.chain-link-conversion" },
        { kind = "content", id = "units.si" },
    },
    schema_versions = {
        { id = "capability-manifest", version = 2 },
        { id = "solution-context", version = 3 },
    },
    integrity_identifiers = {
        { component = "stepcas.build-inputs", scheme = "sha256", value = "bf969674a85116d508e00d8d267475219dce1f63fec614e2ade942358d3db858" },
        { component = "ndl.build-inputs", scheme = "sha256", value = "483d83aba1dd6e4285219a14d1b800660c1c5dd7250f4eb2241ab9c989f89783" },
        { component = "giac.sources-config", scheme = "sha256", value = "3aa6f1832c782fa8c088e5e968fc0f9a93fe0e181e528082f755e87ddfd837c2" },
        { component = "artifact.package", scheme = "runtime-sha256-sidecar",
          value = sidecarName("NPS_UNIFIED_SIDECAR", "nps_nspire.luax.tns") },
        { component = "ui.document", scheme = "external-sha256-sidecar",
          value = sidecarName("NPS_V4_SIDECAR", "nps_v4.tns") },
    },
}
nps_split = {
    walkthrough = function() return nil end,
    differentiate = function(text, ...) calls.differentiate = calls.differentiate + 1 last_args = { text, ... } return take_step_result(text) end,
    integrate = function(text, ...)
        calls.integrate = calls.integrate + 1
        profile_events[#profile_events + 1] = "solve:integrate"
        last_args = { text, ... }
        return take_step_result(text)
    end,
    solve = function(text, ...) calls.solve = calls.solve + 1 last_args = { text, ... } return take_step_result(text) end,
    kinematics = function(...) calls.kinematics = calls.kinematics + 1 last_args = { ... } return fake_kinematics end,
    unit_conversion = function(source, target)
        calls.unit_conversion = calls.unit_conversion + 1
        profile_events[#profile_events + 1] = "solve:unit_conversion"
        if source == "3 m" and target == "s" then return fake_dimension_mismatch end
        return fake_unit_conversion
    end,
    density = function(...) calls.density = calls.density + 1 last_args = { ... } return fake_density end,
    vector_addition = function(...)
        calls.vector_addition = calls.vector_addition + 1
        last_args = { ... }
        return fake_vector_addition
    end,
    relative_motion = function(...)
        calls.relative_motion = calls.relative_motion + 1
        last_args = { ... }
        return fake_relative_motion
    end,
    work = function(...) calls.work = calls.work + 1 last_args = { ... } return fake_work end,
    forces = function(...) calls.forces = calls.forces + 1 last_args = { ... } return fake_forces end,
    magnitude_angle_to_components = function(...)
        calls.components = calls.components + 1
        last_args = { ... }
        return fake_components
    end,
    catch_up = function(...)
        calls.catch_up = calls.catch_up + 1
        last_args = { ... }
        return fake_catch_up
    end,
}
nps_split.integrity_status = function() calls.integrity = calls.integrity + 1 return "verified" end
-- The fields src/platform/nspire/lua_module.cc:1388 sets, with the reading the target device gives:
-- a non-CAS CX II on 6.4.0.74 under Ndl r2022, which the module says is a pair it knows.
nps_split.device_identity = function()
    calls.device_identity = calls.device_identity + 1
    return { model = "cx2", cas = "non-cas", os = "6.4.0.74", os_index = 1, hardware_type = 1,
             hardware_subtype = 1, ndl_revision = 2022, third_party_loader = false,
             model_agrees_with_os = true }
end
nps_split.capability_manifest = function() calls.manifest = calls.manifest + 1 return fake_manifest end
nps_split.resource_profile_begin = function(operation)
    calls.resource_profile_begin = calls.resource_profile_begin + 1
    profile_events[#profile_events + 1] = "begin:" .. operation
    return true
end
nps_split.resource_profile_finish = function(metrics)
    calls.resource_profile_finish = calls.resource_profile_finish + 1
    profile_events[#profile_events + 1] = "finish:" .. metrics.operation
    profile_finishes[#profile_finishes + 1] = metrics
    return { status = "written", report_written = true }
end
-- One module, so the step entries and caseval are one table, and the fake is the module V4 asks
-- for by name. Any other name is an error, the way a missing .luax.tns is on the calculator.
-- version() answers with the banner the device build produces, khi-src/src/usual.cc:7957 with
-- GIAC_VERSION from config.h, so the version scan runs against the shape it will actually meet.
nps_split.caseval = function(s)
    calls.giac = calls.giac + 1
    if s == "version()" then
        return "giac for TI Nspire CX 1.9.0, (c) B. Parisse and R. De Graeve, Institut Fourier"
    end
    return "giac(" .. s .. ")"
end
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
local nrequire_names = {}
nrequire = function(name)
    nrequire_names[#nrequire_names + 1] = name
    if name == "nps_nspire" then
        nps_nspire = nps_split
        return nps_nspire
    end
    error("no module named " .. tostring(name))
end

-- Most checks inspect settled layout. First paint checks call on.paint directly.
local function painted()
    for _ = 1, 4 do on.paint(gc) end
    drawn = {}
    draw_calls = {}
    filled = 0
    font_styles = {}
    on.paint(gc)
    local text = table.concat(drawn, "\n")
    check(not text:find("paint failed", 1, true), "paint does not raise: " .. text:sub(1, 200))
    return text
end

local function draw_call(prefix, y)
    for _, call in ipairs(draw_calls) do
        if call.y == y and call.text:sub(1, #prefix) == prefix then return call end
    end
    return nil
end

-- Where the last paint put the row for this step, so a tap can be aimed at what was drawn.
function bandTopFor(step)
    for _, band in ipairs(steps.bands or {}) do
        if band.step == step then return band.top end
    end
    return -1
end

-- The frame's text with the line breaks closed up, for a phrase that a wrap may have split across
-- two rows. The wrap drops the space it broke on, so rejoining with one puts the phrase back.
function paintedRun()
    local out = {}
    for _, call in ipairs(draw_calls) do out[#out + 1] = call.text end
    return table.concat(out, " ")
end

-- The same search without pinning a row's y, for the checks where the point is what was drawn
-- rather than where. Globals because this chunk is at the 200-local ceiling.
function draw_call_any(prefix)
    for _, call in ipairs(draw_calls) do
        if call.text:sub(1, #prefix) == prefix then return call end
    end
    return nil
end

-- UI-005 as a property of the whole frame rather than of one row: the furthest any string reaches
-- past the right edge. Anything above zero is a glyph the student cannot read and cannot scroll to.
function drawOverflow()
    local worst = 0
    for _, call in ipairs(draw_calls) do
        local past = call.x + gc:getStringWidth(call.text) - platform.window:width()
        if past > worst then worst = past end
    end
    return worst
end

-- From the first line beginning with this prefix to the end of the frame, joined in paint order. A
-- wrapped token survives as a contiguous run inside the result, so searching this for the whole
-- token says whether the wrap kept it or dropped part of it at a line end.
-- The named steps in steps.rows, in order, as one string. This is the presentation model buildRows
-- rebuilds on every detail change, so it is where a renamed or reordered step would show. The
-- result model the module handed over cannot show it, because nothing in the viewer writes there.
function stepRowNames()
    local out = {}
    for _, row in ipairs(steps.rows or {}) do
        if row.kind == "text" then out[#out + 1] = row.step .. " " .. row.text end
    end
    return table.concat(out, " | ")
end

function drawnJoined(prefix)
    local out, started = "", false
    for _, call in ipairs(draw_calls) do
        if started or call.text:sub(1, #prefix) == prefix then
            started = true
            out = out .. call.text
        end
    end
    return out
end

-- Types into the input editor the way the OS would leave it: a math box holding the text.
local function type_line(s)
    fctEditor.editor:setExpression("\\0el {" .. s .. "}")
end

local chunk = assert(loadfile("lua/nps_v4.lua"))
local ok, err = pcall(chunk)
check(ok, "the document loads: " .. tostring(err))
check(#nrequire_names == 1 and nrequire_names[1] == "nps_nspire",
      "it asks for one module, nps_nspire, and for nothing else")
check(calls.integrity == 1 and hasGiac == true and hasSteps == true,
      "one verified integrity check enables both halves")
-- PLAT-012 in the PRD's own words is that a corrupt runtime never reaches outside computation, and
-- in this product outside computation means math.evalStr, which is what V3 routed through at
-- nps_v3.lua:28 before V4 replaced it with a native caseval. The runtime conjunct in
-- plainInputDoesNotFallback only fires when a document both holds the call and reaches the branch
-- that runs it, and one of the three corrupt documents refuses too early to reach that branch. This
-- reads the source, so a reintroduced call is caught at load whatever runs afterwards.
do
    local source = assert(io.open("lua/nps_v4.lua")):read("*a")
    check(source:find("evalStr", 1, true) == nil,
          "the document never reaches for the OS evaluator, which is V3's outside-computation path")
end
for _, name in ipairs({ "enterKey", "paint", "escapeKey", "tabKey", "backtabKey", "arrowDown",
                        "arrowUp", "arrowLeft", "arrowRight", "charIn", "save",
                        "restore" }) do
    check(type(on[name]) == "function", name .. " is installed")
end
-- Held now, because every isolated document loaded later registers its own menu over this one, and
-- writes its own handlers into the same `on` table for the same reason.
main_menu = registered_menu
main_contextMenu = on.contextMenu
check(type(registered_menu) == "table" and registered_menu[1][1] == "Physics",
      "the Physics group leads the tool palette")
check(registered_menu[2][1] == "Templates" and registered_menu[3][1] == "Steps" and
      registered_menu[4][1] == "Actions",
      "the templates and the walkthrough controls precede the native categories")

-- The native Calculator categories, in native order, after the two pinned boxes. Polynomials and
-- Plots are the two sanctioned deviations: native nests the first under Algebra as a submenu the
-- palette cannot express, and has no Plots category at all because graphing is the Graphs app.
do
    local expected = { "Physics", "Templates", "Steps", "Actions", "Number", "Algebra",
                       "Polynomials", "Calculus", "Probability", "Statistics", "Matrix & Vector",
                       "Plots" }
    check(#registered_menu == #expected, "the palette holds " .. #expected .. " tool boxes, not " ..
          #registered_menu)
    for i, name in ipairs(expected) do
        check(registered_menu[i] and registered_menu[i][1] == name,
              "tool box " .. i .. " is " .. name)
    end

    -- Every constraint that would otherwise be discovered on the calculator. The item cap and the
    -- box cap are the platform's, from TI's Lua guide chapter 17. 44 is a proxy for budgets'
    -- measured width, since the palette never touches our graphics context and this stub returns
    -- the same width at every font size, so a wide-glyph label still has to be checked on hardware.
    local entries = 0
    local longest = 0
    for box = 1, #registered_menu do
        local items = 0
        for item = 2, #registered_menu[box] do
            local entry = registered_menu[box][item]
            if entry ~= "-" then
                items = items + 1
                entries = entries + 1
                local label = entry[1]
                if #label > longest then longest = #label end
                check(#label <= 44, string.format(
                      "label %d of tool box %d is %d characters, over the 44 budget: %s",
                      item - 1, box, #label, label))
                local first = label:byte(1)
                check(first >= 65 and first <= 90, string.format(
                      "label %d of tool box %d opens with a command rather than a word: %s",
                      item - 1, box, label))
            end
        end
        check(items <= 30, string.format("tool box %d holds %d items, over the palette cap of 30",
                                         box, items))
    end
    check(#registered_menu <= 15, "the palette holds at most 15 tool boxes")
    -- An exact count rather than a floor, because the failure worth catching is an entry going
    -- missing, and a floor cannot see that. The cost is that an intentional palette change edits
    -- this number, which is the trade and not an oversight.
    check(entries == 186, "the palette holds every retained entry: " .. entries .. " of 186")
    check(longest <= 44, "the longest label is " .. longest .. " characters")
end
local step_menu_count = 0
for box = 1, #registered_menu do
    if registered_menu[box][1] == "Steps" then step_menu_count = step_menu_count + 1 end
end
check(step_menu_count == 1 and registered_menu[3][2][1] == "Full walkthrough (all steps)" and
      registered_menu[3][3][1] == "Hint walkthrough (Tab next)",
      "one explicit Steps group exposes both progression settings")
registered_menu[3][3][2]()
check(steps.progression == "hint", "the Steps palette enables hint progression before solving")
registered_menu[3][2][2]()
check(steps.progression == "full", "and restores full progression without running a solver")

do
    local expected = { ["Increase Font Size"] = 16, ["Decrease Font Size"] = 12 }
    check(fctEditor == nil and inited == false,
          "the font palette regression runs before the first paint creates an editor")
    for item = 2, #registered_menu[4] do
        local entry = registered_menu[4][item]
        if entry ~= "-" and expected[entry[1]] then
            local survived, why = pcall(entry[2])
            check(survived, entry[1] .. " declines safely before the first paint: " .. tostring(why))
            check(fsize == expected[entry[1]], entry[1] .. " still selects its adjacent font size")
        end
    end
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
check(text:find("Giac 1.9.0 :", 1, true) ~= nil,
      "and draws the version it scanned out of the running backend, not a compiled-in one")
check(calls.giac == 1, "after asking Giac its version once")
-- PLAT-001 is the one requirement about which handheld this is, and it was unreadable from the
-- handheld: the module has read the model, the build and the Ndl revision all along and nothing
-- drew them. Asked once, because none of the three can change while the document is open.
check(text:find("Nspire cx2 non-cas, OS 6.4.0.74, Ndl r2022", 1, true) ~= nil and
      calls.device_identity == 1,
      "and names the calculator it is running on, read off the device rather than compiled in")
painted()
check(calls.device_identity == 1, "and does not ask again on every frame it paints")
-- Native leaves an empty work area bare, so the shell's instruction text is gone from it.
check(text:find("Type * to enter shell", 1, true) == nil,
      "the work area carries no instruction text")

local giac_before_manifest = calls.giac
local solve_before_manifest = calls.solve
local manifest_before_command = calls.manifest
type_line("!m")
on.enterKey()
local manifest_text = steps.histText[#steps.histText][2]
local build_fingerprint = fake_manifest.id:match("([^.]+)$")
build_fingerprint = build_fingerprint:sub(1, 12) .. "..." .. build_fingerprint:sub(-12)
check(manifest_before_command == 1 and calls.manifest == manifest_before_command,
      "startup reads the compiled capability manifest once and !m reuses it")
check(manifest_text == " unified " .. build_fingerprint .. ", Giac 1.9.0, 17 modules",
      "and displays the unified manifest identity")
-- The mock is the unified manifest as the shell sees it, so its sidecar rows are the names the build
-- gives them. A name not ending in .tns cannot reach the calculator at all, which is what add_tns
-- measured against the emulator and the handheld, and which the mock claimed for four commits.
do
    local rows = {}
    for _, row in ipairs(fake_manifest.integrity_identifiers) do rows[row.component] = row end
    check(os.getenv("NPS_EVIDENCE") == nil or
          (os.getenv("NPS_UNIFIED_SIDECAR") ~= nil and os.getenv("NPS_V4_SIDECAR") ~= nil),
          "the build tells the smoke test what it names its two sidecars")
    check(rows["artifact.package"] ~= nil and
          rows["artifact.package"].scheme == "runtime-sha256-sidecar" and
          rows["ui.document"] ~= nil and
          rows["ui.document"].scheme == "external-sha256-sidecar",
          "the unified manifest carries both sidecars, the package one runtime verifiable")
    for _, component in ipairs({ "artifact.package", "ui.document" }) do
        local row = rows[component]
        check(row ~= nil and row.value:sub(-4) == ".tns",
              "the " .. component .. " sidecar can reach the calculator: " ..
                  tostring(row and row.value))
    end
end
check(calls.solve == solve_before_manifest and calls.giac == giac_before_manifest,
      "without invoking solve or caseval")
check(runSteps("help", ""):find("!m manifest", 1, true) ~= nil, "the help names the manifest command")
check(runSteps("help", ""):find("v0 is the starting speed", 1, true) ~= nil,
      "and says what the kinematics symbols mean, for a student who does not know them")

check(registered_menu[1][2][1] == "Guided physics problems",
      "the Physics palette opens the guided browser")
registered_menu[1][2][2]()
check(physicsBrowser.active == true and steps.active == false,
      "the guided browser opens without a text command")
check(edit_menu.copy == false and edit_menu.paste == false,
      "and stops offering Copy and Paste, since the editors it would act on are parked")

-- probe/native-ui/13-catalog.png: the native list carries a scrollbar when it overflows, so a list
-- that scrolls without one reads as a list that ends where the screen does.
do
    fills = {}
    on.paint(gc)
    local bars = 0
    for _, r in ipairs(fills) do
        if r.w == 3 and r.x + r.w == 320 then bars = bars + 1 end
    end
    check(bars == 2, "the fixture list draws a scrollbar track and thumb once it overflows: " ..
                     tostring(bars) .. " of the 2 fills expected, over " ..
                     tostring(#PHYSICS_FIXTURES) .. " fixtures")
end
check(fctEditor.editor.visible == false and fctEditor.editor.x == -10000,
      "the browser hides and parks the shell editors")
text = painted()
check(text:find("Guided physics", 1, true) ~= nil and
      text:find("Change units when the unit is cubed", 1, true) ~= nil and
      text:find("See what happens when units cannot match", 1, true) ~= nil,
      "the 320 by 240 browser exposes its first seven fixtures")
check(paintedRun():find("How many cubic metres is 2.50 cubic centimetres?", 1, true) ~= nil,
      "the selected fixture explains its structured problem")

local guided_profile_begin_before = calls.resource_profile_begin
local guided_profile_finish_before = calls.resource_profile_finish
on.enterKey()
check(calls.unit_conversion == 1 and steps.active == true and physicsBrowser.active == false,
      "enter runs the selected unit conversion through the native bridge")
check(steps.result.mode == "unit_conversion" and
      steps.result.input:find("2.50 cubic centimetres", 1, true) ~= nil,
      "the viewer records the fixture family and problem")
check(calls.resource_profile_begin == guided_profile_begin_before + 1 and
      calls.resource_profile_finish == guided_profile_finish_before and
      profile_events[#profile_events - 1] == "begin:unit_conversion" and
      profile_events[#profile_events] == "solve:unit_conversion",
      "a guided solve starts profiling immediately before the native call and not before render")
text = painted()
check(mathBoxShowing("0.00000250 m^3") ~= nil,
      "the unit conversion answer opens in the derivation viewer")
local guided_metrics = profile_finishes[#profile_finishes]
check(calls.resource_profile_finish == guided_profile_finish_before + 1 and
      guided_metrics.operation == "unit_conversion" and guided_metrics.render_ready_ms == 200 and
      guided_metrics.lua_live_bytes > 0 and guided_metrics.lua_live_bytes == math.floor(guided_metrics.lua_live_bytes) and
      guided_metrics.arena_nodes == 10 and guided_metrics.arena_child_slots == 6 and
      guided_metrics.derivation_steps == 3 and guided_metrics.rewrites == 2 and
      guided_metrics.backend_calls == 0 and steps.result.resource_profile.report_written == true,
      "the first successful guided result paint finishes one complete live resource record")
painted()
check(calls.resource_profile_finish == guided_profile_finish_before + 1,
      "repainting a guided result cannot finish the same profile twice")
on.escapeKey()

openPhysicsFixtures()
on.arrowDown()
on.enterKey()
check(calls.density == 1 and steps.result.mode == "density",
      "the second fixture runs density through its native bridge")
on.escapeKey()

openPhysicsFixtures()
on.arrowDown()
on.enterKey()
check(calls.kinematics == 1 and steps.result.mode == "kinematics",
      "the third fixture runs constant acceleration through its native bridge")
on.escapeKey()

openPhysicsFixtures()
on.arrowDown()
on.enterKey()
check(calls.vector_addition == 1 and steps.result.mode == "vector_addition",
      "the fourth fixture runs vector addition through its native bridge")
text = painted()
answer_box = mathBoxShowing("6.2 ")
check(answer_box ~= nil and answer_box.expr:find("7.0 ", 1, true) ~= nil and
      answer_box.expr:find(") m", 1, true) ~= nil and text:find("APPROXIMATE", 1, true) ~= nil,
      "the fourth archetype renders and labels its measured vector answer")
on.escapeKey()

openPhysicsFixtures()
on.arrowDown()
on.enterKey()
check(calls.work == 1 and steps.result.mode == "work" and
      type(last_args[1]) == "table" and last_args[1].force.frame == "lab" and
      last_args[1].force.precision.kind == "exact",
      "the fifth fixture sends typed work vectors to the native bridge")
text = painted()
check(mathBoxShowing("-14 kg m^2/s^2") ~= nil and
      text:find("opposite", 1, true) ~= nil and text:find("displacement", 1, true) ~= nil,
      "negative work is visible without losing its direction")
on.escapeKey()

openPhysicsFixtures()
on.arrowDown()
on.enterKey()
check(calls.components == 1 and steps.result.mode == "magnitude_angle_to_components" and
      type(last_args[1]) == "table" and last_args[1].angle_unit == "degrees",
      "the sixth fixture sends typed magnitude and angle metadata to the native bridge")
text = painted()
answer_box = mathBoxShowing("5*√(3)")
check(answer_box ~= nil and answer_box.expr:find(") m/s", 1, true) ~= nil,
      "symbolic components are formatted for the derivation viewer")
-- The italic i and j used to be drawn a glyph at a time by the viewer. The OS math box italicises
-- them itself (measured: native-ui/d2-render-matrix.png), so the hand-drawn run must be gone, not
-- merely duplicated, or the same answer would be painted twice in two places.
local component_italics = 0
for _, style in ipairs(font_styles) do if style == "i" then component_italics = component_italics + 1 end end
check(component_italics == 0, "the viewer no longer draws basis glyphs by hand")
on.escapeKey()

openPhysicsFixtures()
on.arrowDown()
on.enterKey()
check(calls.unit_conversion == 2 and steps.result.outcome == "dimension mismatch",
      "the seventh fixture exercises a typed dimensional refusal")
text = painted()
-- The verdict wraps when it is long, so the refusal's reason is read across the rows it took
-- rather than on one of them.
check(paintedRun():find("length cannot be converted to time", 1, true) ~= nil and
      paintedRun():find("Dimension check", 1, true) ~= nil,
      "the refusal explains the mismatch and retains its failed check")
on.escapeKey()

openPhysicsFixtures()
on.arrowDown()
text = painted()
check(text:find("Find when a fast runner catches a slow one", 1, true) ~= nil and
      text:find("Change units when the unit is cubed", 1, true) == nil,
      "the eighth fixture scrolls into the 320 by 240 browser")
check(paintedRun():find("Boreal starts 5 seconds later at 4 metres per second", 1, true) ~= nil,
      "the catch-up fixture explains its delayed-start event")
local catch_up_history_before = #steps.histText
on.enterKey()
check(calls.catch_up == 1 and steps.result.mode == "catch_up" and
      type(last_args[1]) == "table",
      "the eighth fixture calls the native catch-up bridge exactly once")
local catch_up_input = last_args[1]
check(catch_up_input.first.name == "Atlas" and catch_up_input.second.name == "Boreal" and
      catch_up_input.first.frame == "track" and catch_up_input.second.frame == "track" and
      catch_up_input.first.position == "0 m" and catch_up_input.first.velocity == "2 m/s" and
      catch_up_input.first.start_time == "0 s" and catch_up_input.second.position == "0 m" and
      catch_up_input.second.velocity == "4 m/s" and catch_up_input.second.start_time == "5 s" and
      catch_up_input.first.motion == "constant_velocity" and
      catch_up_input.second.motion == "constant_velocity",
      "the catch-up fixture sends two named typed active-interval records")
check(steps.result.event_time.value == "10" and
      steps.result.event_time.exact_value == "10" and
      steps.result.event_time.unit == "s" and
      steps.result.event_position.value == "20" and
      steps.result.event_position.exact_value == "20" and
      steps.result.event_position.unit == "m",
      "the viewer retains structured catch-up event values and units")
check(steps.result.event_time.precision.kind == "exact" and
      steps.result.event_position.precision.significant_digits == 0 and
      steps.result.shared_active_start.exact_value == "5" and
      steps.result.active_domain == "t_event >= 5",
      "the viewer retains catch-up precision and active-boundary metadata")
text = painted()
check(mathBoxShowing("Atlas and Boreal meet at 10 s and position 20 m") ~= nil and
      text:find("Constant-velocity active-interval model", 1, true) ~= nil,
      "the catch-up result and derivation render in the viewer")
steps.result.outcome = "no meeting"
steps.result.detail = "the active-interval position laws never have the same position"
steps.result.result = steps.result.detail
steps.result.solved = false
steps.result.has_result = true
steps.result.event_time = nil
steps.result.event_position = nil
text = painted()
check(text:find("NO RESULT", 1, true) == nil and
      text:find("EXACT", 1, true) ~= nil and
      paintedRun():find(steps.result.detail, 1, true) ~= nil,
      "a verified empty solution set renders as an exact terminal result")
steps.result.outcome = "meeting at every active time"
steps.result.detail = "the two position laws coincide for every time in the shared active domain"
steps.result.result = steps.result.detail
text = painted()
check(text:find("NO RESULT", 1, true) == nil and
      text:find("EXACT", 1, true) ~= nil and
      paintedRun():find(steps.result.detail, 1, true) ~= nil,
      "a verified all-times solution set renders as an exact terminal result")
check(#steps.histText == catch_up_history_before + 1 and
      steps.histText[#steps.histText][1] == " Guided: Find when a fast runner catches a slow one" and
      steps.histText[#steps.histText][2]:find("10 s and position 20 m", 1, true) ~= nil,
      "the catch-up event joins document history")
on.escapeKey()

openPhysicsFixtures()
on.arrowDown()
text = painted()
check(text:find("Find a speed seen from something moving", 1, true) ~= nil and
      text:find("Find when a fast runner catches a slow one", 1, true) ~= nil,
      "the relative-motion fixture is keyboard reachable beside the other motion model")
check(paintedRun():find("Its speed over the ground is one thing", 1, true) ~= nil,
      "the selected fixture says in plain words what a relative speed is")
local relative_history_before = #steps.histText
on.enterKey()
check(calls.relative_motion == 1 and steps.result.mode == "relative_motion" and
      type(last_args[1]) == "table",
      "the ninth fixture calls the native relative-motion bridge exactly once")
local relative_input = last_args[1]
check(relative_input.subject_name == "drone" and relative_input.reference_name == "wind" and
      relative_input.subject_velocity.x == "36" and relative_input.subject_velocity.y == "-18" and
      relative_input.subject_velocity.rank == 2 and relative_input.subject_velocity.frame == "ground" and
      relative_input.subject_velocity.unit == "km/h" and
      relative_input.subject_velocity.precision.kind == "exact" and
      relative_input.reference_velocity.x == "3" and relative_input.reference_velocity.y == "1" and
      relative_input.reference_velocity.rank == 2 and
      relative_input.reference_velocity.frame == "ground" and
      relative_input.reference_velocity.unit == "m/s" and
      relative_input.reference_velocity.precision.significant_digits == 0,
      "the fixture sends named framed 2D vectors with exact mixed-unit metadata")
check(steps.result.exact_x == "7" and steps.result.exact_y == "-6" and
      steps.result.unit == "m/s" and steps.result.frame == "ground" and
      steps.result.direction == "southeast",
      "the viewer retains the structured relative velocity and direction result")
text = painted()
answer_box = mathBoxShowing("(7 ")
check(answer_box ~= nil and answer_box.expr:find("- 6 ", 1, true) ~= nil and
      answer_box.expr:find(") m/s", 1, true) ~= nil and
      text:find("southeast relative to wind", 1, true) ~= nil,
      "the shared vector and meaning renderers show the answer and direction")
check(paintedRun():find("Giac's component cross-check agrees", 1, true) ~= nil,
      "the relative-motion viewer names its successful backend trust check")
local relative_italics = 0
for _, style in ipairs(font_styles) do if style == "i" then relative_italics = relative_italics + 1 end end
check(relative_italics == 0, "relative velocity goes through the same math box, not a hand-drawn one")
check(#steps.histText == relative_history_before + 1 and
      steps.histText[#steps.histText][1] == " Guided: Find a speed seen from something moving" and
      steps.histText[#steps.histText][2]:find("(7 i - 6 j) m/s", 1, true) ~= nil,
      "the relative-motion answer joins document history")
on.escapeKey()

forces_history_before = #steps.histText
openPhysicsFixtures()
on.arrowDown()
on.enterKey()
check(calls.forces == 1 and steps.result.mode == "forces" and type(last_args[1]) == "table",
      "the tenth fixture calls the native force bridge exactly once")
forces_input = last_args[1]
check(forces_input.body == "block" and forces_input.support == "table" and
      forces_input.mass == "2 kg" and forces_input.gravity == "10 m/s^2" and
      forces_input.surface == "horizontal" and forces_input.applied == "12 N" and
      forces_input.friction == "kinetic" and forces_input.friction_coefficient == "0.25" and
      forces_input.motion == "up the axis" and forces_input.equilibrium == false and
      forces_input.unknown == "acceleration",
      "the fixture sends the declared body, surface, friction model and requested unknown")
check(steps.result.inventory ~= nil and #steps.result.inventory == 4 and
      steps.result.inventory[4].kind == "friction" and
      steps.result.inventory[4].along == "-5" and
      steps.result.pairs ~= nil and #steps.result.pairs == 1 and
      steps.result.pairs[1].reaction_on == "table",
      "the viewer retains the force inventory and the third-law pair it keeps out of it")
check(#steps.histText == forces_history_before + 1 and
      steps.histText[#steps.histText][1] == " Guided: Find how hard a sliding block speeds up" and
      steps.histText[#steps.histText][2]:find("3.5", 1, true) ~= nil,
      "the force answer joins document history with its value")
on.escapeKey()
openPhysicsFixtures()
on.arrowUp()
on.escapeKey()

check(#steps.histText >= 10 and #histME1 >= 10,
      "guided solves join the document history")
openPhysicsFixtures()
on.escapeKey()
check(physicsBrowser.active == false and fctEditor.editor.visible == true,
      "escape closes the browser and restores the shell")
check(edit_menu.copy == true and edit_menu.paste == true,
      "and offers Copy and Paste again once the input line is back")

-- Native esc backs out one level and never drops the document, so at the shell it does nothing.
do
    local shell_history = #steps.histText
    on.escapeKey()
    on.escapeKey()
    check(steps.active == false and physicsBrowser.active == false and
          fctEditor.editor.visible == true and #steps.histText == shell_history,
          "esc at the shell is a no-op rather than a way out of the document")
end

do
local missing_fixture = #PHYSICS_FIXTURES + 1
local browser_refusal_before = #steps.histText
local browser_focus_before = physicsBrowser.focus
openPhysicsFixtures()
physicsBrowser.focus = missing_fixture
on.enterKey()
physicsBrowser.focus = browser_focus_before
check(#steps.histText == browser_refusal_before + 1 and
      steps.histText[#steps.histText][1] == " Guided: " .. tostring(missing_fixture) and
      steps.histText[#steps.histText][2] == " fixture unavailable",
      "a browser refusal records itself in the history")
check(physicsBrowser.status == "fixture unavailable",
      "and still names the reason on the browser surface it came from")
-- A refusal that leaves nothing typed is easy to miss in the corner, so the OS says it as well.
check(#msgboxes == 1 and msgboxes[1].title == "Ki" and
      msgboxes[1].message == "Guided: " .. tostring(missing_fixture) .. ": fixture unavailable",
      "and the OS draws the refusal as a native dialog naming what failed and why")
on.escapeKey()

steps.status = "StepCAS module is outdated or incomplete (missing caseval)"
drawn = {}
on.paint(gc)
local corner = nil
for _, line in ipairs(drawn) do
    if line:sub(1, 7) == "StepCAS" then corner = line end
end
check(corner ~= nil and #corner <= 50 and corner:find("HELP text", 1, true) ~= nil,
      "a shortened status names the key that opens its complete text: " ..
          tostring(corner))
steps.status = nil
end

-- The label says what the entry finds and the command lands in the input editor, so a beginner
-- reads the physics on the menu and sees the syntax only once it is theirs to edit.
for item = 3, #registered_menu[1] do
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
local history_before_plain = #steps.histText
type_line("factor(x^2-1)")
on.enterKey()
check(calls.giac == 2, "a plain line is evaluated by Giac")
check(#histME1 == history_before_plain + 1 and #steps.histText == history_before_plain + 1,
      "and added to the history")
check(steps.active == false, "without opening the viewer")

-- A step request goes to the module with the text after the prefix and the current variable.
local step_profile_begin_before = calls.resource_profile_begin
local step_profile_finish_before = calls.resource_profile_finish
type_line("!i 1/x")
on.enterKey()
check(calls.integrate == 1, "!i runs the integrate entry point")
check(last_args and last_args[1] == "1/x" and last_args[2] == "x", "with the text and the variable")
check(calls.giac == 2, "and not Giac")
check(#steps.histText == history_before_plain + 2 and
      steps.histText[history_before_plain + 2][2]:find("ln(x)", 1, true) ~= nil,
      "the answer goes into the history like a Giac answer")
check(steps.active == true, "and the viewer opens")
check(calls.resource_profile_begin == step_profile_begin_before + 1 and
      calls.resource_profile_finish == step_profile_finish_before and
      profile_events[#profile_events - 1] == "begin:integrate" and
      profile_events[#profile_events] == "solve:integrate",
      "a typed step solve starts profiling immediately before its native call")
check(fctEditor.editor.visible == false and histME1[1].editor.visible == false,
      "with the editors hidden underneath it")
check(histME1[1].editor.x == -10000 and fctEditor.editor.x == -10000, "and parked off screen")
-- The shell's size-change listeners call reposME after an entry lands, and on the emulator that
-- put every editor back over the viewer.
reposME()
check(histME2[2].editor.x == -10000, "and a reposition while the viewer is up keeps them parked")

local original_fill_rect = gc.fillRect
local fail_next_fill = true
gc.fillRect = function(...)
    if fail_next_fill then
        fail_next_fill = false
        error("injected result paint failure", 0)
    end
    return original_fill_rect(...)
end
drawn = {}
draw_calls = {}
on.paint(gc)
gc.fillRect = original_fill_rect
check(calls.resource_profile_finish == step_profile_finish_before and
      steps.pendingResourceProfile ~= nil and
      table.concat(drawn, "\n"):find("steps paint failed", 1, true) ~= nil,
      "a failed result paint keeps the active profile pending")
text = painted()
local step_metrics = profile_finishes[#profile_finishes]
check(calls.resource_profile_finish == step_profile_finish_before + 1 and
      step_metrics.operation == "integrate" and step_metrics.render_ready_ms == 300 and
      step_metrics.arena_nodes == 12 and step_metrics.arena_child_slots == 7 and
      step_metrics.derivation_steps == 4 and step_metrics.rewrites == 3 and
      step_metrics.backend_calls == 2,
      "the first typed result paint records its solver, arena, derivation, backend, and render metrics")
painted()
check(calls.resource_profile_finish == step_profile_finish_before + 1,
      "repainting a typed result cannot finish the same profile twice")
check(text:find("Giac's derivative agrees", 1, true) ~= nil, "the trust label is drawn")
local exact_conditional_result_class = text:find("EXACT + CONDITIONAL", 1, true) ~= nil
local saved_result_assumptions = steps.result.assumptions
steps.result.assumptions = nil
steps.result.precision = { kind = "measured", significant_digits = 3 }
local approximate_text = painted()
local approximate_result_class = approximate_text:find("APPROXIMATE", 1, true) ~= nil and
                                 approximate_text:find("EXACT", 1, true) == nil
steps.result.assumptions = saved_result_assumptions
local approximate_conditional_result_class =
    painted():find("APPROXIMATE + CONDITIONAL", 1, true) ~= nil
steps.result.precision = nil
steps.result.assumptions = nil
-- CALC-010. A linearization is an approximation away from the point, so the shell must not label
-- its answer exact just because every number in it is rational.
steps.result.approximation = true
check(painted():find("APPROXIMATE", 1, true) ~= nil and painted():find("EXACT", 1, true) == nil,
      "a linearization answer is labelled an approximation rather than exact")
steps.result.approximation = nil
check(painted():find("EXACT", 1, true) ~= nil,
      "an ordinary exact answer keeps its exact label when no approximation is declared")

-- Section 17 wants an unverified answer labelled where the student is already reading. A check that
-- could not run and a solve the student stopped are both named outcomes, so the label reads the
-- status rather than inferring one from the verification list. Globals rather than locals: this
-- chunk is at LuaJIT's ceiling of 200 in one function.
saved_result_status = steps.result.status
saved_result_outcome = steps.result.outcome
steps.result.status = "solved but unchecked"
unchecked_text = painted()
check(unchecked_text:find("EXACT + UNCHECKED", 1, true) ~= nil,
      "an answer whose check could not run is labelled unchecked beside its own class")
check(unchecked_text:find("NO RESULT", 1, true) == nil and
      unchecked_text:find("STOPPED", 1, true) == nil,
      "and is not demoted to a failure or a partial answer, since the goal itself was reached")

steps.result.status = "cancelled"
check(painted():find("STOPPED", 1, true) ~= nil,
      "a solve the student stopped reads as stopped rather than as a result that never came")
steps.result.status = "not recorded"
steps.result.outcome = "cancelled"
check(painted():find("STOPPED", 1, true) ~= nil,
      "and still does when the stop came too early for a prefix to be worth keeping")
steps.result.status = saved_result_status
steps.result.outcome = saved_result_outcome
check(painted():find("STOPPED", 1, true) == nil, "while a solve nobody stopped says nothing of the kind")

local saved_result_agreement = steps.result.agrees
steps.result.agrees = nil
steps.result.result_form = "numerical approximation"
local math015_result_forms = painted():find("APPROXIMATE", 1, true) ~= nil
steps.result.result_form = "special-function form"
steps.result.giac_form = "special-function form"
text = painted()
math015_result_forms = math015_result_forms and
                       text:find("SPECIAL FUNCTION", 1, true) ~= nil and
                       text:find("exact special-function form", 1, true) ~= nil
steps.result.result_form = "unevaluated exact form"
steps.result.giac_tag = "unevaluated"
steps.result.giac_form = "unevaluated exact form"
text = painted()
math015_result_forms = math015_result_forms and
                       text:find("UNEVALUATED EXACT", 1, true) ~= nil and
                       text:find("Giac: unevaluated exact form", 1, true) ~= nil
steps.result.result_form = "unsupported symbolic form"
steps.result.giac_form = "unsupported symbolic form"
math015_result_forms = math015_result_forms and
                       painted():find("UNSUPPORTED", 1, true) ~= nil
steps.result.result_form = "elementary closed form"
steps.result.giac_tag = "exact"
steps.result.giac_form = nil
steps.result.assumptions = saved_result_assumptions
steps.result.agrees = saved_result_agreement
evidence("MATH-015", math015_result_forms,
         "the result surface distinguishes special, unevaluated exact and unsupported forms")
local saved_result_status = steps.result.status
steps.result.agrees = nil
steps.result.status = "resource limit reached"
steps.result.giac_compare_tag = "timeout"
local verifier_failure_text = painted()
-- Through paintedRun: naming the missing check in the class word pushes the comparison onto a new row.
check(paintedRun():find("resource limit reached", 1, true) ~= nil and
      paintedRun():find("comparison: timeout", 1, true) ~= nil and
      verifier_failure_text:find("agrees", 1, true) == nil,
      "the trust line exposes an unusable terminal Giac comparison")
steps.result.agrees = saved_result_agreement
steps.result.status = saved_result_status
steps.result.giac_compare_tag = nil

-- A resource halt keeps the checked work under it, so a derivation comes back with steps and no
-- answer. A non-empty step list is not an answer, and the surface has to say which one it has.
-- Globals rather than locals: this chunk is at LuaJIT's ceiling of 200 in one function.
halted = steps.result
halted_saved = { result = halted.result, canonical = halted.canonical, solved = halted.solved,
                 display_result = halted.display_result, has_result = halted.has_result,
                 has_components = halted.has_components, has_polar = halted.has_polar,
                 status = halted.status, outcome = halted.outcome }
halted.result, halted.canonical, halted.display_result = nil, nil, nil
halted.has_result, halted.solved, halted.has_components, halted.has_polar = nil, nil, nil, nil
halted.status, halted.outcome = "resource limit reached", "resource limit reached"
halted_text = painted()
check(#(halted.steps or {}) > 0 and halted.answer_only ~= true,
      "the halted derivation carries the steps it kept and is not answer only")
check(halted_text:find("NO RESULT", 1, true) ~= nil,
      "and the surface reads a non-empty step list as work rather than as an answer")
check(halted_text:find("resource limit reached", 1, true) ~= nil,
      "and names the halt where the student is already reading")
for field, value in pairs(halted_saved) do halted[field] = value end
text = painted()
evidence("UI-013", exact_conditional_result_class and approximate_result_class and
         approximate_conditional_result_class,
         "the result trust line distinguishes exact, conditional and approximate answers")
check(text:find("STEPS  integral  x  STANDARD", 1, true) ~= nil,
      "the compact header identifies the operation, variable and detail level")
check(text:find("INPUT", 1, true) ~= nil and text:find("ANSWER", 1, true) ~= nil and
      text:find("TRUST", 1, true) ~= nil,
      "the viewer labels the input, answer and trust hierarchy")
check(text:find("ASSUMES", 1, true) ~= nil and text:find("x > 0", 1, true) ~= nil,
      "the answer's assumptions are drawn")
check(text:find("12n 4s 2g", 1, true) == nil, "diagnostic counters do not crowd the operation title")
local title_call = draw_call("STEPS", 1)
-- Located by the row it is drawn on rather than by the number it holds. The header's timing slot
-- carries the solver's time until the frame that measures the wait has landed, and the wait after.
local metrics_call = draw_call("", 2)
check(title_call and metrics_call and title_call.x + #title_call.text * 6 < metrics_call.x,
      "the header title and measurements do not overlap at 320 pixels")
-- PERF-005. What a student waits for is the solve plus the frame that shows it, so it is measured to
-- the paint and can only be printed from the next one. It is larger than the solve time it replaces
-- in the slot, which is what makes it the number the requirement asks for rather than a cheaper one.
perf005 = draw_call("", 2)
check(perf005 ~= nil and steps.result.render_ready_ms ~= nil and
      steps.result.render_ready_ms > (steps.result.total_ms or 0) and
      perf005.text:sub(1, #tostring(steps.result.render_ready_ms) + 1) ==
          tostring(steps.result.render_ready_ms) .. "r",
      "the header's timing slot holds the wait to the first readable frame, not the solve alone")
check(text:find("L/R fold", 1, true) ~= nil and text:find("ENTER details", 1, true) ~= nil,
      "the footer names the branch and detail controls")
check(text:find("Logarithmic integral", 1, true) ~= nil, "a step name is drawn in the list")
evidence("UI-012", text:find("PLAN", 1, true) ~= nil and
         text:find("WORK", 1, true) ~= nil and text:find("CHECK", 1, true) ~= nil,
         "the walkthrough labels plan, work and check phases on the visible steps")
check(text:find("[-] Integrate by rule", 1, true) ~= nil and #steps.visibleSteps == 4,
      "an expanded parent exposes its three children")
check(filled == 4 + (#steps.rows > #steps.bands and 2 or 0),
      "the background, header, footer, focused step and any scrollbar are filled")

local saved_variable = steps.variable
steps.variable = string.rep("long", 30)
text = painted()
title_call = draw_call("STEPS", 1)
metrics_call = draw_call("", 2)
check(title_call and title_call.text:sub(-3) == "..." and metrics_call and
      title_call.x + #title_call.text * 6 < metrics_call.x,
      "a long variable is bounded before it can overlap header measurements")
steps.variable = saved_variable
text = painted()

local saved_mode, saved_heap = steps.result.mode, steps.result.heap_free
steps.result.mode = "definite integral"
steps.result.heap_free = { total_kb = 18730, largest_kb = 12928 }
text = painted()
title_call = draw_call("STEPS", 1)
check(title_call and title_call.text:find("STEPS  definite integral  x", 1, true) ~= nil,
      "the definite-integral title remains readable with physical-device heap readings")
steps.result.mode, steps.result.heap_free = saved_mode, saved_heap
text = painted()

local fold_result = steps.result.result
on.arrowLeft()
text = painted()
check(steps.collapsed[1] == true and #steps.visibleSteps == 1,
      "left collapses the focused parent and removes descendants from navigation")
local collapsed_result_unchanged = steps.result.result == fold_result
check(text:find("[+] Integrate by rule", 1, true) ~= nil and
      text:find("Logarithmic integral", 1, true) == nil,
      "the closed marker replaces the hidden child rows")
on.arrowDown()
check(steps.focus == 1, "a hidden child cannot receive focus")
on.arrowRight()
text = painted()
evidence("STEP-008", collapsed_result_unchanged and steps.result.result == fold_result and
         steps.collapsed[1] == nil and #steps.visibleSteps == 4 and
         text:find("[-] Integrate by rule", 1, true) ~= nil and
         text:find("Logarithmic integral", 1, true) ~= nil,
         "collapse and expansion preserve the answer while hiding and restoring child steps")
on.arrowRight()
check(steps.focus == 2, "right on an open parent keeps moving to its first child")
on.arrowLeft()
check(steps.focus == 1, "left on a leaf keeps moving to the previous visible step")

on.arrowDown()
on.enterKey()
function scanIntegralStep()
    local lines, from, to = {}, false, false
    for i = 1, 40 do
        lines[#lines + 1] = painted()
        from = from or mathBoxExact("\226\136\171((x^(-1)), x)") ~= nil
        to = to or mathBoxExact("ln(x)") ~= nil
        on.arrowDown()
    end
    for i = 1, 40 do on.arrowUp() end
    painted()
    return table.concat(lines, "\n"), from, to
end
text, standard_from, standard_to = scanIntegralStep()
check(text:find("Step 2 of 4: Logarithmic integral", 1, true) ~= nil, "enter opens the focused step")
check(text:find("Phase: Work", 1, true) ~= nil, "the focused detail keeps its phase visible")
-- MATH-003. The label stays text and the expression goes to a math box, respelled for the eye:
-- measured on the emulator, Giac's int( renders as a flat function call and the integral sign
-- renders with its dx, so the box must hold the sign and must not hold the word.
check(text:find("Starting from:", 1, true) ~= nil and
      standard_from and
      mathBoxShowing("int((x^(-1)), x)") == nil, "with what it started from")
check(text:find("Write:", 1, true) ~= nil and standard_to, "and what it produced")
check(text:find("Normalized input: (x^(-1))", 1, true) ~= nil,
      "the focused detail presents the canonical normalized input")
evidence("MATH-012", steps.result.original_expression == "1/x" and
         steps.result.normalized_expression == "(x^(-1))" and steps.result.input == "1/x" and
         mathBoxExact("1/x") ~= nil and
         text:find("Normalized input: (x^(-1))", 1, true) ~= nil,
         "the viewer preserves the exact input while presenting its canonical normalized form")
local standard_hides_fuller = text:find("only defined for a positive", 1, true) == nil
check(standard_hides_fuller, "the standard level leaves the fuller explanation out")

rule_sequence_standard = stepRowNames()
on.backtabKey()
text, beginner_from, beginner_to = scanIntegralStep()
rule_sequence_beginner = stepRowNames()
local beginner_shows_fuller = text:find("only defined for a positive", 1, true) ~= nil
evidence("STEP-009", beginner_shows_fuller and
         text:find("Step 2 of 4: Logarithmic integral", 1, true) ~= nil,
         "the selected step opens with its fuller operation explanation")
evidence("STEP-010", standard_hides_fuller and beginner_shows_fuller,
         "standard and beginner detail levels produce distinct explanation depth")
-- Both models, because the requirement is that they agree. The result model on its own proves
-- nothing about the viewer, which never writes to it: relabelling every step as Simplify at
-- beginner detail, which is the forbidden behaviour word for word, left this row green until the
-- presentation model was compared as well.
evidence("STEP-020", steps.result.steps == fake_result.steps and
         steps.result.steps[2].rule == "i.reciprocal" and #steps.result.steps == 4 and
         rule_sequence_standard ~= "" and rule_sequence_beginner == rule_sequence_standard and
         rule_sequence_standard:find("Logarithmic integral", 1, true) ~= nil,
         "changing detail level leaves the canonical rule sequence unchanged")
check(text:find("%d+%-%d+/%d+") ~= nil and text:find("L/R step", 1, true) ~= nil,
      "the compact footer reports overflow and step navigation")
-- Down scrolls the open step, so the rest of it is reachable. The two expressions are boxes rather
-- than text, and a box that has scrolled off is parked, so whether they were shown is recorded as
-- the scroll passes them rather than looked for at the end.
local seen = text
seen_from, seen_to = beginner_from, beginner_to
for _ = 1, 6 do
    seen_from = seen_from or mathBoxExact("\226\136\171((x^(-1)), x)") ~= nil
    seen_to = seen_to or mathBoxExact("ln(x)") ~= nil
    on.arrowDown()
    seen = seen .. "\n" .. painted()
end
check(seen:find("Requires: x > 0", 1, true) ~= nil, "and scrolling reaches the step's own restriction")
evidence("UI-014", seen:find("Requires: x > 0", 1, true) ~= nil and
         seen:find("Checked by: rule-local invariant", 1, true) ~= nil,
         "the focused step exposes its own assumption and verification evidence")
evidence("UI-004", seen:find("Rule i.reciprocal", 1, true) ~= nil and
         seen:find("Starting from:", 1, true) ~= nil and seen:find("Write:", 1, true) ~= nil and
         seen_from and seen_to,
         "the focused detail identifies its rule and exact before and after expressions")
for _ = 1, 20 do on.arrowUp() end
text = painted()
check(text:find("Step 2 of 4", 1, true) ~= nil, "scrolling up past the top is safe")

on.arrowRight()
text = painted()
evidence("UI-003", text:find("Step 3 of 4", 1, true) ~= nil and
         text:find("Step 2 of 4", 1, true) == nil and text:find("L/R step", 1, true) ~= nil,
         "one focused step is shown while left and right navigation remains explicit")
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
-- STEP-016. The requirement is about the presentation a student reaches, so it is measured here
-- rather than against the golden fixture writer, which nothing outside the tests runs. Two halves,
-- the ones golden_tests words for the fixture: every line of step prose shown is one the step is
-- carrying, and the fuller explanation appears for exactly the steps that filled it.
step016 = { focus = steps.focus, view = steps.view, detail = steps.detail, scroll = steps.stepScroll,
            orphans = {}, rows_seen = 0, shown = 0, carried = 0, borrowed = 0 }

-- A projection may join two fields the step carries with punctuation of its own. It may not write a
-- third thing, so a row is compared exactly rather than searched for.
function step016Projects(row, s)
    if not s then return false end
    if row.kind == "text" then return row.text == s.name .. ": " .. s.goal end
    if row.kind == "why" then return row.text == s.short end
    if row.kind == "math" then
        if row.text == s.before or row.text == s.after or row.text == s.case then return true end
        if s.case and s.case ~= "" then
            return row.text == s.case .. "  (ruled out)" or row.text == s.case .. "  (not settled)"
        end
    end
    return false
end

function step016Sweep()
    for _, row in ipairs(steps.rows or {}) do
        step016.rows_seen = step016.rows_seen + 1
        if not step016Projects(row, (steps.result.steps or {})[row.step]) then
            step016.orphans[#step016.orphans + 1] = tostring(row.kind) .. " " .. tostring(row.text)
        end
    end
end

step016Sweep()
check(#step016.orphans == 0,
      "every row of the list is a string the step it names carries: " ..
          (step016.orphans[1] or "none"))

-- Rebuilding has to give the same list back, which is what rendering twice settles for the fixture.
-- Two toggles is the whole cycle, so the level ends where it started and buildRows has run again.
step016.rows_before = stepRowNames()
for _ = 1, #STEP_DETAILS do stepsToggleDetail() end
step016.rows_after = stepRowNames()
check(step016.rows_before ~= "" and step016.rows_before == step016.rows_after,
      "and building the list again gives the same rows back")

-- Each step opened on its own and scrolled to the bottom, since the fuller explanation sits below
-- the fold. Rows are joined on spaces because a sentence that wraps is drawn as several of them.
if STEP_DETAILS[steps.detail] ~= "beginner" then stepsToggleDetail() end
step016.step = 0
while step016.step < #steps.result.steps do
    step016.step = step016.step + 1
    steps.focus = step016.step
    steps.view = "step"
    steps.stepScroll = 0
    step016.seen = (painted():gsub("\n", " "))
    step016.scrolled = 0
    while step016.scrolled < 8 do
        on.arrowDown()
        step016.seen = step016.seen .. " " .. (painted():gsub("\n", " "))
        step016.scrolled = step016.scrolled + 1
    end
    step016.carrier = steps.result.steps[step016.step]
    if step016.carrier.detail and step016.carrier.detail ~= "" then
        step016.carried = step016.carried + 1
    end
    for _, other in ipairs(steps.result.steps) do
        if other.detail and other.detail ~= "" and
           step016.seen:find(other.detail, 1, true) ~= nil then
            if other == step016.carrier then
                step016.shown = step016.shown + 1
            else
                step016.borrowed = step016.borrowed + 1
            end
        end
    end
end
check(step016.carried > 0 and step016.shown == step016.carried and step016.borrowed == 0,
      "the fuller explanation is drawn for exactly the steps that filled it: " ..
          step016.shown .. " shown of " .. step016.carried .. " filled, " ..
          step016.borrowed .. " borrowed from another step")

steps.focus = step016.focus
steps.view = step016.view
steps.stepScroll = step016.scroll
if steps.detail ~= step016.detail then stepsToggleDetail() end
painted()

-- The fourth step is one row below the fold when the trust verdict needs two lines, which it does
-- here: the verdict used to be drawn off the right edge instead of wrapping, and reading it is
-- worth the row. Focusing the step is what has to bring it on screen, so that is what is checked,
-- and the row it lands on has to sit above the footer rather than two pixels into it.
steps.focus = 4
text = painted()
check(text:find("check: Check the answer", 1, true) ~= nil,
      "focusing the fourth step scrolls it into view at 320 by 240")
-- Held rather than indexed inline. The check above already fails when the row is gone, and a nil
-- indexed here would take the rest of the file with it, evidence write included.
fourth_row = draw_call_any("check: Check the answer")
check(fourth_row ~= nil and fourth_row.y + STEP_LINE <= 240 - STEP_LINE,
      "and it sits above the fixed footer rather than under it")
check(drawOverflow() == 0,
      "and no part of the header or the list is drawn past the right edge")
steps.focus = 1
painted()

-- Four steps fit, so the overflow case is forced rather than waited for.
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
on.escapeKey()
check(steps.active == false, "esc again closes the viewer")
check(fctEditor.editor.visible == true and histME1[1].editor.visible == true,
      "and the editors come back")
check(histME1[1].editor.x ~= -10000 and fctEditor.editor.x ~= -10000, "to their places on screen")
text = painted()
check(text:find("Giac's derivative agrees", 1, true) ~= nil, "the shell shows the last verdict")

local hint_integrate_before = calls.integrate
local hint_relative_before = calls.relative_motion
local hint_giac_before = calls.giac
type_line("!h on")
on.enterKey()
check(steps.progression == "hint" and steps.active == false,
      "!h on selects persisted hint progression before solving")
check(calls.integrate == hint_integrate_before and calls.relative_motion == hint_relative_before and
      calls.giac == hint_giac_before,
      "changing progression invokes neither a solver nor Giac")
check(runSteps("help", ""):find("!h on|off", 1, true) ~= nil,
      "the typed help makes the hint command discoverable")

local long_input = string.rep("x+", 50) .. "x"
local first_goal = fake_result.steps[1].goal
fake_result.steps[1].goal = "Canonical-first-step-" .. string.rep("x", 64)
next_step_result = fake_result
type_line("!i " .. long_input)
on.enterKey()
check(#long_input == 101 and steps.walkthrough == "hint" and steps.revealed == 1,
      "the clipping fixture is a 101-character hint-mode input")
text = painted()
local long_input_call = draw_call_any(long_input:sub(1, 12))
local long_step_call = draw_call_any("Integrate by rule: Canonical-first-step-")
-- UI-005. The typed line has no spaces in it, so before this it was one unbreakable word cut at
-- the screen edge with its tail unreachable. It now breaks by character onto a second line, and
-- the first line must not end in the ellipsis that says something was thrown away.
evidence("UI-005", long_input_call and long_input_call.text:sub(-3) ~= "..." and
         drawnJoined(long_input:sub(1, 12)):find(long_input:sub(1, 60), 1, true) ~= nil and
         drawOverflow() == 0,
         "a long unbreakable input wraps instead of losing its tail, and nothing overruns 320 pixels")
check(long_step_call and long_step_call.text:sub(-3) == "..." and
      long_step_call.x + gc:getStringWidth(long_step_call.text) <= platform.window:width(),
      "the native width metric keeps a long canonical hint row inside 320 pixels")
check(text:find("ANSWER", 1, true) == nil and text:find("TRUST", 1, true) == nil,
      "fitting long hint text does not leak the hidden result hierarchy")
on.enterKey()
text = painted()
local long_detail_call = draw_call_any("Canonical-first-step-")
-- The same 64-character unbreakable token in the open step, where there is room to wrap it. All 64
-- have to still be on screen, which is the half a truncating fit could never satisfy.
evidence("UI-005", long_detail_call and
         drawnJoined("Canonical-first-step-"):find(string.rep("x", 64), 1, true) ~= nil and
         drawOverflow() == 0,
         "an unbroken long token wraps whole in the focused step detail")
on.escapeKey()
on.escapeKey()
fake_result.steps[1].goal = first_goal

local deep_steps = {}
for depth = 0, 64 do
    deep_steps[#deep_steps + 1] = {
        kind = "transformation", name = "Depth " .. depth, goal = "legal maximum depth row",
        short = "", claim = "equivalent expression", verified = true, failed = false, depth = depth,
    }
end
local original_steps = fake_result.steps
local original_step_count = fake_result.step_count
local original_steps_truncated = fake_result.steps_truncated
local original_progression = steps.progression
fake_result.steps = deep_steps
fake_result.step_count = #deep_steps
fake_result.steps_truncated = true
steps.progression = "full"
next_step_result = fake_result
type_line("!i x")
on.enterKey()
steps.focus = #deep_steps
text = painted()
local max_depth_call = draw_call_any("Depth 64")
local truncation_call = draw_call_any("step list cut short")
local fixed_footer_call = draw_call_any("UP/DOWN select")
check(max_depth_call and steps.result.steps[steps.focus].depth == 64 and
      max_depth_call.x + gc:getStringWidth(max_depth_call.text) <= platform.window:width() - 3,
      "the legal depth-64 row is clamped to a drawable content span")
check(#steps.rows == 65 and truncation_call and fixed_footer_call and
      truncation_call.y + gc:getStringHeight(truncation_call.text) <= platform.window:height() - 15 and
      truncation_call.x + gc:getStringWidth(truncation_call.text) <= platform.window:width() and
      truncation_call.y < fixed_footer_call.y,
      "a full truncated list reserves a visible warning row above the fixed footer")
on.escapeKey()
fake_result.steps = original_steps
fake_result.step_count = original_step_count
fake_result.steps_truncated = original_steps_truncated
steps.progression = original_progression

local hint_history_before = #steps.histText
openPhysicsFixtures()
on.enterKey()
check(calls.relative_motion == hint_relative_before + 1 and steps.active == true and
      steps.walkthrough == "hint" and steps.revealed == 1 and #steps.visibleSteps == 1,
      "a guided relative-motion solve exposes exactly its first canonical hint")
check(#steps.histText == hint_history_before + 1 and
      steps.histText[#steps.histText][2] == " hint ready: 1/11 steps; Tab reveals next",
      "hint mode records a non-answer history placeholder")
text = painted()
check(text:find("Cartesian relative-motion plan", 1, true) ~= nil and
      text:find("Relative-motion vector rank", 1, true) == nil and
      text:find("Relative direction", 1, true) == nil,
      "the initial viewer draws the first canonical step and no later one")
check(text:find("ANSWER", 1, true) == nil and text:find("TRUST", 1, true) == nil and
      text:find("MEANING", 1, true) == nil and text:find("(7 ", 1, true) == nil and
      text:find("southeast", 1, true) == nil and text:find("Giac's", 1, true) == nil,
      "the initial hint leaks no answer, trust verdict, final check, or interpretation")
check(text:find("TAB next hint", 1, true) ~= nil and text:find("hint 1/11", 1, true) ~= nil,
      "the hint footer labels both progression and the next action")
local hint_title_call = draw_call("HINT", 1)
local hint_metrics_call = draw_call("", 2)
local hint_footer_call = draw_call("TAB next hint", 227)
local hint_count_call = draw_call("hint 1/11", 227)
check(hint_title_call and hint_metrics_call and
      hint_title_call.x + #hint_title_call.text * 6 < hint_metrics_call.x and
      hint_footer_call and hint_count_call and
      hint_footer_call.x + #hint_footer_call.text * 6 < hint_count_call.x and
      hint_count_call.x + #hint_count_call.text * 6 <= 320,
      "the explicit hint header and footer labels fit without overlap at 320 by 240")

on.tabKey()
check(steps.revealed == 2 and #steps.visibleSteps == 2 and steps.focus == 2,
      "one Tab reveals exactly one next canonical step")
text = painted()
check(text:find("Relative-motion vector rank", 1, true) ~= nil and
      text:find("Declared velocity frames", 1, true) == nil and
      text:find("ANSWER", 1, true) == nil,
      "the second hint is visible while the third and final answer remain hidden")
check(calls.relative_motion == hint_relative_before + 1 and calls.giac == hint_giac_before,
      "revealing a hint does not recompute or call Giac")

for expected = 3, 10 do
    on.tabKey()
    check(steps.revealed == expected and #steps.visibleSteps == expected and steps.focus == expected,
          "Tab reveals only canonical step " .. expected)
end
text = painted()
check(text:find("Relative direction", 1, true) == nil and text:find("ANSWER", 1, true) == nil and
      text:find("TRUST", 1, true) == nil and text:find("MEANING", 1, true) == nil,
      "the penultimate hint still withholds the final step and result hierarchy")

on.tabKey()
check(steps.revealed == 11 and #steps.visibleSteps == 11 and steps.focus == 11,
      "the final Tab reveals the last canonical step")
text = painted()
check(text:find("Relative direction", 1, true) ~= nil and text:find("ANSWER", 1, true) ~= nil and
      text:find("TRUST", 1, true) ~= nil and text:find("MEANING", 1, true) ~= nil and
      paintedRun():find("southeast relative to wind", 1, true) ~= nil and
      paintedRun():find("Giac's component cross-check agrees", 1, true) ~= nil,
      "the last hint reveals the answer, trust, and result interpretation together")
check(text:find("ANSWER shown", 1, true) ~= nil and text:find("hint 11/11", 1, true) ~= nil,
      "the completed hint footer states that the answer is visible")
check(calls.relative_motion == hint_relative_before + 1 and calls.giac == hint_giac_before,
      "the complete walkthrough used the original result without recomputation")
on.tabKey()
check(steps.view == "result" and steps.revealed == 11 and calls.relative_motion == hint_relative_before + 1 and
      calls.giac == hint_giac_before,
      "Tab at completion opens the full guided input without advancing or recomputing")
on.escapeKey()
check(steps.active and steps.view == "list", "Escape from the result returns to the completed hints")
on.escapeKey()

-- !h off is a setting for the next solve, so the record already on the shelf keeps the steps it
-- withheld and the message that says so. Only reopening the viewer over it can see either, and
-- without that reopen this check passed over a progression message that claimed the wrong thing.
do
    local shelved_relative = calls.relative_motion
    openPhysicsFixtures()
    on.enterKey()
    check(steps.active == true and steps.walkthrough == "hint" and steps.revealed == 1 and
          calls.relative_motion == shelved_relative + 1,
          "a fresh hint solve shelves ten of its eleven steps before hints go off")
    on.escapeKey()
    check(steps.active == false, "and its viewer is closed when the setting changes")

    type_line("!h off")
    on.enterKey()
    check(steps.progression == "full" and steps.active == false,
          "!h off restores full progression deterministically")
    check(steps.status == "walkthrough: full from the next solve",
          "and reports full progression as reaching the next solve, not the shelved record")

    type_line("!!")
    on.enterKey()
    check(steps.active == true and steps.walkthrough == "hint" and steps.revealed == 1 and
          #steps.visibleSteps == 1 and calls.relative_motion == shelved_relative + 1,
          "reopening with hints off reinstates the recorded hint walkthrough without recomputing")
    check(steps.status == "walkthrough: full from the next solve",
          "and the reopened viewer keeps the message written for that record: " ..
              tostring(steps.status))
    text = painted()
    check(text:find("HINT", 1, true) ~= nil and text:find("TAB next hint", 1, true) ~= nil and
          text:find("hint 1/11", 1, true) ~= nil,
          "the reopened viewer still labels itself a hint walkthrough")
    check(text:find("ANSWER", 1, true) == nil and text:find("TRUST", 1, true) == nil and
          paintedRun():find("southeast relative to wind", 1, true) == nil and
          paintedRun():find("(7 i - 6 j) m/s", 1, true) == nil,
          "and turning hints off releases none of the answer that record withheld")
    on.escapeKey()
    check(steps.active == false, "the reopened hint viewer closes again")
end
local full_relative_before = calls.relative_motion
local full_history_before = #steps.histText
openPhysicsFixtures()
on.enterKey()
check(calls.relative_motion == full_relative_before + 1 and steps.walkthrough == "full" and
      steps.revealed == 11 and #steps.visibleSteps == 11,
      "full mode still exposes the complete derivation immediately")
text = painted()
check(text:find("STEPS  relative_motion", 1, true) ~= nil and
      text:find("ANSWER", 1, true) ~= nil and text:find("TRUST", 1, true) ~= nil and
      text:find("MEANING", 1, true) ~= nil and
      text:find("Cartesian relative-motion plan", 1, true) ~= nil,
      "full mode preserves the complete answer and walkthrough hierarchy")
check(#steps.histText == full_history_before + 1 and
      steps.histText[#steps.histText][2]:find("(7 i - 6 j) m/s", 1, true) ~= nil,
      "full mode preserves answer history")
local full_revealed_before_tab = steps.revealed
on.tabKey()
check(steps.view == "result" and steps.revealed == full_revealed_before_tab and
      calls.relative_motion == full_relative_before + 1 and
      calls.giac == hint_giac_before,
      "Tab opens the full guided input without changing the walkthrough")
on.escapeKey()
on.escapeKey()

fake_vector_addition.mode = "vector_addition"
fake_vector_addition.input = "guided fixture"
steps.result = fake_vector_addition
steps.rows = { { step = 1, kind = "text", text = "Vector addition: Add two Cartesian vectors",
                 depth = 0, verified = true, failed = false } }
steps.focus = 1
steps.scroll = 0
steps.view = "list"
openSteps()
text = painted()
-- The basis vectors used to be picked out of the string and drawn one italic glyph at a time. The
-- OS math box italicises a lone i and j itself, so the hand-drawn run has to be gone rather than
-- kept alongside, and the answer has to reach the box as the plain ASCII letters.
local italic_count = 0
for _, style in ipairs(font_styles) do
    if style == "i" then italic_count = italic_count + 1 end
end
check(italic_count == 0 and mathBoxExact("(6.2 i + 7.0 j) m") ~= nil,
      "the vector answer reaches the math box with plain i and j and is not drawn by hand")
check(text:find("î", 1, true) == nil and text:find("ĵ", 1, true) == nil and
      text:find("\204\130", 1, true) == nil and
      mathBoxShowing("\204\130") == nil,
      "the vector viewer does not use mixed precomposed or combining-hat glyphs")
on.escapeKey()

stepsSetProgression("hint")
next_step_result = fake_answer_only
type_line("!i x*sin(x)")
on.enterKey()
check(steps.result.answer_only == true and steps.active == true and steps.walkthrough == "hint",
      "an answer-only result remains an explicitly labelled fallback in hint mode")
check(steps.histText[#steps.histText][2]:find("answer-from-giac", 1, true) ~= nil,
      "the honest answer-only Giac result remains available in history")
text = painted():gsub("\n", " ")
local answer_only_labels = 0
for _, line in ipairs(drawn) do
    if line == "no steps for this one" then answer_only_labels = answer_only_labels + 1 end
end
check(#steps.rows == 1 and answer_only_labels == 1,
      "the answer-only viewer renders exactly one no-steps label")
check(#steps.visibleSteps == 0,
      "a fallback without recorded steps has no navigable derivation")
check(text:find("ANSWER", 1, true) ~= nil and text:find("answer-from-giac", 1, true) ~= nil and
      text:find("Giac: exact", 1, true) ~= nil,
      "hint mode shows the truthful answer and trust tag when no walkthrough exists")
evidence("UI-015", steps.result.solved == false and steps.result.outcome == "unsupported form" and
         text:find("answer only", 1, true) ~= nil and text:find("1/1", 1, true) == nil and
         #steps.visibleSteps == 0 and text:find("Giac: exact", 1, true) ~= nil,
         "an unsupported answer-only outcome stays distinct and exposes no fabricated walkthrough")
on.enterKey()
check(steps.view == "list", "enter cannot open a step when the fallback has none")
on.escapeKey()
check(steps.active == false, "one escape closes the answer-only viewer")
stepsSetProgression("full")

do
    for _, progression in ipairs({ "full", "hint" }) do
        local partial = {}
        for key, value in pairs(fake_answer_only) do partial[key] = value end
        partial.status = "partially solved"
        partial.result = "ln(x)-x*cos(x)+sin(x)"
        partial.giac_raw = partial.result
        partial.step_count = 3
        partial.steps = {
            { kind = "plan", phase = "plan", name = "Integrate by rule",
              goal = "Integrate ((x^(-1)) + (x * sin(x))) with respect to x",
              verified = true, failed = false, depth = 0 },
            { kind = "transformation", phase = "integrate", name = "Sum rule",
              goal = "Integrate ((x^(-1)) + (x * sin(x)))",
              before = "int(((x^(-1)) + (x * sin(x))), x)",
              after = "(ln(x) + int((x * sin(x)), x))",
              verified = true, failed = false, depth = 1 },
            { kind = "transformation", phase = "integrate", name = "Logarithmic integral",
              goal = "Integrate (x^(-1))", before = "int((x^(-1)), x)", after = "ln(x)",
              verified = true, failed = false, depth = 2 },
        }
        stepsSetProgression(progression)
        next_step_result = partial
        type_line("!i x^-1+x*sin(x)")
        on.enterKey()
        local solved_calls = calls.integrate
        if progression == "hint" then
            check(#steps.visibleSteps == 1 and steps.revealed == 1,
                  "a partial fallback starts with one recorded hint")
            check(painted():find("ANSWER", 1, true) == nil and
                  steps.histText[#steps.histText][2]:find(partial.result, 1, true) == nil,
                  "a partial fallback does not bypass hint progression")
            on.tabKey()
            on.tabKey()
        end
        check(#steps.visibleSteps == 3 and #steps.result.steps == 3 and #steps.rows > 1,
              "a fallback answer preserves every recorded step in " .. progression .. " mode")
        local shown = painted()
        check(shown:find("no steps for this one", 1, true) == nil and
              shown:find("CAS answer", 1, true) ~= nil and
              -- Through paintedRun: the status wraps once the class word names the missing check.
              paintedRun():find("partially solved", 1, true) ~= nil and
              mathBoxShowing(partial.result) ~= nil,
              "the retained prefix and separately labelled CAS answer remain available")
        steps.focus = 1
        on.enterKey()
        check(steps.view == "step", "Enter opens a retained fallback step")
        on.escapeKey()
        on.arrowDown()
        check(steps.focus == 2, "a retained fallback prefix supports step navigation")
        steps.focus = 1
        on.arrowLeft()
        check(#steps.visibleSteps == 1, "a retained fallback prefix can fold its plan")
        on.arrowRight()
        check(#steps.visibleSteps == 3, "unfolding restores the retained fallback steps")
        painted()
        local band = (steps.bands or {})[1]
        check(band ~= nil, "a retained fallback step has a pointer target")
        if band then
            steps.focus = band.step
            on.mouseUp(100, band.top + 2)
            check(steps.view == "step", "a tap opens a retained fallback step")
            on.mouseUp(100, 100)
            check(steps.view == "list", "a tap returns from the retained fallback detail")
        end
        check(calls.integrate == solved_calls and partial.answer_only and not partial.solved,
              "fallback navigation neither recomputes nor promotes the native result")
        closeSteps()
    end
    stepsSetProgression("full")
end

-- A prefix with nothing after it sets the mode, and every line is then a request until !g.
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
check(calls.kinematics == 2, "!k runs the kinematics entry point")
check(last_args and last_args[1] == "find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s",
      "with everything after the prefix as one problem")
check(steps.active == true, "and the viewer opens")
check(steps.result.input == last_args[1],
      "a non-expression solver keeps its original physical problem in the viewer")
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
check(steps.mode == "kinematics" and calls.kinematics == 2, "a bare !k sets the kinematics mode")
type_line("find t; v = 17 m/s; v0 = 5 m/s; a = 3 m/s^2")
on.enterKey()
check(calls.kinematics == 3, "and the mode sends a bare line to kinematics")
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

-- An invalid variable is refused where it is entered, before it can poison later requests or a
-- saved document. The OS delivers enter to the focused editor's key filter rather than to
-- on.enterKey, so this drives the filter the way the calculator does. Scoped: the chunk is at the
-- 200-local ceiling.
do
    local module_solve = nps_split.solve
    local entries = #steps.histText
    type_line("!v a b")
    fctEditor.editor.filter.enterKey()
    check(steps.variable == "t" and #steps.histText == entries + 1 and
          steps.histText[#steps.histText][2]:find("single identifier", 1, true) ~= nil and
          steps.status:find("single identifier", 1, true) ~= nil,
          "!v refuses whitespace without replacing the current variable")
    type_line("!v 2x")
    fctEditor.editor.filter.enterKey()
    check(steps.variable == "t" and steps.status:find("single identifier", 1, true) ~= nil,
          "!v refuses a name that starts with a digit")
    type_line("!v x+y")
    fctEditor.editor.filter.enterKey()
    check(steps.variable == "t" and steps.status:find("single identifier", 1, true) ~= nil,
          "!v refuses punctuation inside a name")
    type_line("!v")
    fctEditor.editor.filter.enterKey()
    check(steps.variable == "t" and steps.status:find("single identifier", 1, true) ~= nil,
          "!v refuses an empty name")
    type_line("!v " .. string.rep("a", 4097))
    fctEditor.editor.filter.enterKey()
    check(steps.variable == "t" and steps.status:find("single identifier", 1, true) ~= nil,
          "!v refuses a name beyond the bridge input limit")
    check(runSteps("variable", "_velocity2") == "variable _velocity2" and
          runSteps("variable", string.rep("a", 4096)):sub(1, 9) == "variable " and
          runSteps("variable", "\207\128") == "variable \207\128" and
          runSteps("variable", "\226\136\158") == "variable \226\136\158",
          "the shell accepts every identifier form and the exact bridge length limit")
    runSteps("variable", "t")
    local solves = calls.solve
    type_line("!s 2x+5=13")
    fctEditor.editor.filter.enterKey()
    check(steps.active == true and calls.solve == solves + 1 and last_args[2] == "t",
          "the next solve reaches the module with the last valid variable")
    on.escapeKey()

    -- A raise, which is what a programming error or a hostile argument produces, must not leave
    -- the filter: outside pcall it unwinds into the OS and resets the calculator.
    nps_split.solve = function() error("bad argument #2 to 'solve' (simulated raise)", 0) end
    type_line("!s 2x+5=13")
    local survived, why = pcall(fctEditor.editor.filter.enterKey)
    check(survived, "a module raise does not escape the input editor's enter filter: " .. tostring(why))
    check(steps.active == false and type(steps.status) == "string" and
          steps.status:find("simulated raise", 1, true) ~= nil,
          "and the trapped raise is named in the status line with the viewer closed")
    type_line("!s 2x+5=13")
    survived, why = pcall(fctEditor.editor.filter.returnKey)
    check(survived, "nor the return filter: " .. tostring(why))
    check(pcall(on.paint, gc), "the shell paints after the trapped raise")
    nps_split.solve = module_solve
    steps.variable = "t"
    fctEditor.editor:setText("")
    fctEditor:fixContent()
end

-- Enter reaches this filter with a surface open only if the OS sends it to an editor the shell has
-- parked, since openSteps and openPhysicsFixtures both unfocus fctEditor and hide the editors. What
-- it does there is pinned rather than assumed: it is the shell's key, the way escape through the same
-- filter always has been.
do
    type_line("!s 2x+5=13")
    fctEditor.editor.filter.enterKey()
    local entries = #steps.histText
    check(steps.active == true and steps.view == "list",
          "the viewer opens on the list: " .. tostring(steps.status))
    fctEditor.editor.filter.enterKey()
    check(steps.view == "step" and #steps.histText == entries,
          "enter through the parked editor's filter walks into the record rather than submitting a line")
    fctEditor.editor.filter.escapeKey()
    check(steps.view == "list" and steps.active == true,
          "and escape through the same filter walks back out, which it did before enter joined it")
    on.escapeKey()

    openPhysicsFixtures()
    local browser_entries = #steps.histText
    fctEditor.editor.filter.enterKey()
    check(physicsBrowser.active == false and steps.active == true and
          #steps.histText == browser_entries + 1,
          "with the guided browser open the same key runs the focused fixture: " ..
              tostring(physicsBrowser.status))
    on.escapeKey()
    check(steps.active == false and physicsBrowser.active == false,
          "and the shell is back afterwards")
end

-- A resource refusal is typed before the bridge has an expression context to give: typed_failure
-- carries neither the original nor the normalized expression, because the input outgrew the limits
-- before either existed. It is a named halt, not a broken record, so it opens like any refusal.
do
    local refusal = { outcome = "resource exceeded",
                      detail = "the normalized expression is longer than the accepted limit",
                      solved = false, answer_only = false, status = "resource limit reached",
                      nodes = 0, step_count = 0, rewrites = 0, giac_calls = 0, steps = {},
                      steps_truncated = false }
    local module_differentiate = nps_split.differentiate
    nps_split.differentiate = function() return refusal end
    local entries = #steps.histText
    type_line("!d " .. string.rep("x*", 300) .. "x")
    on.enterKey()
    check(steps.active == true and steps.result == refusal and #steps.histText == entries + 1 and
          steps.histText[#steps.histText][2]:find("resource", 1, true) ~= nil,
          "a typed resource refusal opens the viewer and records the halt, not an invalid record: " ..
              tostring(steps.status))
    local text = painted()
    check(text:find("resource limit reached", 1, true) ~= nil and
          paintedRun():find("longer than the accepted limit", 1, true) ~= nil,
          "and the viewer names the halt and its reason")
    on.escapeKey()
    -- The guard still holds for a record that did work and lost its context on the way.
    nps_split.differentiate = function()
        return { outcome = "differentiated", solved = true, status = "solved and verified",
                 result = "2*x", steps = {}, nodes = 3, step_count = 0, rewrites = 0, giac_calls = 0 }
    end
    type_line("!d x^2")
    on.enterKey()
    check(steps.active == false and steps.status == "steps: solver returned an invalid expression context",
          "a solved record without its expression context is still refused as invalid")
    -- A module that answers with nothing at all is broken, not refusing. Every typed_failure names an
    -- outcome and a status, so a record carrying neither has not refused anything.
    nps_split.differentiate = function() return {} end
    type_line("!d x^2")
    local answered, why = pcall(on.enterKey)
    check(answered and steps.active == false and
          steps.status == "steps: solver returned an invalid expression context",
          "a record with no context, no answer and no halt is refused as invalid: " ..
              tostring(answered and steps.status or why))
    nps_split.differentiate = module_differentiate
    fctEditor.editor:setText("")
    fctEditor:fixContent()
end

-- Save and restore carry the history and the reading choices, and a restore validates its input.
local saved = on.save()
check(type(saved.history) == "table" and #saved.history == #steps.histText, "save records the history")
check(saved.variable == "t" and saved.progression == "full", "and the variable and progression setting")
check(saved.expression == nil, "and nothing for an input line the last enter already emptied")

steps.variable = "a b"
saved = on.save()
check(saved.variable == "x", "save replaces an invalid in-memory variable with the safe default")
steps.variable = "t"

fctEditor.editor:setExpression("\\0el {2*x+1}")
saved = on.save()
check(saved.expression == "2*x+1",
      "a line typed but not entered is saved too, unwrapped the way the enter key reads it")
fctEditor.editor:setText("")
fctEditor:fixContent()
saved = on.save()
check(saved.expression == nil, "and an empty math box is not a line worth keeping")

on.restore("not a table")
on.restore({ variable = 42, mode = "nonsense", detail = 0.5, progression = "automatic", history = "junk",
             expression = 42 })
check(steps.variable == "t" and steps.detail == 2 and steps.progression == "full",
      "a bad restore changes no reading or progression setting")
on.restore({ variable = "a b" })
check(steps.variable == "t", "restore refuses an invalid identifier")
on.restore({ variable = string.rep("a", 4097) })
check(steps.variable == "t", "restore refuses a variable beyond the bridge input limit")
check(steps.pendingExpression == nil, "and puts nothing on the input line either")

-- The editors' own key filter, which this harness stubbed away until now, so nothing inside it had
-- ever run here. Del removing a history entry is the shell's behaviour rather than a new binding.
-- Scoped, because this chunk is at LuaJIT's ceiling of 200 locals in one function.
do
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
end

-- A native form cycles focus and wraps at both ends rather than dead-ending on the last field.
-- Globals rather than locals: this chunk is at LuaJIT's ceiling of 200 in one function.
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
on.restore({ variable = "y", mode = "solve", detail = 1, progression = "hint",
             history = { { " a", " b" }, { 1, 2 }, "x" }, expression = "3*x" })
check(steps.variable == "y" and steps.mode == "solve" and steps.detail == 1 and
      steps.progression == "hint",
      "a good restore brings back the choices and hint progression")
check(steps.pendingHistory and #steps.pendingHistory == 1, "and keeps only the well formed history")
check(steps.pendingExpression == "3*x", "and the line the student had not entered yet")
check(fctEditor:getExpression() == "", "which is not on screen before a paint has run")
text = painted()
check(#histME1 == 1 and steps.pendingHistory == nil, "which the next paint puts back on screen")
check(steps.pendingExpression == nil and fctEditor:getExpression() == "3*x",
      "and the same paint puts the unentered line back where the student left it")

-- The shell's own Giac channel is the module's caseval now rather than luagiac's.
check(calls.giac > 0, "plain lines went through the module's caseval")

-- UI-001, the touchpad half. Before this every tap in the viewer reached the shell widgets that
-- openSteps had just parked off screen, so the pointer did nothing. The gesture under test is the
-- native list one: a tap selects, and a tap on the selection confirms.
--
-- Scratch values hang off this table rather than on locals, because this chunk is at LuaJIT's
-- ceiling of 200 and one more name fails the whole file.
mathcase = {}
do
    -- Driven through a real request rather than by assembling the rows by hand, so the bands under
    -- test are the ones a student's own solve produces.
    stepsSetProgression("full")
    next_step_result = fake_result
    type_line("!i 1/x")
    on.enterKey()
    steps.focus = 1
    steps.scroll = 0
    steps.view = "list"
    painted()
    check(type(steps.bands) == "table" and #steps.bands > 1,
          "the list records where its rows were drawn")

    -- A tap on a row that is not selected only selects it. This is the check that matters: if a
    -- single tap both moved and confirmed, a mis-aimed tap in the guided browser would launch a
    -- solve the student never chose.
    mathcase.other = nil
    for _, band in ipairs(steps.bands) do
        if band.step ~= steps.focus then mathcase.other = band end
    end
    check(mathcase.other ~= nil, "and more than one step is on screen to tap between")
    on.mouseUp(100, mathcase.other.top + 2)
    check(steps.focus == mathcase.other.step and steps.view == "list",
          "a tap on an unselected step selects it and does not open it")

    -- The second tap on the same row is the confirm.
    painted()
    on.mouseUp(100, bandTopFor(steps.focus) + 2)
    check(steps.view == "step", "a second tap on the selected step opens it")
    on.mouseUp(100, 100)
    check(steps.view == "list", "and a tap in the open step goes back to the list")

    -- A tap on nothing is not a confirm either.
    painted()
    mathcase.before_focus = steps.focus
    on.mouseUp(100, 239)
    check(steps.focus == mathcase.before_focus and steps.view == "list",
          "a tap on the footer changes neither the selection nor the view")
    closeSteps()

    -- The same rule in the guided browser, where a stray confirm costs a solve rather than a view.
    openPhysicsFixtures()
    physicsBrowser.focus = 1
    painted()
    mathcase.calls_before = calls.density
    mathcase.row2 = nil
    for _, band in ipairs(physicsBrowser.bands) do
        if band.index == 2 then mathcase.row2 = band end
    end
    on.mouseUp(100, mathcase.row2.top + 2)
    check(physicsBrowser.focus == 2 and physicsBrowser.active == true and
          calls.density == mathcase.calls_before,
          "a tap on a guided entry selects it and runs nothing")
    painted()
    on.mouseUp(100, mathcase.row2.top + 2)
    check(calls.density == mathcase.calls_before + 1 and steps.active == true,
          "and the tap that lands on the selection is the one that runs it")
    on.escapeKey()
end

-- Every guided entry has to tell a student who has not taken the course what it does. That is a
-- judgement, but two halves of it are mechanical and are the halves that rot: a label full of the
-- vocabulary the student came here to learn, and a label the screen cuts off.
do
    openPhysicsFixtures()
    physicsBrowser.focus = 1
    painted()
    check(drawOverflow() == 0, "no guided label is drawn past the right edge of the screen")
    for index = 1, #PHYSICS_FIXTURES do
        physicsBrowser.focus = index
        painted()
        check(drawOverflow() == 0,
              "guided entry " .. index .. " and its explanation stay inside 320 pixels")
        local label = PHYSICS_FIXTURES[index].label
        for _, jargon in ipairs({ "Cartesian", "agnitude", "elocity", "ector sum", "imension mis",
                                  "omponents", "onstant acceleration", "Boreal", "Atlas", "Drone" }) do
            check(label:find(jargon, 1, true) == nil,
                  "guided entry " .. index .. " names what it does rather than saying " .. jargon)
        end
        check(#PHYSICS_FIXTURES[index].problem > #label,
              "guided entry " .. index .. " explains itself at more length than its own title")
        -- The footer marks its own overflow, so an explanation that outgrew the space says so.
        -- None of them may be doing that: the explanation is the whole point of the entry.
        check(draw_call_any("...") == nil,
              "guided entry " .. index .. " fits its whole explanation in the footer")
    end
    physicsBrowser.focus = 1
    on.escapeKey()
end

-- MATH-011's templates. What matters is not that a string was inserted but that the caret lands
-- inside the shape, that what comes back out is the linear syntax the engine reads, and that an
-- entry which types cannot fire while the line it types into is parked off screen.
do
    check(("a∫πz"):usub(2, 3) == "∫π" and ("a∫πz"):usub(-2) == "πz"
          and ("a∫πz"):usub(5) == "" and ("a∫πz"):usub(0, 99) == "a∫πz",
          "the native string fixture slices Unicode characters at caret boundaries")
    fctEditor.editor:setExpression("\\0el {}")
    fctEditor:fixContent()
    main_menu[2][2][2]()
    check(fctEditor:getExpression() == "()/()",
          "the fraction template inserts the linear form the engine reads")
    fctEditor:addString("7")
    check(fctEditor:getExpression() == "(7)/()",
          "and leaves the caret in the numerator rather than after the whole shape")

    fctEditor.editor:setExpression("\\0el {}")
    fctEditor:fixContent()
    main_menu[2][7][2]()
    fctEditor:addString("x^2")
    check(fctEditor:getExpression() == "∫(x^2,x)",
          "the integral template leaves the caret where the integrand goes")

    -- Every entry in the box, driven rather than read: none may leave the wrapper in the text the
    -- engine would be handed, which is what an insert that forgot the unwrapper would do.
    for item = 2, #main_menu[2] do
        if main_menu[2][item] ~= "-" then
            fctEditor.editor:setExpression("\\0el {}")
            fctEditor:fixContent()
            main_menu[2][item][2]()
            check(fctEditor:getExpression():find("\\0el", 1, true) == nil and
                  fctEditor:getExpression() ~= "",
                  "template " .. main_menu[2][item][1] .. " yields plain linear syntax")
        end
    end

    -- Every kind the requirement names, driven rather than read. The loop above proves a template
    -- yields linear syntax; these prove the caret lands in the slot the student fills first, which
    -- is what makes a skeleton a template rather than an insert. The last two have no slot, and a
    -- caret move there would be the defect: an equals sign and a unit belong after what is typed.
    -- Globals rather than locals, the 200-local ceiling again.
    math011 = {
        { 2, "", "7", "(7)/()" }, { 3, "", "2", "^(2)" }, { 4, "", "x", "sqrt(x)" },
        { 5, "", "x", "surd(x,3)" }, { 6, "", "x^2", "(x^2,x)" },
        { 7, "", "x^2", "∫(x^2,x)" }, { 8, "", "x", "∫(x,x,0,1)" },
        { 9, "x", "5", "x=5" }, { 11, "5", "", "5 m" },
        { 18, "", "x^2", "limit(x^2,x,0)" },
        { 19, "", "1/x", "limit(1/x,x,0,-1)" },
        { 20, "", "1/x", "limit(1/x,x,0,1)" },
        { 21, "", "1/x", "limit(1/x,x,infinity)" },
        { 22, "", "1/x", "limit(1/x,x,-infinity)" },
    }
    math011_built = true
    for _, shape in ipairs(math011) do
        fctEditor.editor:setExpression("\\0el {}")
        fctEditor:fixContent()
        if shape[2] ~= "" then fctEditor:addString(shape[2]) end
        main_menu[2][shape[1]][2]()
        if shape[3] ~= "" then fctEditor:addString(shape[3]) end
        math011_built = math011_built and fctEditor:getExpression() == shape[4]
        check(fctEditor:getExpression() == shape[4],
              "the " .. main_menu[2][shape[1]][1]:lower() .. " template builds " .. shape[4])
    end

    -- The other half of the requirement's sentence. A template is only worth having if what it
    -- makes is the linear syntax the keypad makes, so the engine has to be handed exactly what a
    -- student would have typed rather than a shape only the editor understands.
    math011_calls = calls.integrate
    fctEditor.editor:setExpression("\\0el {}")
    fctEditor:fixContent()
    fctEditor:addString("!i ")
    main_menu[2][4][2]()
    fctEditor:addString("x")
    next_step_result = fake_result
    on.enterKey()
    math011_read = calls.integrate == math011_calls + 1 and last_args ~= nil and
                   last_args[1] == "sqrt(x)"
    check(math011_read,
          "a line the templates built reaches the engine as the linear syntax a student would type")
    if steps.active then closeSteps() end

    -- The palette opens over the viewer, where the entry line is parked at -10000. Typing into it
    -- there would be invisible, so the entry has to decline and say why.
    fctEditor.editor:setExpression("\\0el {}")
    fctEditor:fixContent()
    steps.result = fake_result
    openSteps()
    main_menu[2][2][2]()
    check(fctEditor:getExpression() == "" and steps.status ~= nil and
          steps.status:find("close the steps", 1, true) ~= nil,
          "a template declines while the viewer has the entry line parked, and says so")
    closeSteps()
    fctEditor.editor:setExpression("\\0el {}")
    fctEditor:fixContent()
    evidence("MATH-011", math011_built and math011_read and #math011 == 14,
             "the entry line takes keypad-friendly linear syntax and carries a template for every "
             .. "kind the requirement names: fractions, powers, square and other roots, "
             .. "derivatives, indefinite and definite integrals, equations and units, each driven "
             .. "here rather than read, each leaving the caret in the slot the student fills first "
             .. "where it has one, and what one built reaching the engine as the same linear text a "
             .. "student would have typed")
end

-- The display layer's own hard cases. The happy path is covered above by the physics answers; these
-- are the ones that decide whether it is robust: a form taller than a line, one wider than the
-- screen at every font it may try, a word that only looks like a rewritable one, and the widgets
-- left behind on the way out.
--
-- They sit here rather than at the end of the file deliberately. Everything below loads isolated
-- documents into the same global `on`, so a paint down there is whichever document loaded last and
-- these checks would be judging a different one.
--
-- Globals rather than locals: this chunk is at LuaJIT's ceiling of 200 and a do block still needs a
-- free slot for each name it introduces.
do
    steps.result = fake_result
    steps.rows = { { step = 1, kind = "text", text = "Only row", depth = 0, verified = true,
                     failed = false } }
    steps.focus = 1
    steps.scroll = 0
    steps.view = "list"
    -- The answer is withheld in hint mode until the last step is out, and every case here is about
    -- the answer, so the walkthrough goes back to full first.
    steps.walkthrough = "full"

    -- A stacked form is two lines tall in the stub, as a fraction is on the calculator. The header
    -- has to grow with it, or the row underneath is drawn over the bottom half of the answer.
    steps.result.display_result = "(x^2+1)/(x-3)"
    openSteps()
    painted()
    painted()
    mathcase.tall_header = steps.headerHeight
    mathcase.tall_box = mathBoxExact("(x^2+1)/(x-3)")
    steps.result.display_result = "17"
    painted()
    painted()
    mathcase.short_header = steps.headerHeight
    check(mathcase.tall_box ~= nil and mathcase.tall_header > mathcase.short_header,
          "a stacked answer makes the header taller and a flat one does not")
    check(mathBoxExact("(x^2+1)/(x-3)") == nil and mathBoxExact("17") ~= nil,
          "and changing the answer re-sets the same box rather than leaving the old one up")

    -- Wider than the header at every font in the ladder: 2000 characters are thousands of pixels
    -- even at the smallest. The box has to give up and the text has to carry it, and the part that
    -- does not fit has to be declared rather than quietly dropped.
    steps.result.display_result = string.rep("q", 2000)
    painted()
    painted()
    draw_calls = {}
    painted()
    check(mathBoxExact(string.rep("q", 2000)) == nil,
          "an answer too tall at the smallest font is not left as a clipped box")
    check(drawOverflow() == 0 and drawnJoined("qqq"):find(string.rep("q", 100), 1, true) ~= nil and
          draw_call_any("(") ~= nil,
          "it falls back to wrapped text inside 320 pixels and says how much did not fit")

    -- This answer fits only after remeasurement at the smaller supported editor font.
    steps.result.display_result = string.rep("q", 47)
    painted()
    mathcase.shrunk = mathBoxExact(string.rep("q", 47))
    check(mathcase.shrunk ~= nil and mathcase.shrunk.font < 9,
          "an answer that only fits at a smaller size gets one rather than the text fallback")

    -- One step past the ladder: 60 characters are 280 pixels even at font 7, against 266. A box
    -- here is truncated on the right with nothing to say so and no way to reach the rest, which is
    -- what the emulator did to a long unit quantity before the width was checked at all.
    steps.result.display_result = string.rep("w", 60)
    painted()
    draw_calls = {}
    painted()
    check(mathBoxExact(string.rep("w", 60)) == nil,
          "an answer too wide at the smallest font is not left as a box cut off at the edge")
    check(drawOverflow() == 0 and drawnJoined("www"):find(string.rep("w", 60), 1, true) ~= nil,
          "and every character of it is on screen as wrapped text instead")

    -- A word that starts with the rewritten one keeps its own name, or a variable called interval
    -- would be drawn as an integral sign followed by erval.
    steps.result.display_result = "interval + int(x,x) + printf"
    painted()
    check(mathBoxExact("interval + \226\136\171(x,x) + printf") ~= nil,
          "only a whole word followed by a bracket is respelled for display")

    steps.result.display_result = nil
    closeSteps()
    check(mathBoxShowing("interval") == nil,
          "closing the viewer parks its boxes, which the canvas underneath cannot paint over")
end

do
    mathcase.saved_result = steps.result
    for _, field in ipairs({ "before", "after", "case" }) do
        mathcase.long_result = {}
        for key, value in pairs(fake_result) do mathcase.long_result[key] = value end
        mathcase.long_step = {}
        for key, value in pairs(fake_result.steps[2]) do mathcase.long_step[key] = value end
        mathcase.long_step.before = nil
        mathcase.long_step.after = nil
        mathcase.long_step[field] = string.rep("x+", 1000) .. "tail_marker"
        if field == "case" then mathcase.long_step.kind = "branch" end
        mathcase.long_result.steps = { mathcase.long_step }
        steps.result = mathcase.long_result
        steps.focus = 1
        steps.view = "step"
        steps.stepScroll = 0
        steps.walkthrough = "full"
        openSteps()
        mathcase.saw_head = false
        mathcase.saw_tail = false
        mathcase.overflows = 0
        for _ = 1, 120 do
            mathcase.page = painted():gsub("\n", "")
            mathcase.saw_head = mathcase.saw_head or mathcase.page:find("x+x+x+", 1, true) ~= nil
            mathcase.saw_tail = mathcase.saw_tail or mathcase.page:find("tail_marker", 1, true) ~= nil
            mathcase.overflows = mathcase.overflows + drawOverflow()
            on.arrowDown()
        end
        check(mathcase.saw_head and mathcase.saw_tail,
              "every part of a long " .. field .. " expression is reachable by scrolling")
        check(mathcase.overflows == 0,
              "a long " .. field .. " expression stays within the display width")
        closeSteps()
    end
    steps.result = mathcase.saved_result
end

do
    for _, base in ipairs({ fake_answer_only, fake_result }) do
        mathcase.full_result = {}
        for key, value in pairs(base) do mathcase.full_result[key] = value end
        mathcase.full_result.display_result = string.rep("x+", 1000) .. "answer_tail"
        mathcase.full_result.giac_raw = mathcase.full_result.display_result
        mathcase.full_result.detail = string.rep("explanation ", 100) .. "reason_tail"
        mathcase.full_result.assumptions = string.rep("a and ", 100) .. "assumption_tail"
        mathcase.full_result.interpretation = string.rep("positive direction ", 50) .. "meaning_tail"
        stepsSetProgression("full")
        next_step_result = mathcase.full_result
        type_line("!i x*sin(x)")
        on.enterKey()
        mathcase.solve_calls = calls.integrate
        painted()
        painted()
        check(steps.headerHeight <= platform.window:height() - 3 * STEP_LINE,
              "long result metadata leaves room for the walkthrough and footer")
        check(paintedRun():find("TAB result", 1, true) ~= nil,
              "a shortened result advertises its complete view")
        on.tabKey()
        check(steps.view == "result", "Tab opens the complete result without inventing a step")
        mathcase.seen = ""
        mathcase.overflows = 0
        for _ = 1, 200 do
            painted()
            for _, draw in ipairs(draw_calls) do
                if draw.y >= STEP_LINE and draw.y + STEP_LINE <= platform.window:height() - STEP_LINE then
                    mathcase.seen = mathcase.seen .. draw.text
                end
            end
            mathcase.overflows = mathcase.overflows + drawOverflow()
            on.arrowDown()
        end
        check(mathcase.seen:find("answer_tail", 1, true) ~= nil and
              mathcase.seen:find("assumption_tail", 1, true) ~= nil and
              mathcase.seen:find("meaning_tail", 1, true) ~= nil,
              "the complete answer, assumptions and interpretation are reachable on screen")
        if not base.solved then
            check(mathcase.seen:find("reason_tail", 1, true) ~= nil and
                  mathcase.seen:find("CAS answer", 1, true) ~= nil,
                  "a complete fallback retains its source label and refusal reason")
        end
        check(mathcase.overflows == 0, "complete result pages stay within the display width")
        on.escapeKey()
        check(steps.active and steps.view == "list", "Escape returns from the result to the list")
        painted()
        on.tabKey()
        check(steps.view == "result" and steps.stepScroll == 0,
              "reopening the complete result starts at its beginning")
        on.mouseUp(100, 100)
        check(steps.view == "list", "a tap returns from the complete result")
        check(calls.integrate == mathcase.solve_calls,
              "reading the complete result never calls the solver again")
        closeSteps()
    end
    stepsSetProgression("hint")
    next_step_result = mathcase.full_result
    type_line("!i x*sin(x)")
    on.enterKey()
    for exposed = 1, #mathcase.full_result.steps - 1 do
        painted()
        check(steps.view == "list" and steps.revealed == exposed and
              paintedRun():find("answer_tail", 1, true) == nil,
              "a long result stays hidden until every hint is revealed")
        on.tabKey()
    end
    painted()
    on.tabKey()
    check(steps.view == "result" and steps.revealed == #mathcase.full_result.steps,
          "Tab opens the complete result only after the final hint")
    on.tabKey()
    check(steps.view == "list", "Tab also returns from the complete result")
    closeSteps()
    stepsSetProgression("full")
    mathcase.input_result = {}
    for key, value in pairs(fake_answer_only) do mathcase.input_result[key] = value end
    mathcase.input_result.display_result = "2"
    mathcase.input_result.giac_raw = "2"
    mathcase.input_result.detail = "unsupported form"
    next_step_result = mathcase.input_result
    type_line("!i " .. string.rep("x+", 150) .. "input_tail")
    on.enterKey()
    painted()
    check(paintedRun():find("TAB result", 1, true) ~= nil,
          "a clipped input advertises its complete view even when the answer fits")
    on.tabKey()
    check(steps.view == "result", "a clipped input opens the complete result")
    mathcase.saw_input_tail = false
    for _ = 1, 25 do
        painted()
        for _, draw in ipairs(draw_calls) do
            if draw.y >= STEP_LINE and draw.y + STEP_LINE <= platform.window:height() - STEP_LINE then
                mathcase.saw_input_tail = mathcase.saw_input_tail or draw.text:find("input_tail", 1, true) ~= nil
            end
        end
        on.arrowDown()
    end
    check(mathcase.saw_input_tail, "the full original input is reachable by scrolling")
    closeSteps()
end

local function copyModule()
    local module = {}
    for name, entry in pairs(nps_split) do module[name] = entry end
    return module
end

local function copyManifest()
    local manifest = {}
    for name, entry in pairs(fake_manifest) do manifest[name] = entry end
    manifest.symbolic_backend = {}
    for name, entry in pairs(fake_manifest.symbolic_backend) do manifest.symbolic_backend[name] = entry end
    manifest.schema_versions = {}
    for index, entry in ipairs(fake_manifest.schema_versions) do
        manifest.schema_versions[index] = { id = entry.id, version = entry.version }
    end
    manifest.installed_modules = {}
    for index, entry in ipairs(fake_manifest.installed_modules) do
        manifest.installed_modules[index] = { kind = entry.kind, id = entry.id }
    end
    return manifest
end

local function loadIsolated(module, loadFailure, withoutNdl)
    local names = {}
    local env = setmetatable({ print = function() end, mathEvalStrCalls = 0, math = {} }, { __index = _G })
    for name, entry in pairs(math) do env.math[name] = entry end
    env.math.evalStr = function()
        env.mathEvalStrCalls = env.mathEvalStrCalls + 1
        return "unsafe fallback"
    end
    env.nrequire = function(name)
        names[#names + 1] = name
        if loadFailure then error(loadFailure, 0) end
        if name ~= "nps_nspire" then error("no module named " .. tostring(name), 0) end
        env.nps_nspire = module
        return module
    end
    -- Ndl registers nrequire, so a calculator without Ndl is one where it is not a function.
    -- Shadowed with false rather than nil, because nil would resolve through __index to the real one.
    if withoutNdl then env.nrequire = false end
    -- Its own handler table. Without this the chunk's `on` resolves through __index to the shared
    -- global one, every isolated document overwrites the last, and a check made after a second load
    -- drives whichever document loaded most recently. Measured: PLAT-006 typed into this document's
    -- editor and pressed a handler belonging to the document loaded after it, which read its own
    -- empty editor and did nothing, so the row's no-fallback half was met by a solve that never ran.
    local chunk = assert(loadfile("lua/nps_v4.lua"))
    env.on = {}
    setfenv(chunk, env)
    local ok, loadError = pcall(chunk)
    return env, names, registered_menu, ok, loadError
end

do
    local module = copyModule()
    local display_calls = 0
    module.math_display = function(expression)
        display_calls = display_calls + 1
        if expression == "(1 * (2^(-1)))" then return "(1 / 2)" end
        if expression == "(3 * (4^(-1)))" then error("display unavailable") end
        return nil, "display refused"
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.fctEditor.editor:setExpression("\\0el {!s 2*x+5=13}")
    env.on.enterKey()
    env.steps.result.canonical = "(1 * (2^(-1)))"
    for _ = 1, 3 do env.on.paint(gc) end
    check(mathBoxExact("(1 / 2)") ~= nil and env.steps.result.canonical == "(1 * (2^(-1)))",
          "native math display changes notation without rewriting the recorded answer")
    local formatted, solves, backend_calls = display_calls, calls.solve, calls.giac
    for _ = 1, 10 do env.on.paint(gc) end
    check(formatted > 0 and display_calls == formatted and calls.solve == solves and calls.giac == backend_calls,
          "unchanged paints reuse math formatting without a solver or backend call")
    env.steps.result.canonical = "(3 * (4^(-1)))"
    for _ = 1, 3 do env.on.paint(gc) end
    check(mathBoxExact("(3 * (4^(-1)))") ~= nil,
          "a formatter failure preserves the complete original math expression")
    env.on.escapeKey()
end

-- Criterion 11 on the launch screen: a refusal must never paint as success. Takes the document it
-- judges rather than reaching for the global handler table, so that it paints the load it was
-- called about whatever has been loaded since.
-- The remedy is checked against the failure rather than against a constant, because the two came
-- apart: telling a reader with no Ndl to install the module names a cure for a different illness.
function namesItsRefusal(env, expected, what, remedy)
    drawn = {}
    local painted_ok = pcall(env.on.paint, gc)
    local screen = table.concat(drawn, "\n")
    check(painted_ok and screen:find(expected, 1, true) ~= nil,
          "the launch screen names its refusal for " .. what .. ": " .. screen:sub(1, 140))
    check(screen:find(remedy or "Install the matching module", 1, true) ~= nil,
          "and says what to do about it for " .. what)
    return screen
end

-- A palette entry that refuses leaves nothing typed behind, so unless the refusal reaches the
-- history it lives only in the corner and is gone with the next status line.
function paletteRefusalIsRecorded(env, expected)
    if not pcall(env.on.paint, gc) then return false end
    local before = #env.steps.histText
    local called = pcall(env.openPhysicsFixtures)
    local pair = env.steps.histText[#env.steps.histText]
    return called and env.physicsBrowser.active == false and
           #env.steps.histText == before + 1 and pair[1] == " Guided physics" and
           pair[2]:find(expected, 1, true) ~= nil and env.steps.status == pair[2]:sub(2)
end

local function plainInputDoesNotFallback(env)
    local giacBefore = calls.giac
    drawn = {}
    draw_calls = {}
    local paintOk = pcall(env.on.paint, gc)
    if not paintOk or not env.fctEditor then return false end
    local recoveryText = table.concat(drawn, "\n")
    env.fctEditor.editor:setExpression("\\0el {1+1}")
    local enterOk = pcall(env.on.enterKey)
    return enterOk and env.mathEvalStrCalls == 0 and calls.giac == giacBefore, recoveryText
end

local release_module = copyModule()
release_module.resource_profile_begin = nil
release_module.resource_profile_finish = nil
local release_begin_before = calls.resource_profile_begin
local release_finish_before = calls.resource_profile_finish
local release_env, _, _, release_ok, release_error = loadIsolated(release_module)
local release_shell_ok = release_ok and pcall(release_env.on.paint, gc)
local release_answer = release_shell_ok and release_env.runSteps("integrate", "1/x")
local release_result_ok = release_answer and pcall(release_env.on.paint, gc)
check(release_result_ok and release_env.steps.active == true and
      calls.resource_profile_begin == release_begin_before and
      calls.resource_profile_finish == release_finish_before,
      "a normal release module runs and renders without either diagnostic profile export: " ..
          tostring(release_error))

assert((function()
    local rejected_module = copyModule()
    local rejected_begins = 0
    local rejected_finishes = 0
    rejected_module.resource_profile_begin = function()
        rejected_begins = rejected_begins + 1
        return false
    end
    rejected_module.resource_profile_finish = function()
        rejected_finishes = rejected_finishes + 1
    end
    local rejected_env, _, _, rejected_ok, rejected_error = loadIsolated(rejected_module)
    local rejected_shell_ok = rejected_ok and pcall(rejected_env.on.paint, gc)
    local rejected_answer = rejected_shell_ok and rejected_env.runSteps("integrate", "1/x")
    local rejected_result_ok = rejected_answer and pcall(rejected_env.on.paint, gc)
    check(rejected_result_ok and rejected_env.steps.active == true and rejected_begins == 1 and
          rejected_finishes == 0 and
          rejected_env.steps.resourceProfileError == "profile begin rejected operation",
          "a rejected diagnostic interval cannot suppress a solve or emit a false report: " ..
              tostring(rejected_error))
    return true
end)())

local missing_integrity_module = copyModule()
missing_integrity_module.integrity_status = nil
local missing_integrity_manifest_before = calls.manifest
local missing_integrity_env, _, _, missing_integrity_ok, missing_integrity_error =
    loadIsolated(missing_integrity_module)
check(missing_integrity_ok and calls.manifest == missing_integrity_manifest_before and
      missing_integrity_env.hasGiac == false and missing_integrity_env.hasSteps == false and
      missing_integrity_env.runSteps("integrate", "1/x") ==
          "StepCAS module incompatible (missing integrity status)",
      "a missing integrity status disables both native surfaces before reading the manifest: " ..
          tostring(missing_integrity_error))
namesItsRefusal(missing_integrity_env, "StepCAS module incompatible", "a missing integrity status")
msgbox_before_integrity = #msgboxes
check(paletteRefusalIsRecorded(missing_integrity_env, "StepCAS module incompatible"),
      "the Guided physics entry records its refusal in the history rather than only in the corner")
check(#msgboxes == msgbox_before_integrity + 1 and
      msgboxes[#msgboxes].message:find("StepCAS module incompatible", 1, true) ~= nil,
      "and the OS says it too, since the module loaded well enough to carry the dialog")

-- A module too old to carry the dialog still has to record the refusal, which is the durable half.
do
local no_dialog_module = copyModule()
no_dialog_module.os_msgbox = nil
no_dialog_module.integrity_status = nil
local no_dialog_env = loadIsolated(no_dialog_module)
local no_dialog_before = #msgboxes
check(paletteRefusalIsRecorded(no_dialog_env, "StepCAS module incompatible"),
      "a module with no os_msgbox still records the refusal in the history")
check(#msgboxes == no_dialog_before,
      "and asks for no dialog rather than failing on the binding it does not have")
end
local missing_integrity_no_fallback, missing_integrity_recovery =
    plainInputDoesNotFallback(missing_integrity_env)
check(missing_integrity_no_fallback,
      "a missing integrity status cannot fall back to math.evalStr or caseval")
evidence("PLAT-006", missing_integrity_no_fallback and
         type(missing_integrity_recovery) == "string" and
         missing_integrity_recovery:find("StepCAS module incompatible", 1, true) ~= nil and
         missing_integrity_recovery:find("Install the matching module and digest.", 1, true) ~= nil,
         "an incompatible bridge paints a recovery instruction instead of crashing")

local malformed_integrity_module = copyModule()
malformed_integrity_module.integrity_status = function() return {} end
local malformed_integrity_env, _, _, malformed_integrity_ok, malformed_integrity_error =
    loadIsolated(malformed_integrity_module)
check(malformed_integrity_ok and malformed_integrity_env.hasGiac == false and
      malformed_integrity_env.hasSteps == false and
      malformed_integrity_env.runSteps("integrate", "1/x") == "StepCAS integrity status malformed",
      "a malformed integrity status disables both native surfaces: " .. tostring(malformed_integrity_error))
namesItsRefusal(malformed_integrity_env, "StepCAS integrity status malformed",
                "a malformed integrity status")
local malformed_integrity_no_fallback = plainInputDoesNotFallback(malformed_integrity_env)
check(malformed_integrity_no_fallback,
      "a malformed integrity status cannot fall back to math.evalStr or caseval")

local failed_integrity_module = copyModule()
local reduced_integrity_manifest = copyManifest()
reduced_integrity_manifest.installed_modules = {}
failed_integrity_module.integrity_status = function() return "mismatch" end
failed_integrity_module.capability_manifest = function() return reduced_integrity_manifest end
local failed_integrity_env, _, _, failed_integrity_ok, failed_integrity_error =
    loadIsolated(failed_integrity_module)
check(failed_integrity_ok and failed_integrity_env.hasGiac == false and
      failed_integrity_env.hasSteps == false and
      failed_integrity_env.runSteps("integrate", "1/x") == "StepCAS unavailable (integrity: mismatch)",
      "a non-verified integrity status disables both native surfaces: " .. tostring(failed_integrity_error))
namesItsRefusal(failed_integrity_env, "StepCAS unavailable (integrity: mismatch)",
                "a failed integrity check")
local failed_integrity_no_fallback = plainInputDoesNotFallback(failed_integrity_env)
evidence("PLAT-012", failed_integrity_no_fallback and failed_integrity_env.hasGiac == false and
         failed_integrity_env.hasSteps == false and
         #failed_integrity_module.capability_manifest().installed_modules == 0,
         "a corrupt runtime exposes a reduced manifest and permits no local or outside fallback")

-- PERF-010's launch reading. The budgets lane reads it off the launch screen on the handheld, where
-- keysvc cannot press an arrow, so it has to stay reachable without one. Driven on an isolated
-- module rather than the shared fake, because a heap reading also joins the viewer's header metrics
-- and this harness cannot settle that layout: see the note on the header in task #13.
do
    local heap_module = copyModule()
    heap_module.heap_free = function() return { total_kb = 19609, largest_kb = 13120 } end
    local heap_env, _, _, heap_ok, heap_error = loadIsolated(heap_module)
    check(heap_ok, "a module reporting free heap loads: " .. tostring(heap_error))
    drawn = {}
    pcall(heap_env.on.paint, gc)
    local screen = table.concat(drawn, "\n")
    check(screen:find("free 19609/13120k", 1, true) ~= nil,
          "the launch reading stays on the first screen, reachable without an arrow key: " ..
              screen:sub(1, 120))
end

-- PLAT-001's other half. A model the OS disagrees with is the reading being wrong rather than one
-- more field in it, so it is said in words on the same screen instead of being folded into the line
-- above, where a reader with no reason to look would take the model on trust.
do
    local mismatch_module = copyModule()
    mismatch_module.device_identity = function()
        return { model = "cx", cas = "non-cas", os = "6.4.0.74", os_index = 1, hardware_type = 1,
                 hardware_subtype = 1, ndl_revision = 2022, third_party_loader = false,
                 model_agrees_with_os = false }
    end
    local mismatch_env, _, _, mismatch_ok, mismatch_error = loadIsolated(mismatch_module)
    check(mismatch_ok, "a module reporting a mismatched model loads: " .. tostring(mismatch_error))
    drawn = {}
    pcall(mismatch_env.on.paint, gc)
    local screen = table.concat(drawn, "\n")
    check(screen:find("Nspire cx non-cas, OS 6.4.0.74", 1, true) ~= nil and
          screen:find("This model and this OS are not a pair StepCAS knows", 1, true) ~= nil,
          "the launch screen prints the reading and says outright that it does not hold together: " ..
              screen:sub(1, 160))
end

local incomplete_module = copyModule()
incomplete_module.unit_conversion = nil
local manifest_before_incomplete = calls.manifest
local incomplete_env, incomplete_names, incomplete_menu, incomplete_ok, incomplete_error =
    loadIsolated(incomplete_module)
check(incomplete_ok, "an incomplete module is handled at document load: " .. tostring(incomplete_error))
check(#incomplete_names == 1 and incomplete_names[1] == "nps_nspire",
      "the incomplete-module check performs one native load")
check(calls.manifest == manifest_before_incomplete,
      "a missing export is rejected before reading its manifest")
check(incomplete_env.hasGiac == true and incomplete_env.hasSteps == false,
      "a callable caseval stays usable while an incomplete StepCAS surface is rejected")

-- Criterion 11 on the launch screen, for the path that used to break it: Giac is up because
-- integrity passed, and the refusal fired after that. The paint judged the hasGiac channel and drew
-- its conclusion about all of them, so this screen read as a plain success.
namesItsRefusal(incomplete_env, "outdated or incomplete",
                "a missing solver export while Giac is still up")

local incomplete_giac_before = calls.giac
local giac_ok = pcall(incomplete_env.nps_nspire.caseval, "version()")
check(giac_ok and calls.giac == incomplete_giac_before + 1,
      "plain Giac remains callable through the incomplete module")

local conversion_before = calls.unit_conversion
local guided_ok, guided_error = pcall(incomplete_menu[1][2][2])
check(guided_ok, "the guided action refuses the incomplete module without raising: " .. tostring(guided_error))
check(incomplete_env.physicsBrowser.active == false and
      incomplete_env.steps.status == "StepCAS module is outdated or incomplete (missing unit_conversion)",
      "the guided browser stays closed and names the missing export")
check(calls.unit_conversion == conversion_before,
      "the guided action does not invoke the missing solver")

local integrate_before = calls.integrate
local request_ok, request_error = pcall(incomplete_env.runSteps, "integrate", "1/x")
check(request_ok and request_error == "StepCAS module is outdated or incomplete (missing unit_conversion)",
      "step requests return the incomplete-module diagnosis")
check(calls.integrate == integrate_before,
      "step requests do not call any solver after the surface check fails")

local load_env, load_names, load_menu, load_ok, load_error =
    loadIsolated(nil, "duplicate nps_nspire module shadow\nloader detail")
check(load_ok and #load_names == 1, "an nrequire failure leaves the document usable: " .. tostring(load_error))
check(load_env.hasGiac == false and load_env.hasSteps == false,
      "a failed native load enables neither native surface")
local load_guided_ok = pcall(load_menu[1][2][2])
check(load_guided_ok and load_env.physicsBrowser.active == false and
      load_env.steps.status == "StepCAS module load failed: duplicate nps_nspire module shadow",
      "the load failure preserves a concise nrequire reason")

do
    local unavailable = loadIsolated(nil, "module 'nps_nspire' was found but would not load")
    namesItsRefusal(unavailable, "StepCAS module load failed", "a refused resident load", "Restart the calculator")
    unavailable.readFullText()
    drawn = {}
    unavailable.on.paint(gc)
    check(table.concat(drawn, " "):find("check the module and digest", 1, true) ~= nil,
          "a refused load retains package recovery when restarting does not help")
end

-- The failure the maintainer actually hit, and the one the old code described wrongly. Ndl installs from a
-- document rather than persistently here, so every reboot drops it, nrequire goes back to nil and
-- the whole native surface is gone through no fault of the module. The old screen called that a
-- module load failure and told the reader to install the module, which changes nothing.
-- Scoped, because this file is already near Lua's 200 locals per function and six more went over.
do
    local ndl_env, ndl_names, _, ndl_ok, ndl_error = loadIsolated(copyModule(), nil, true)
    check(ndl_ok and #ndl_names == 0,
          "a calculator without Ndl leaves the document usable: " .. tostring(ndl_error))
    check(ndl_env.hasGiac == false and ndl_env.hasSteps == false,
          "no Ndl enables neither native surface")
    local screen = namesItsRefusal(ndl_env, "Ndl is not loaded", "a calculator without Ndl",
                                   "run the installer")
    check(screen:find("Install the matching module", 1, true) == nil,
          "and it does not tell the reader to install a module that is already there")
end

local incompatible_manifest = copyManifest()
incompatible_manifest.symbolic_backend.interface_id = "nps.giac.string-v0"
local incompatible_module = copyModule()
local incompatible_manifest_calls = 0
incompatible_module.capability_manifest = function()
    incompatible_manifest_calls = incompatible_manifest_calls + 1
    return incompatible_manifest
end
local incompatible_env, _, _, incompatible_ok, incompatible_error = loadIsolated(incompatible_module)
check(incompatible_ok and incompatible_manifest_calls == 1,
      "an incompatible manifest is read exactly once: " .. tostring(incompatible_error))
check(incompatible_env.hasGiac == true and incompatible_env.hasSteps == false and
      incompatible_env.runSteps("integrate", "1/x") == "StepCAS module incompatible (backend interface)",
      "a same-shaped incompatible backend contract is rejected while Giac remains usable")

do
    for _, outdated in ipairs({"envelope", "schema table"}) do
        local manifest = copyManifest()
        if outdated == "envelope" then manifest.schema_version = 1
        else manifest.schema_versions[1].version = 1 end
        local module = copyModule()
        module.capability_manifest = function() return manifest end
        local env, _, _, ok = loadIsolated(module)
        local expected = outdated == "envelope" and
            "StepCAS module incompatible (capability schema must be 2)" or
            "StepCAS module incompatible (missing capability-manifest v2 schema)"
        check(ok and not env.hasSteps and env.runSteps("integrate", "1/x") == expected,
              "the renamed capability keys reject an outdated " .. outdated)
    end
end

local missing_module_manifest = copyManifest()
for _, entry in ipairs(missing_module_manifest.installed_modules) do
    if entry.id == "units.chain-link-conversion" then entry.id = "units.chain-link-conversion-v0" end
end
local missing_module = copyModule()
local missing_module_manifest_calls = 0
missing_module.capability_manifest = function()
    missing_module_manifest_calls = missing_module_manifest_calls + 1
    return missing_module_manifest
end
local missing_module_env, _, _, missing_module_ok, missing_module_error = loadIsolated(missing_module)
check(missing_module_ok and missing_module_manifest_calls == 1,
      "an incomplete capability set is read exactly once: " .. tostring(missing_module_error))
check(missing_module_env.hasSteps == false and
      missing_module_env.runSteps("integrate", "1/x") ==
          "StepCAS module incompatible (missing units.chain-link-conversion)",
      "the manifest must advertise every V4 solver capability")

do
    local manifest = copyManifest()
    for _, entry in ipairs(manifest.installed_modules) do
        if entry.id == "number.integer-method.literal" then entry.id = "number.integer-method.old" end
    end
    local module = copyModule()
    module.capability_manifest = function() return manifest end
    local env, _, _, ok = loadIsolated(module)
    check(ok and not env.hasSteps and env.runSteps("integrate", "1/x") ==
          "StepCAS module incompatible (missing number.integer-method.literal)",
          "a stale module cannot silently omit integer walkthroughs")
end

local malformed_manifest = copyManifest()
malformed_manifest.schema_versions[2].version = "1"
local malformed_module = copyModule()
local malformed_manifest_calls = 0
malformed_module.capability_manifest = function()
    malformed_manifest_calls = malformed_manifest_calls + 1
    return malformed_manifest
end
local malformed_env, _, _, malformed_ok, malformed_error = loadIsolated(malformed_module)
check(malformed_ok and malformed_manifest_calls == 1,
      "a malformed manifest is read exactly once: " .. tostring(malformed_error))
check(malformed_env.hasSteps == false and
      malformed_env.runSteps("integrate", "1/x") == "StepCAS manifest malformed (schema entry 2)",
      "malformed manifest metadata is rejected precisely")

-- PLAT-010's four reds, on one table because the chunk is near LuaJIT's 200-local ceiling. The
-- first two are the artifact clause and the version the manifest has to state, both decided at
-- load. The last two are the version clause itself, which is read from the running backend at first
-- paint, so unlike every refusal above they pass the load-time check and refuse when a solve asks.
plat010 = {}

plat010.artifact_manifest = copyManifest()
plat010.artifact_manifest.artifact = "split"
plat010.artifact_module = copyModule()
plat010.artifact_module.capability_manifest = function() return plat010.artifact_manifest end
plat010.artifact_env = loadIsolated(plat010.artifact_module)
check(plat010.artifact_env.hasSteps == false and
      plat010.artifact_env.runSteps("integrate", "1/x") ==
          "StepCAS module incompatible (artifact must be unified)",
      "the bundled unified artifact is the only one the interface will solve on")

plat010.stated_manifest = copyManifest()
plat010.stated_manifest.symbolic_backend.version = nil
plat010.stated_module = copyModule()
plat010.stated_module.capability_manifest = function() return plat010.stated_manifest end
plat010.stated_env = loadIsolated(plat010.stated_module)
check(plat010.stated_env.hasSteps == false and
      plat010.stated_env.runSteps("integrate", "1/x") ==
          "StepCAS manifest malformed (backend version)",
      "a manifest that states no backend version is rejected at load, before anything can compare it")

plat010.version_manifest = copyManifest()
plat010.version_manifest.symbolic_backend.version = "1.9.1"
plat010.version_module = copyModule()
plat010.version_module.capability_manifest = function() return plat010.version_manifest end
plat010.version_env = loadIsolated(plat010.version_module)
plat010.integrate_before = calls.integrate
check(plat010.version_env.hasSteps == true and
      plat010.version_env.runSteps("integrate", "1/x") ==
          "StepCAS needs Giac 1.9.1, found 1.9.0" and
      calls.integrate == plat010.integrate_before,
      "a backend disagreeing with the version the build documents refuses before it solves")
namesItsRefusal(plat010.version_env, "StepCAS needs Giac 1.9.1, found 1.9.0",
                "a backend version disagreement")

plat010.unreadable_module = copyModule()
plat010.unreadable_module.caseval = function(s)
    calls.giac = calls.giac + 1
    if s == "version()" then return "giac for TI Nspire CX, (c) B. Parisse and R. De Graeve" end
    return "giac(" .. s .. ")"
end
plat010.unreadable_env = loadIsolated(plat010.unreadable_module)
check(plat010.unreadable_env.runSteps("integrate", "1/x") ==
      "StepCAS needs Giac 1.9.0, found no version",
      "a backend whose version cannot be read is not a backend whose version agreed")

assert((function()
    for _, scenario in ipairs({
        {reply = "Bad Argument Value"},
        {reply = "Unreadable runtime", incompatible = true},
        {reply = "version query interrupted", raises = true},
        {reply = string.rep("unreadable response ", 30) .. "response tail", details = "response tail"},
        {reply = 19, details = "Unexpected number response"},
        {reply = "giac for TI Nspire CX 1.9.1, incompatible runtime"},
    }) do
        local module = copyModule()
        if scenario.incompatible then
            local manifest = copyManifest()
            manifest.symbolic_backend.interface_id = "nps.giac.string-v0"
            module.capability_manifest = function() return manifest end
        end
        local queries = 0
        module.caseval = function(command)
            check(command == "version()", "startup diagnosis issues only the version query")
            queries = queries + 1
            if scenario.raises then error(scenario.reply, 0) end
            return scenario.reply
        end
        local env = loadIsolated(module)
        drawn = {}
        local painted = pcall(env.on.paint, gc)
        local screen = table.concat(drawn, "\n")
        check(painted and screen:find("OK.", 1, true) == nil and screen:find("NO.", 1, true) ~= nil,
              "an unreadable or mismatched runtime version never paints a successful startup")
        if not tostring(scenario.reply):find("1.9.1", 1, true) then
            check(env.giacLabel() == "Giac :", "unreadable response text is not displayed as a version")
        end
        env.readFullText()
        drawn = {}
        for scroll = 1, 64 do
            env.on.paint(gc)
            env.on.arrowDown()
        end
        local paragraphs = table.concat(drawn, " ")
        check(paragraphs:find("Version response: ", 1, true) ~= nil and
              paragraphs:find(scenario.details or scenario.reply, 1, true) ~= nil,
              "startup refusal preserves its full backend response for diagnosis")
        env.on.paint(gc)
        check(queries == 1, "reading startup failure details does not repeat backend work")
    end
    return true
end)())

-- Issue 211. A version query that threw is not an answer about the version, so the memo it leaves
-- has to stay open to a retry, while a version that did answer stays the verdict it already was.
do
    local queries = 0
    local module = copyModule()
    module.caseval = function(command)
        calls.giac = calls.giac + 1
        if command ~= "version()" then return "giac(" .. command .. ")" end
        queries = queries + 1
        if queries == 1 then error("version query interrupted", 0) end
        return "giac for TI Nspire CX 1.9.0, (c) B. Parisse and R. De Graeve, Institut Fourier"
    end
    local env = loadIsolated(module)
    drawn = {}
    check(pcall(env.on.paint, gc) and queries == 1 and env.giacLabel() == "Giac :",
          "a version query that raises at first paint leaves no version behind")
    for _ = 1, 8 do env.on.paint(gc) end
    check(queries == 1, "and a repaint reuses that attempt rather than asking the backend again")
    local solved_before = calls.integrate
    local result = env.runSteps("integrate", "1/x")
    check(queries == 2 and calls.integrate == solved_before + 1 and
          result ~= "StepCAS needs Giac 1.9.0, found no version",
          "the next solve retries the query and runs once the backend answers")
    check(env.giacLabel() == "Giac 1.9.0 :" and env.stepRefusal() == nil,
          "and the answered version replaces the failed attempt for every later surface")
    env.on.escapeKey()
    drawn = {}
    env.on.paint(gc)
    local screen = table.concat(drawn, "\n")
    check(screen:find("OK.", 1, true) ~= nil and screen:find("found no version", 1, true) == nil,
          "so the launch screen stops naming a refusal the backend no longer earns")
end

-- The typed surface and the guided browser each decide on their own, so each one retries its own way in.
do
    local queries = 0
    local module = copyModule()
    module.caseval = function(command)
        calls.giac = calls.giac + 1
        if command ~= "version()" then return "giac(" .. command .. ")" end
        queries = queries + 1
        if queries == 1 then error("version query interrupted", 0) end
        return "giac for TI Nspire CX 1.9.0, (c) B. Parisse and R. De Graeve, Institut Fourier"
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.fctEditor.editor:setExpression("\\0el {1+1}")
    env.on.enterKey()
    check(queries == 2 and env.steps.status == nil,
          "a typed line retries the failed query instead of restating a refusal it inherited")

    local browser = loadIsolated(module)
    queries = 0
    browser.on.paint(gc)
    browser.openPhysicsFixtures()
    check(queries == 2 and browser.physicsBrowser.active == true,
          "and the guided browser opens once its retry answers")
    browser.on.escapeKey()
end

-- The control. A backend that answered is refused on what it said, and asking it twice would only
-- get the same sentence back.
do
    local queries = 0
    local module = copyModule()
    module.caseval = function(command)
        calls.giac = calls.giac + 1
        if command ~= "version()" then return "giac(" .. command .. ")" end
        queries = queries + 1
        return "giac for TI Nspire CX, (c) B. Parisse and R. De Graeve"
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    local solved_before = calls.integrate
    check(env.runSteps("integrate", "1/x") == "StepCAS needs Giac 1.9.0, found no version" and
          env.runSteps("integrate", "1/x") == "StepCAS needs Giac 1.9.0, found no version" and
          calls.integrate == solved_before and queries == 1,
          "a backend that answered without a version is refused for good rather than asked again")

    local mismatch = copyModule()
    local mismatch_manifest = copyManifest()
    mismatch_manifest.symbolic_backend.version = "1.9.1"
    mismatch.capability_manifest = function() return mismatch_manifest end
    local mismatch_queries = 0
    mismatch.caseval = function(command)
        calls.giac = calls.giac + 1
        if command ~= "version()" then return "giac(" .. command .. ")" end
        mismatch_queries = mismatch_queries + 1
        return "giac for TI Nspire CX 1.9.0, (c) B. Parisse and R. De Graeve, Institut Fourier"
    end
    local mismatch_env = loadIsolated(mismatch)
    mismatch_env.on.paint(gc)
    solved_before = calls.integrate
    check(mismatch_env.runSteps("integrate", "1/x") == "StepCAS needs Giac 1.9.1, found 1.9.0" and
          mismatch_env.runSteps("integrate", "1/x") == "StepCAS needs Giac 1.9.1, found 1.9.0" and
          calls.integrate == solved_before and mismatch_queries == 1,
          "and a version that disagrees with the manifest is a verdict, not an attempt to repeat")
end

-- Nothing answered at all, so the diagnosis says the query failed rather than naming the type of a
-- response there never was.
do
    local queries = 0
    local module = copyModule()
    module.caseval = function(command)
        calls.giac = calls.giac + 1
        if command ~= "version()" then return "giac(" .. command .. ")" end
        queries = queries + 1
        error({ code = 7 }, 0)
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.readFullText()
    drawn = {}
    for _ = 1, 64 do
        env.on.paint(gc)
        env.on.arrowDown()
    end
    local paragraphs = table.concat(drawn, " ")
    check(paragraphs:find("Version response: ", 1, true) ~= nil and
          paragraphs:find("Query failed", 1, true) ~= nil and
          paragraphs:find("Unexpected table response", 1, true) == nil,
          "a raised error object reads as a query that failed rather than a response that arrived")
end

plat010.guided_before = calls.unit_conversion
plat010.version_env.openPhysicsFixtures()
check(plat010.version_env.physicsBrowser.active == false and
      calls.unit_conversion == plat010.guided_before and
      plat010.version_env.steps.status == "StepCAS needs Giac 1.9.1, found 1.9.0",
      "and the guided browser refuses the same disagreement rather than opening")

-- Through !g, because runSteps answers that one before its refusal gate and later lines leave the automatic path.
do
    plat010.version_env.fctEditor.editor:setExpression("\\0el {!g}")
    plat010.version_env.on.enterKey()
    check(plat010.version_env.steps.automatic == false,
          "the plain Giac switch is a mode change rather than a solve, so it still answers")
    local typed_giac_before = calls.giac
    plat010.version_env.fctEditor.editor:setExpression("\\0el {1+1}")
    local typed_ok = pcall(plat010.version_env.on.enterKey)
    local typed_pair = plat010.version_env.steps.histText[#plat010.version_env.steps.histText]
    check(typed_ok and calls.giac == typed_giac_before,
          "a backend version disagreement refuses the typed line rather than answering from it")
    check(typed_pair and typed_pair[2]:find("StepCAS needs Giac 1.9.1, found 1.9.0", 1, true) ~= nil and
          plat010.version_env.steps.status == "StepCAS needs Giac 1.9.1, found 1.9.0",
          "and names the disagreement in the history and the status line: " .. tostring(typed_pair and typed_pair[2]))
end

local erroring_module = copyModule()
local erroring_manifest_calls = 0
erroring_module.capability_manifest = function()
    erroring_manifest_calls = erroring_manifest_calls + 1
    error("manifest unavailable", 0)
end
local erroring_env, _, _, erroring_ok, erroring_error = loadIsolated(erroring_module)
check(erroring_ok and erroring_manifest_calls == 1,
      "an erroring manifest is called exactly once: " .. tostring(erroring_error))
check(erroring_env.hasSteps == false and
      erroring_env.runSteps("integrate", "1/x") == "StepCAS manifest failed: manifest unavailable",
      "manifest call failures retain their concise reason")

local function writeEvidence()
    local path = os.getenv("NPS_EVIDENCE")
    if not path then return end

    local existing = io.open(path, "rb")
    if not existing then
        check(false, "UI evidence requires the completed unit evidence file")
        return
    end
    local before = existing:read("*a")
    existing:close()
    check(before:find("group\tadapter\n", 1, true) ~= nil,
          "UI evidence appends only after the unit evidence fixture")

    local rows = { "group\tui v4" }
    for _, record in ipairs(evidence_records) do
        rows[#rows + 1] = table.concat({
            "evidence", record.requirement, record.passed and "pass" or "fail", "ui v4", record.what,
        }, "\t")
    end
    local appended = table.concat(rows, "\n") .. "\n"
    local output, openError = io.open(path, "ab")
    if not output then
        check(false, "UI evidence could not append: " .. tostring(openError))
        return
    end
    local wrote, writeError = output:write(appended)
    local closed, closeError = output:close()
    check(wrote ~= nil and closed ~= nil,
          "UI evidence append completes: " .. tostring(writeError or closeError or "ok"))
    if not wrote or not closed then return end

    local combined = io.open(path, "rb")
    if not combined then
        check(false, "UI evidence path cannot be reopened")
        return
    end
    local after = combined:read("*a")
    combined:close()
    local emitted = after:sub(#before + 1)
    local expected = {
        "MATH-011", "MATH-012", "MATH-015", "STEP-008", "STEP-009", "STEP-010", "STEP-016",
        "STEP-020", "UI-003", "UI-004", "UI-005", "UI-012", "UI-013", "UI-014", "UI-015",
        "PLAT-006", "PLAT-012",
    }
    local complete = after:sub(1, #before) == before
    for _, requirement in ipairs(expected) do
        complete = complete and emitted:find("evidence\t" .. requirement .. "\tpass\tui v4\t", 1, true) ~= nil
    end
    -- Counted as well as listed. Naming each row catches one that stopped being emitted, and only
    -- the count catches one that was added without being named, which is how six of these came to
    -- be reaching the report with nothing holding them there.
    local named, distinct, at = {}, 0, 1
    while at <= #emitted do
        local stop = emitted:find("\n", at, true) or (#emitted + 1)
        local line = emitted:sub(at, stop - 1)
        if line:sub(1, 9) == "evidence\t" then
            local tab = line:find("\t", 10, true)
            local requirement = tab and line:sub(10, tab - 1) or ""
            if requirement ~= "" and not named[requirement] then
                named[requirement] = true
                distinct = distinct + 1
            end
        end
        at = stop + 1
    end
    complete = complete and distinct == #expected
    -- Deliberate, not an oversight. UI-001 is about a real touchpad and MATH-003 about the OS
    -- giving an expression its math face, and both run here against stubbed calculator globals, so
    -- a pass here would say the host stub agrees with itself. Asserting the absence rather than
    -- just not emitting means adding the row later is a failing test rather than a quiet upgrade.
    -- PLAT-010 joins them: the reds above measure the gate, but the contract itself is a bundled
    -- Giac agreeing with the manifest the cross build wrote, and only the device can show that.
    -- UI-002 joins MATH-003 for the reason nps_v4.lua gives above the math box, that they are one
    -- layer seen twice. The fitting half is measured here and the two-dimensional half cannot be:
    -- the stub answers a fraction's height from one character, so every fitting check would read
    -- the same against a stub that laid the whole expression out flat.
    complete = complete and emitted:find("evidence\tUI-001\t", 1, true) == nil and
               emitted:find("evidence\tUI-002\t", 1, true) == nil and
               emitted:find("evidence\tMATH-003\t", 1, true) == nil and
               emitted:find("evidence\tPLAT-010\t", 1, true) == nil
    check(complete, "UI evidence preserves prior TSV and emits only the supported requirement links")
end

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

clearAction = menuAction(main_menu, "Actions", "Clear History")
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
main_contextMenu()
check(#menus == menu_count + 1, "ctrl+menu on a history entry opens a menu")
check(menus[#menus] and menus[#menus].items[1] == "Copy to Entry Line" and
      menus[#menus].items[2] == "Delete Entry",
      "with the reversible action on the item the focus opens on")
check(#steps.histText == 2 and fctEditor:getExpression() == "",
      "and a dismissal, which the framework returns as nil, does nothing at all")

menu_answer = 1
theView:setFocus(histME1[1])
main_contextMenu()
check(fctEditor:getExpression() ~= "", "copy puts the entry on the input line")
check(#steps.histText == 2, "and leaves the history where it was")

fctEditor.editor:setText("")
fctEditor:fixContent()
menu_answer = 2
theView:setFocus(histME1[1])
main_contextMenu()
check(#steps.histText == 1, "delete removes the entry the menu was opened on")

-- The input line already has the OS's own cut, copy and paste, so a menu there would duplicate them.
theView:setFocus(fctEditor)
menu_count = #menus
main_contextMenu()
check(#menus == menu_count, "ctrl+menu on the input line opens nothing")

nps_split.os_menu = nil
theView:setFocus(histME1[1])
menu_count = #menus
main_contextMenu()
check(#menus == menu_count and #steps.histText == 1,
      "and a module too old to draw a menu neither opens one nor acts as though it had")

-- A split, as the bridge hands one over. Three cases rather than the two a real quadratic produces,
-- because the resolution a case reports has three spellings and a wording nobody has seen drawn is
-- a wording nobody has checked. Scratch values hang off a table for the same reason as mathcase.
branchcase = {}
do
    branchcase.result = {
        outcome = "solved", detail = "", solved = true, answer_only = false,
        status = "solved and verified", result = "2 and (-2)",
        original_expression = "x^2 = 4", normalized_expression = "((x^2) = 4)",
        nodes = 14, step_count = 5, rewrites = 4, giac_calls = 0,
        steps = {
            { kind = "plan", phase = "plan", name = "Isolate the square and split on its roots",
              goal = "Solve for x", short = "Get x^2 alone, then take both square roots",
              claim = "no claim", verified = true, failed = false, depth = 0 },
            { kind = "branch", phase = "solve", name = "Square root case",
              goal = "Take the positive square root",
              short = "x = 2 is one of the values whose square is 4",
              claim = "solution set narrowed", verified = true, failed = false, depth = 1,
              case = "(x = 2)", resolution = "solved",
              settled_by = "substitution into the original equation" },
            { kind = "branch", phase = "solve", name = "Square root case",
              goal = "Rule out a real root", short = "no real number squares to a negative",
              claim = "solution set narrowed", verified = true, failed = false, depth = 1,
              case = "((x^2) = (-4))", resolution = "rejected",
              settled_by = "no real number squares to a negative" },
            { kind = "branch", phase = "solve", name = "Square root case",
              goal = "Leave this case open", short = "nothing settled this one",
              claim = "solution set narrowed", verified = false, failed = false, depth = 1,
              case = "(x = 9)", resolution = "unresolved" },
            { kind = "check", phase = "check", name = "Completeness of the split",
              goal = "Check that no case is missing",
              short = "The cases multiply back out to the equation that was split",
              claim = "solution set preserved", verified = true, failed = false, depth = 1 },
        },
    }
    stepsSetProgression("full")
    theView:setFocus(fctEditor)
    next_step_result = branchcase.result
    type_line("x^2 = 4")
    on.enterKey()
    check(steps.result == branchcase.result and steps.view == "list",
          "a split opens in the viewer like any other derivation")

    -- The list first, because that is the screen a student meets before opening anything. Read as
    -- it scrolls rather than from one frame, since a split is longer than the rows on screen.
    branchcase.list = painted()
    branchcase.captureMath = function()
        for _, editor in ipairs(editors) do
            if editor.readOnly and editor.visible and editor.x and editor.x >= 0 and
               editor.y >= steps.headerHeight and mathBoxFits(editor) then
                branchcase.list = branchcase.list .. "\n" .. editor.expr
            end
        end
    end
    branchcase.captureMath()
    branchcase.scrolled = 0
    while branchcase.scrolled < 8 do
        on.arrowDown()
        branchcase.list = branchcase.list .. "\n" .. painted()
        branchcase.captureMath()
        branchcase.scrolled = branchcase.scrolled + 1
    end
    check(branchcase.list:find("CASE", 1, true) ~= nil,
          "a case is labelled by its own phase rather than as ordinary work")
    check(branchcase.list:find("(x = 2)", 1, true) ~= nil,
          "and the list shows which case it is, which a step with no before and no after would not")
    check(branchcase.list:find("(ruled out)", 1, true) ~= nil and
          branchcase.list:find("(not settled)", 1, true) ~= nil,
          "a case that was ruled out or left open says so in the list")
    check(branchcase.list:find("Completeness of the split", 1, true) ~= nil,
          "the split's completeness check is on the same screen, so exhaustiveness is not repeated "
          .. "on every case")
    -- STEP-016 again, over the rows a case produces, which are the ones with no before and no after
    -- and so the ones a renderer would have to invent something for.
    step016.case_orphans = #step016.orphans
    step016Sweep()
    check(#step016.orphans == step016.case_orphans,
          "a case row is the condition the step carries, not a sentence written about it: " ..
              (step016.orphans[step016.case_orphans + 1] or "none"))

    -- Then each case opened on its own. The condition is an expression, so it goes through a box.
    steps.focus = 2
    steps.view = "step"
    steps.stepScroll = 0
    -- A sentence long enough to wrap is drawn as two rows, so the rows are joined before they are
    -- read. Otherwise the check pins where the OS happened to break the line.
    branchcase.solved = (painted():gsub("\n", " "))
    check(branchcase.solved:find("Phase: Case", 1, true) ~= nil and
          branchcase.solved:find("Condition:", 1, true) ~= nil and
          mathBoxExact("(x = 2)") ~= nil,
          "an open case names itself and shows its condition as maths")
    check(branchcase.solved:find(
              "This case gives an answer, checked by substitution into the original equation",
              1, true) ~= nil,
          "and says what settled it in words a beginner reads")
    check(branchcase.solved:find("From:", 1, true) == nil and
          branchcase.solved:find("To:", 1, true) == nil,
          "a case has no before and no after, so neither label is drawn")

    steps.focus = 3
    steps.stepScroll = 0
    branchcase.rejected = (painted():gsub("\n", " "))
    check(branchcase.rejected:find(
              "This case is ruled out: no real number squares to a negative", 1, true) ~= nil,
          "a rejected case is drawn as ruled out")
    check(branchcase.rejected:find("This case gives an answer", 1, true) == nil,
          "and cannot be read as one that gave an answer")

    steps.focus = 4
    steps.stepScroll = 0
    branchcase.open = (painted():gsub("\n", " "))
    check(branchcase.open:find("This case is not settled yet", 1, true) ~= nil and
          branchcase.open:find("This case gives an answer", 1, true) == nil and
          branchcase.open:find("ruled out", 1, true) == nil,
          "an unsettled case is drawn as neither answered nor ruled out")
    on.escapeKey()
    on.escapeKey()

    -- An empty real solution set, in the shape the bridge really returns for x^2 = -4: an outcome
    -- and no result string, because there is no value to print. The engine answered and checked it,
    -- so the screen must not put NO RESULT over a derivation it also calls verified.
    branchcase.empty = {
        outcome = "no real solution",
        detail = "no real value of x has square (-4)",
        solved = false, has_result = true, answer_only = false,
        status = "solved and verified",
        original_expression = "x^2 = -4", normalized_expression = "((x^2) = (-4))",
        nodes = 9, step_count = 1, rewrites = 1, giac_calls = 0,
        steps = {
            { kind = "branch", phase = "solve", name = "Square root case",
              goal = "Rule out a real root", short = "no real number squares to a negative",
              claim = "solution set narrowed", verified = true, failed = false, depth = 0,
              case = "((x^2) = (-4))", resolution = "rejected",
              settled_by = "no real number squares to a negative" },
        },
    }
    theView:setFocus(fctEditor)
    next_step_result = branchcase.empty
    type_line("x^2 = -4")
    on.enterKey()
    branchcase.answer = (painted():gsub("\n", " "))
    check(branchcase.answer:find("NO RESULT", 1, true) == nil,
          "an empty real solution set is not reported as no result")
    check(mathBoxShowing("no real solution") ~= nil,
          "and the empty set is named as the answer it is, in the box the answer is drawn in")
    on.escapeKey()
end

-- The two kinds of condition PHYS-027 keeps apart, drawn apart. The catch-up equal-position step is
-- the one the golden fixtures record an assumption before on.
do
    branchcase.physics = {
        outcome = "solved", detail = "", solved = true, answer_only = false,
        status = "solved and verified", result = "t = 10 s",
        original_expression = "2t = 4*(t - 5)", normalized_expression = "((2 * t) = (4 * (t - 5)))",
        nodes = 9, step_count = 1, rewrites = 1, giac_calls = 0,
        steps = {
            { kind = "transformation", phase = "solve", name = "Equal-position event",
              goal = "Create equal-position event equation", short = "Set the positions equal",
              claim = "equivalent", verified = true, failed = false, depth = 0,
              before = "0 + 2*t", after = "0 + 4*(t - 5)",
              domain = "t >= 5",
              assumes = "both bodies move at constant velocity in frame track" },
        },
    }
    theView:setFocus(fctEditor)
    next_step_result = branchcase.physics
    type_line("2t = 4*(t - 5)")
    on.enterKey()
    check(steps.result == branchcase.physics, "the two-condition step opens in the viewer")
    steps.focus = 1
    steps.view = "step"
    steps.stepScroll = 0
    if STEP_DETAILS[steps.detail] ~= "beginner" then stepsToggleDetail() end
    branchcase.both = (painted():gsub("\n", " "))
    branchcase.scrolled = 0
    while branchcase.scrolled < 6 do
        on.arrowDown()
        branchcase.both = branchcase.both .. " " .. (painted():gsub("\n", " "))
        branchcase.scrolled = branchcase.scrolled + 1
    end
    check(branchcase.both:find("Requires: t ≥ 5", 1, true) ~= nil,
          "a mathematical restriction is drawn as something the step requires")
    check(branchcase.both:find(
              "Assumes: both bodies move at constant velocity in frame track", 1, true) ~= nil,
          "and a physical modelling assumption is drawn beside it as something assumed")
    on.escapeKey()
    if STEP_DETAILS[steps.detail] ~= "standard" then stepsToggleDetail() end
end

evidence("STEP-016",
         #step016.orphans == 0 and step016.rows_seen > 0 and step016.rows_before ~= "" and
         step016.rows_before == step016.rows_after and step016.carried > 0 and
         step016.shown == step016.carried and step016.borrowed == 0,
         "the viewer a student reads projects the recorded trace rather than retelling it: each of "
         .. "the " .. step016.rows_seen .. " rows it listed across a linear derivation and a split "
         .. "is a string the step it names carries, building the list again returns the same rows, "
         .. "and the fuller explanation was drawn for exactly the steps that filled that field "
         .. "rather than composed for the ones that left it empty")

do
    (function()
    local module = copyModule()
    local attempted, evaluated = {}, {}
    local requests = {
        { "Algebra", "Solve  solve(expr,var)", "solve(2*x+5=13,x)", "solve", "2*x+5=13" },
        { "Algebra", "Factor  factor(expr)", "factor(x^2-1)", "factor", "x^2-1" },
        { "Polynomials", "Factor  factor(P)", "factor(x^2-1)", "factor", "x^2-1" },
        { "Algebra", "Simplify  simplify(expr)", "simplify(x+x)", "simplify", "x+x" },
        { "Calculus", "Derivative  diff(expr,var)", "diff(y^2,y)", "differentiate", "y^2" },
        { "Calculus", "Integral  int(expr,var)", "int(1/x,x)", "integrate", "1/x" },
        { "Templates", "Derivative, how fast something changes", "(x^2,x)", "differentiate", "x^2" },
        { "Templates", "Integral, the area under a curve", "∫(1/x,x)", "integrate", "1/x" },
        { "Templates", "Integral between two limits", "∫(x,x,0,1)", "definite integral", "x" },
    }
    module.walkthrough = function(command, variable)
        attempted[#attempted + 1] = { command, variable }
        for _, request in ipairs(requests) do
            if command == request[3] then
                local record = {}
                for key, value in pairs(fake_result) do record[key] = value end
                record.request_expression = command
                record.mode = request[4]
                record.original_expression = request[5]
                record.normalized_expression = request[5]
                return record
            end
        end
        if command == "int(x,x,0)" then
            return { request_expression = command, mode = "integrate", outcome = "unsupported command",
                     status = "unsupported", detail = "A definite integral needs both bounds",
                     solved = false, answer_only = false, steps = {}, step_count = 0 }
        end
        if command == "diff(bad,x)" then return "malformed" end
        if command == "diff(context,x)" then
            return { request_expression = command, mode = "differentiate", outcome = "differentiated",
                     status = "solved and verified", solved = true, result = "2*x", steps = {} }
        end
        if command == "diff(mismatch,x)" then
            return { request_expression = "diff(other,x)", mode = "differentiate", outcome = "unsupported",
                     status = "unsupported", solved = false, steps = {} }
        end
        if command == "diff(throw,x)" then error("command fixture failure") end
        return nil
    end
    module.caseval = function(command)
        if command == "version()" then return nps_split.caseval(command) end
        evaluated[#evaluated + 1] = command
        return "giac(" .. command .. ")"
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    local function enter(command)
        env.fctEditor.editor:setExpression("\\0el {" .. command .. "}")
        env.on.enterKey()
    end
    for _, request in ipairs(requests) do
        env.fctEditor.editor:setExpression("\\0el {}", 6, 6)
        local found = false
        for _, category in ipairs(env.menu) do
            if category[1] == request[1] then
                for index = 2, #category do
                    if type(category[index]) == "table" and category[index][1] == request[2] then
                        category[index][2]()
                        found = true
                    end
                end
            end
        end
        check(found and env.fctEditor:getExpression() ~= "", "the menu inserts " .. request[2])
        local before = #attempted
        local inserted = env.fctEditor:getExpression()
        env.fctEditor:addString(request[1] == "Templates" and request[5]
                                or request[3]:sub(#inserted + 1))
        check(env.fctEditor:getExpression() == request[3], "the menu entry can be completed in place")
        env.on.enterKey()
        check(#attempted == before + 1 and attempted[#attempted][1] == request[3],
              "one native command receives the complete menu expression " .. request[3])
        check(#evaluated == 0, "a recognized menu command never falls through to ordinary CAS")
        check(env.steps.active and env.steps.result.request_expression == request[3]
              and env.steps.result.mode == request[4], "the menu command opens its own walkthrough")
        if env.steps.active then
            env.on.enterKey()
            check(env.steps.view == "step", "the menu walkthrough opens step detail")
            env.on.escapeKey()
            env.on.escapeKey()
        end
    end
    enter("int(x,x,0)")
    check(env.steps.active and env.steps.result.solved == false and #evaluated == 0,
          "an unsupported signature keeps its refusal instead of running a different operation")
    if env.steps.active then env.on.escapeKey() end
    enter("normal(x/(x+1))")
    check(evaluated[#evaluated] == "normal(x/(x+1))" and not env.steps.active,
          "an unhandled operation reaches CAS once with every argument retained")
    local finished = #profile_finishes
    env.on.paint(gc)
    check(#profile_finishes == finished + 1 and profile_finishes[#profile_finishes].operation == "walkthrough",
          "ordinary CAS fallback finishes its measurement when the shell renders")
    local before = #evaluated
    enter("diff(bad,x)")
    check(not env.steps.active and #evaluated == before and type(env.steps.status) == "string"
          and env.steps.status:find("invalid", 1, true),
          "a malformed native command record is refused without CAS fallback")
    for _, command in ipairs({ "diff(context,x)", "diff(mismatch,x)" }) do
        enter(command)
        check(not env.steps.active and #evaluated == before and env.steps.status:find("invalid", 1, true),
              "an unrelated or incomplete command context cannot become a walkthrough")
    end
    enter("diff(throw,x)")
    check(not env.steps.active and #evaluated == before,
          "a command exception does not replay evaluation through CAS")
    enter("!g")
    local attempted_before = #attempted
    enter("diff(y^2,y)")
    check(#attempted == attempted_before and evaluated[#evaluated] == "diff(y^2,y)",
          "plain CAS mode bypasses command recognition")
    env.stepsSetProgression("hint")
    enter("diff(y^2,y)")
    check(env.steps.active and env.steps.walkthrough == "hint",
          "selecting hint walkthrough restores automatic command walkthroughs")
    if env.steps.active then
        check(env.steps.revealed < #env.steps.result.steps, "a menu hint starts with later steps withheld")
        env.on.tabKey()
        env.on.escapeKey()
    end
    local saved = env.on.save()
    check(saved.automatic == true, "automatic walkthrough preference is saved")
    env.on.restore({ automatic = false })
    check(env.steps.automatic == false, "plain CAS preference restores")
    env.on.restore({ automatic = "invalid" })
    check(env.steps.automatic == false, "a malformed saved preference cannot change execution mode")
    local old_module = copyModule()
    old_module.walkthrough = nil
    local old = loadIsolated(old_module)
    check(not old.hasSteps and type(old.stepRefusal()) == "string"
          and old.stepRefusal():find("walkthrough", 1, true),
          "an older module cannot silently omit menu walkthrough support")
    end)()
end

do
    (function()
    local scenarios = { "malformed", "exception", "refusal", "context", "cas", "guided" }
    for _, scenario in ipairs(scenarios) do
        local module = copyModule()
        local begun, finishes = 0, {}
        module.resource_profile_begin = function() begun = begun + 1 return true end
        module.resource_profile_finish = function(metrics)
            finishes[#finishes + 1] = metrics
            return { report_written = true }
        end
        module.walkthrough = function()
            if scenario == "exception" then error("command failure") end
            if scenario == "refusal" then return nil, "request rejected" end
            if scenario == "cas" then return nil end
            return "malformed"
        end
        module.differentiate = function() return { outcome = "differentiated", solved = true } end
        module.caseval = function(command)
            if command == "version()" then return nps_split.caseval(command) end
            error("CAS failure")
        end
        local env = loadIsolated(module)
        env.on.paint(gc)
        if scenario == "guided" then
            env.PHYSICS_FIXTURES[1].run = function() error("guided failure") end
            env.openPhysicsFixtures()
            env.on.enterKey()
        else
            env.fctEditor.editor:setExpression("\\0el {" ..
                (scenario == "context" and "!d x" or "diff(x,x)") .. "}")
            env.on.enterKey()
        end
        check(begun == 1 and #finishes == 0 and env.steps.pendingResourceProfile ~= nil,
              "a failed " .. scenario .. " request retains its interval until error display")
        env.on.paint(gc)
        local metrics = finishes[1] or {}
        check(#finishes == 1 and metrics.request_failed == true and
              metrics.solver_metrics_available == false and metrics.arena_nodes == nil and
              metrics.derivation_steps == nil and metrics.backend_calls == nil and
              type(metrics.render_ready_ms) == "number" and metrics.lua_live_bytes > 0,
              "a failed " .. scenario .. " request finishes with unavailable solver counters")
        env.on.paint(gc)
        check(#finishes == 1 and env.steps.pendingResourceProfile == nil,
              "a failed " .. scenario .. " interval cannot finish twice")
        if scenario == "guided" then env.on.escapeKey() end
        module.walkthrough = function(command)
            local record = {}
            for key, value in pairs(fake_result) do record[key] = value end
            record.request_expression = command
            record.mode = "integrate"
            return record
        end
        env.fctEditor.editor:setExpression("\\0el {int(1/x,x)}")
        env.on.enterKey()
        env.on.paint(gc)
        local recovered = finishes[2] or {}
        check(begun == 2 and #finishes == 2 and recovered.request_failed == false and
              recovered.solver_metrics_available == true and recovered.arena_nodes == 12 and
              recovered.derivation_steps == 4 and recovered.backend_calls == 2,
              "a valid request after " .. scenario .. " starts a fresh measured interval")
    end
    end)()
end

do
    (function()
    for _, route in ipairs({ "automatic", "plain" }) do
        for _, progression in ipairs({ "full", "hint" }) do
            for _, outcome in ipairs({ "answer", "error", "nil", "throw" }) do
                local module = copyModule()
                local record = {}
                for key, value in pairs(fake_result) do record[key] = value end
                record.agrees, record.giac_tag = nil, "unsupported operation"
                local solves, evaluations = 0, 0
                module.integrate = function()
                    solves = solves + 1
                    return record
                end
                module.walkthrough = function() return nil end
                module.caseval = function(command)
                    if command == "version()" then return nps_split.caseval(command) end
                    evaluations = evaluations + 1
                    if outcome == "throw" then error("current CAS failure") end
                    if outcome == "nil" then return nil end
                    return outcome == "error" and "Error: current domain" or "[-2,2]"
                end
                local env = loadIsolated(module)
                env.on.paint(gc)
                local function enter(command)
                    env.fctEditor.editor:setExpression("\\0el {" .. command .. "}")
                    env.on.enterKey()
                end
                env.stepsSetProgression(progression)
                if route == "plain" then enter("!g") end
                enter("!i 1/x")
                if progression == "hint" then env.on.tabKey() end
                local retained = env.steps.result
                local revealed, rows, collapsed = env.steps.revealed, env.steps.rows, env.steps.collapsed
                local prior_status = env.steps.status
                check(env.steps.active and solves == 1 and prior_status ~= nil,
                      "a prior walkthrough establishes its status for " .. route .. "/" .. progression .. "/" .. outcome)
                env.closeSteps()
                enter("eval(solve(3*x^2-12=0,x))")
                check(evaluations == 1 and solves == 1 and not env.steps.active,
                      "ordinary CAS executes once without another solve for " .. route .. "/" .. outcome)
                check(env.steps.result == retained and env.steps.revealed == revealed and
                      env.steps.rows == rows and env.steps.collapsed == collapsed,
                      "ordinary CAS retains walkthrough and hint state for " .. route .. "/" .. progression)
                drawn = {}
                env.on.paint(gc)
                local screen = table.concat(drawn, "\n")
                if outcome == "throw" then
                    check(env.steps.status and env.steps.status:find("current CAS failure", 1, true) and
                          env.steps.status ~= prior_status,
                          "a current CAS exception replaces the prior status for " .. route)
                else
                    local expected = outcome == "nil" and "Error" or
                                     (outcome == "error" and "Error: current domain" or "[-2,2]")
                    local pair = env.steps.histText[#env.steps.histText]
                    check(env.steps.status == nil and not screen:find(prior_status, 1, true),
                          "ordinary CAS does not paint the prior walkthrough status for " .. route .. "/" .. progression .. "/" .. outcome)
                    check(pair and pair[2] == " " .. expected,
                          "ordinary CAS preserves its current answer or failure for " .. route .. "/" .. outcome)
                end
                enter("!!")
                check(env.steps.active and env.steps.status == prior_status and
                      env.steps.result == retained and env.steps.revealed == revealed and
                      env.steps.rows == rows and env.steps.collapsed == collapsed and
                      solves == 1 and evaluations == 1,
                      "reopening restores the retained walkthrough status without recomputation for " .. route .. "/" .. progression .. "/" .. outcome)
            end
        end
    end
    end)()
end

if os.getenv("NPS_COMMAND_MODULE") then
    (function()
    local module = copyModule()
    local fixture_manifest = nps_split.capability_manifest
    local fixture_walkthrough = nps_split.walkthrough
    local open_native = assert(package.loadlib(os.getenv("NPS_COMMAND_MODULE"), "luaopen_nps_split"))
    local fixture, loaded = nps_split, package.loaded.nps_split
    -- luaL_register otherwise overwrites the existing fixture table.
    nps_split, package.loaded.nps_split = nil, nil
    local native = open_native()
    nps_split, package.loaded.nps_split = fixture, loaded
    local backend = luagiac
    luagiac = nil
    local dispatched, evaluated = 0, 0
    module.walkthrough = function(...)
        dispatched = dispatched + 1
        return native.walkthrough(...)
    end
    module.caseval = function(command)
        if command ~= "version()" then evaluated = evaluated + 1 end
        return nps_split.caseval(command)
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    for _, command in ipairs({ "solve(2*x+5=13,x)", "solve(x^2=9,x)", "diff(x^2,x)",
                               "int(x^2,x)", "simplify(x+x)", "factor(x^2-1)",
                               "expand((x+1)^2)", "rearrange(V=I*R,I)" }) do
        local before = dispatched
        env.fctEditor.editor:setExpression("\\0el {" .. command .. "}")
        env.on.enterKey()
        check(dispatched == before + 1 and evaluated == 0,
              "the real command bridge executes once from the editor: " .. command)
        check(env.steps.active and env.steps.result.request_expression == command
              and env.steps.result.solved and #env.steps.result.steps > 0,
              "a real native derivation opens in the UI: " .. command)
        if env.steps.active then
            env.on.paint(gc)
            env.on.enterKey()
            env.on.escapeKey()
            env.on.escapeKey()
        end
    end
    for _, command in ipairs({ "int(x,x,0)", "diff(x,x,2)", "factor(x,y)", "solve(x,2)" }) do
        env.fctEditor.editor:setExpression("\\0el {" .. command .. "}")
        env.on.enterKey()
        check(env.steps.active and not env.steps.result.solved and evaluated == 0,
              "a real bridge preflight refusal remains visible without fallback: " .. command)
        if env.steps.active then env.on.escapeKey() end
    end
    local function select_integer_menu(label)
        for _, category in ipairs(env.menu) do
            for index = 2, #category do
                local item = category[index]
                if type(item) == "table" and item[1]:find(label, 1, true) == 1 then
                    item[2]()
                    return true
                end
            end
        end
        return false
    end
    for _, case in ipairs({
        {"Greatest Common Divisor", "gcd(", "-48,18)", "6", "int.gcd-conclusion"},
        {"Euclidean Quotient", "iquo(", "17,5)", "3", "int.division"},
        {"Euclidean Remainder", "irem(", "17,5)", "2", "int.division"},
        {"Factorial", "factorial(", "5)", "120", "int.factorial-product"},
        {"Permutations", "perm(", "5,2)", "20", "int.permutation-product"},
        {"Combinations", "comb(", "5,2)", "10", "int.combination-product"},
        {"Prime Test", "is_prime", "(17)", "2", "int.prime-conclusion"},
        {"Next Prime", "nextprime(", "17)", "19", "int.next-prime"},
        {"Power Modulo", "powmod(", "2,10,17)", "4", "int.modular-power"},
        {"Factor Integer", "ifactor(", "60)", "((2^2) * 3 * 5)", "int.factor-product"},
    }) do
        env.fctEditor.editor:setExpression("\\0el {}")
        check(select_integer_menu(case[1]) and env.fctEditor:getExpression() == case[2],
              "the real " .. case[1] .. " menu inserts its supported command")
        env.fctEditor:addString(case[3])
        local command = case[2] .. case[3]
        check(env.fctEditor:getExpression() == command,
              "completing the menu entry preserves the integer arguments: " .. command)
        local before_dispatch, before_evaluation = dispatched, evaluated
        env.on.enterKey()
        local result = env.steps.result
        check(dispatched == before_dispatch + 1 and evaluated == before_evaluation,
              "the integer menu executes one native request without a CAS fallback: " .. command)
        check(env.steps.active and result and result.mode == "integer" and result.solved and
              result.request_expression == command and result.result == case[4],
              "the integer menu opens its exact recorded answer: " .. command)
        local found_rule = false
        for _, step in ipairs(result and result.steps or {}) do
            found_rule = found_rule or step.rule == case[5]
        end
        check(env.steps.active and found_rule,
              "the integer menu exposes its operation's actual recorded method: " .. command)
        if env.steps.active then
            env.on.paint(gc)
            env.on.escapeKey()
        end
    end

    env.fctEditor.editor:setExpression("\\0el {}")
    check(select_integer_menu("Determinant") and env.fctEditor:getExpression() == "det(",
          "the existing determinant menu inserts the native walkthrough command")
    env.fctEditor:addString("[[1,2],[3,4]])")
    do
        local before_dispatch, before_evaluation = dispatched, evaluated
        env.on.enterKey()
        local record = env.steps.result
        check(dispatched == before_dispatch + 1 and evaluated == before_evaluation,
              "the determinant menu dispatches once without a CAS fallback")
        check(env.steps.active and record and record.mode == "determinant" and
              record.request_expression == "det([[1,2],[3,4]])" and not record.solved and
              not record.has_result and record.status == "dependency unavailable",
              "a split module names missing determinant row tracing without inventing a scalar answer")
        if env.steps.active then env.on.escapeKey() end
        env.fctEditor.editor:setExpression("\\0el {det([[1,2,3],[4,5,6]])}")
        env.on.enterKey()
        record = env.steps.result
        check(env.steps.active and record and record.mode == "determinant" and not record.solved and
              record.result == nil and #record.steps == 0 and evaluated == before_evaluation,
              "a nonsquare determinant stays a native refusal in the walkthrough viewer")
        if env.steps.active then env.on.escapeKey() end
    end

    env.fctEditor.editor:setExpression("\\0el {}")
    check(select_integer_menu("Factorial"), "the large exact result starts from the factorial menu")
    env.fctEditor:addString("100)")
    local before_dispatch, before_evaluation = dispatched, evaluated
    env.on.enterKey()
    local factorial_100 = "93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000"
    check(env.steps.active and env.steps.result.solved and env.steps.result.result == factorial_100,
          "the factorial menu retains all 158 digits of the native 100 factorial result")
    local saved_width = platform.window.width
    for _, width in ipairs({320, 180}) do
        platform.window.width = function() return width end
        env.resizeGC(gc)
        drawn, draw_calls = {}, {}
        env.on.paint(gc)
        check(env.steps.active and table.concat(drawn):find("T text", 1, true) ~= nil,
              "the oversized exact answer advertises its reader shortcut at " .. width)
        local lines, fits = {}, true
        if env.steps.active then
            env.on.charIn("t")
            for page = 1, 100 do
                drawn, draw_calls = {}, {}
                env.on.paint(gc)
                for _, call in ipairs(draw_calls) do
                    fits = fits and call.x >= 0 and call.x + gc:getStringWidth(call.text) <= width
                        and call.y >= 0 and call.y + 12 <= 240
                    if call.y == env.STEP_LINE then lines[#lines + 1] = call.text end
                end
                env.on.arrowDown()
            end
            env.on.escapeKey()
        end
        local visible = table.concat(lines):gsub("%s", "")
        check(visible:find(factorial_100, 1, true) ~= nil and fits,
              "all 158 actual answer digits scroll into view in order without clipping at " .. width)
        check(env.steps.active and env.steps.result.request_expression == "factorial(100)" and
              dispatched == before_dispatch + 1 and evaluated == before_evaluation,
              "reading the entire exact answer preserves the walkthrough without recomputation at " .. width)
    end
    platform.window.width = saved_width
    env.resizeGC(gc)
    if env.steps.active then env.on.escapeKey() end
    module.density = function(...)
        dispatched = dispatched + 1
        return native.density(...)
    end
    local first_guidance_editor = #editors + 1
    env = loadIsolated(module)
    env.on.paint(gc)
    for _, case in ipairs({
        {"solve(2*x+5=13,x)", "eq.divide-both-sides", "(x = 4)"},
        {"diff(x*sin(x),x)", "d.product", "((1 * sin(x)) + (x * cos(x)))"},
        {"int(x^2,x)", "i.power", "((x^3) * (3^-1))"},
        {"density", "physics.density.substitute", "(m = (2000 * (3 * (1000000^-1))))"},
    }) do
        if case[1] == "density" then
            env.openPhysicsFixtures()
            env.on.arrowDown()
            env.on.enterKey()
        else
            env.fctEditor.editor:setExpression("\\0el {" .. case[1] .. "}")
            env.on.enterKey()
        end
        local record, selected = env.steps.result
        for index, step in ipairs(record.steps or {}) do
            if step.rule == case[2] then selected = index break end
        end
        check(env.steps.active and selected and record.steps[selected].after == case[3],
              "classroom guidance uses the real recorded mathematics for " .. case[1])
        if selected then
            local step = record.steps[selected]
            check(step.verified and not step.failed and type(step.action) == "string" and step.action ~= "",
                  "the native transformation supplies a verified concrete action for " .. case[1])
            env.steps.focus, env.steps.view = selected, "step"
            local before_requests, before_backend = dispatched, evaluated
            for _, width in ipairs({320, 180}) do
                platform.window.width = function() return width end
                env.resizeGC(gc)
                for _, reader in ipairs({false, true}) do
                    env.steps.stepScroll = 0
                    if reader then env.readFullText() end
                    local seen, fits = {}, true
                    for i = 1, 100 do
                        drawn, draw_calls = {}, {}
                        env.on.paint(gc)
                        for _, call in ipairs(draw_calls) do
                            fits = fits and call.x >= 0 and call.x + gc:getStringWidth(call.text) <= width
                            if call.y >= 15 and call.y < 225 then seen[#seen + 1] = call.text end
                        end
                        env.on.arrowDown()
                    end
                    local text = table.concat(seen):gsub("%s", "")
                    check(text:find(("Do:" .. step.action):gsub("%s", ""), 1, true)
                          and text:find("Write:", 1, true) and fits,
                          "native classroom instruction is complete and readable " .. case[1] .. "/" .. width .. "/" .. tostring(reader))
                    if reader then
                        check(text:find(("Write:" .. case[3]):gsub("%s", ""), 1, true),
                              "the native full reader retains the exact line to write for " .. case[1])
                        env.on.escapeKey()
                    end
                end
            end
            check(dispatched == before_requests and evaluated == before_backend and env.steps.focus == selected,
                  "reading native guidance preserves selection without solver or CAS calls for " .. case[1])
            if case[2] == "d.product" then
                env.steps.walkthrough = "hint"
                for revealed = 2, 3 do
                    env.steps.revealed = revealed
                    for _, reader in ipairs({false, true}) do
                        env.steps.stepScroll = 0
                        if reader then env.readFullText() end
                        local leaked = false
                        for i = 1, 50 do
                            drawn = {}
                            env.on.paint(gc)
                            leaked = leaked or table.concat(drawn):find("cos(x)", 1, true) ~= nil
                            for index = first_guidance_editor, #editors do
                                local editor = editors[index]
                                if editor.visible and editor.x and editor.x >= 0 and editor.y >= 0
                                   and editor.y < 240 and editor.expr:find("cos(x)", 1, true) then leaked = true end
                            end
                            env.on.arrowDown()
                        end
                        check(not leaked, "the real product-rule parent withholds completed child mathematics " .. revealed .. "/" .. tostring(reader))
                        if reader then env.on.escapeKey() end
                    end
                end
                env.steps.walkthrough = "full"
                env.steps.revealed = #record.steps
            end
        end
        platform.window.width = saved_width
        env.resizeGC(gc)
        env.on.escapeKey()
        env.on.escapeKey()
    end
    luagiac = backend
    check(nps_split.capability_manifest == fixture_manifest and nps_split.walkthrough == fixture_walkthrough,
          "loading the real bridge preserves the shared module fixture for subsequent UI cases")
    end)()
end

do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local matrix
    for _, category in ipairs(env.menu) do
        if category[1] == "Matrix & Vector" then matrix = category end
    end
    local typed = {}
    for index = 2, #(matrix or {}) do
        local item = matrix[index]
        if type(item) == "table" then
            env.fctEditor.editor:setExpression("\\0el {}", 6, 6)
            item[2]()
            typed[#typed + 1] = env.fctEditor:getExpression()
        end
    end
    local inserted = " " .. table.concat(typed, " ") .. " "
    check(matrix and inserted:find(" image( ", 1, true) and inserted:find(" eigenvalues( ", 1, true),
          "the matrix palette still types the linear algebra commands it is supposed to")
    check(not inserted:find(" bug( ", 1, true),
          "no matrix palette entry types bug(, which the backend does not implement")
    end)()
end

do
    (function()
    local create, refused = D2Editor.newRichText, nil
    D2Editor.newRichText = function()
        local editor = create()
        local set_font = editor.setFontSize
        editor.setFontSize = function(self, size)
            if size and not ({[7]=true, [9]=true, [10]=true, [11]=true, [12]=true, [16]=true, [24]=true})[size] then
                refused = size
                error("unsupported handheld editor font " .. tostring(size))
            end
            return set_font(self, size)
        end
        return editor
    end
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    env.addME("x", "x+1")
    env.fsize = 12
    -- The stub raises on a rejected size, so the walk is protected and reports a failed check.
    local climbed, fell = {}, {}
    local climbing = pcall(function()
        for _ = 1, 4 do env.fontUp() climbed[#climbed + 1] = env.fsize end
    end)
    check(climbing and table.concat(climbed, ",") == "16,24,24,24",
          "Increase Font Size reaches the largest accepted size and stops there: " ..
          table.concat(climbed, ",") .. " refused=" .. tostring(refused))
    env.fsize = 24
    local falling = pcall(function()
        for _ = 1, 7 do env.fontDown() fell[#fell + 1] = env.fsize end
    end)
    check(falling and table.concat(fell, ",") == "16,12,11,10,9,7,7",
          "Decrease Font Size reaches the smallest accepted size and stops there: " ..
          table.concat(fell, ",") .. " refused=" .. tostring(refused))
    env.fsize = 14
    local stepped = pcall(env.fontDown)
    check(stepped and env.fsize == 12, "a font size off the accepted ladder steps onto it rather than past it")
    check(not refused, "every size the font menu hands the editor is one the handheld accepts")
    D2Editor.newRichText = create
    end)()
end

do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local create = D2Editor.newRichText
    local allocations, first = 0, nil
    D2Editor.newRichText = function()
        allocations = allocations + 1
        if allocations == 2 then error("history editor allocation failed", 0) end
        first = create()
        return first
    end
    local added = pcall(env.addME, "allocation input", "allocation result")
    D2Editor.newRichText = create
    check(not added, "a failed history editor allocation is reported")
    check(#env.histME1 == 0 and #env.histME2 == 0 and #env.steps.histText == 0,
          "a failed second history editor allocation leaves all three history arrays unchanged")
    check(first and first.visible == false and first.x == -10000,
          "the uncommitted first history editor is released after its partner allocation fails")

    for index = 1, 55 do env.addME("input " .. index, "result " .. index) end
    check(#env.histME1 == 50 and #env.histME2 == 50 and #env.steps.histText == 50,
          "history retains a bounded number of paired editor rows")
    check(env.steps.histText[1][1] == "input 6" and env.steps.histText[50][2] == "result 55",
          "history eviction retains the newest entries in their existing order")

    local saved = {}
    for index = 1, 55 do saved[index] = { "saved " .. index, "answer " .. index } end
    local restored = loadIsolated(copyModule())
    restored.on.restore({ history = saved })
    allocations = 0
    D2Editor.newRichText = function() allocations = allocations + 1 return create() end
    restored.on.paint(gc)
    D2Editor.newRichText = create
    check(allocations == 101 and #restored.histME1 == 50 and #restored.histME2 == 50,
          "restore requests editors only for the bounded history window")
    check(restored.steps.histText[1][1] == "saved 6" and
          restored.steps.histText[50][2] == "answer 55",
          "restore keeps the same newest-first eviction result as live history")
    end)()
end

do
    (function()
    local module = copyModule()
    local error_text = string.rep("long error context ", 40) .. "ERROR_TAIL"
    module.walkthrough = function() return nil, error_text end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.fctEditor.editor:setExpression("\\0el {diff(x,x)}")
    env.on.enterKey()
    local read_text
    for _, category in ipairs(env.menu) do
        for index = 2, #category do
            local item = category[index]
            if type(item) == "table" and item[1] == "Read Full Text" then read_text = item[2] end
        end
    end
    check(type(read_text) == "function", "long dynamic text has a discoverable full-text reading action")
    local saved_width = platform.window.width
    for _, width in ipairs({ 320, 180 }) do
        platform.window.width = function() return width end
        env.resizeGC(gc)
        drawn, draw_calls = {}, {}
        env.on.paint(gc)
        local bounded = true
        for _, call in ipairs(draw_calls) do
            if call.text:find("steps:", 1, true) then
                bounded = bounded and call.x >= 0 and call.x + gc:getStringWidth(call.text) <= width
            end
        end
        check(bounded, "status text stays within the " .. width .. " pixel display")
        if read_text then
            read_text()
            local tail, fits = false, true
            for page = 1, 90 do
                drawn, draw_calls = {}, {}
                env.on.paint(gc)
                for _, call in ipairs(draw_calls) do
                    tail = tail or call.text:find("ERROR_TAIL", 1, true) ~= nil
                    fits = fits and call.x >= 0 and call.x + gc:getStringWidth(call.text) <= width
                        and call.y >= 0 and call.y + 12 <= 240
                end
                env.on.arrowDown()
            end
            check(tail and fits, "the complete error scrolls into view without clipping at " .. width ..
                  " (tail=" .. tostring(tail) .. ", fits=" .. tostring(fits) ..
                  ", status=" .. tostring(env.steps.status) .. ")")
            env.on.escapeKey()
        end
    end
    platform.window.width = saved_width
    env.resizeGC(gc)
    local function scan_reader(pages)
        local visible, fits = {}, true
        for page = 1, pages do
            drawn, draw_calls = {}, {}
            env.on.paint(gc)
            for _, call in ipairs(draw_calls) do
                fits = fits and call.x >= 0 and call.x + gc:getStringWidth(call.text) <= platform.window:width()
                    and call.y >= 0 and call.y + 12 <= platform.window:height()
                if call.y >= 15 and call.y < platform.window:height() - 15 then
                    visible[#visible + 1] = call.text
                end
            end
            env.on.arrowDown()
        end
        return table.concat(visible):gsub("%s", ""), fits
    end
    local input_text = string.rep("x + ", 100) .. "INPUT_TAIL"
    local answer_text = string.rep("y+", 200) .. "ANSWER_TAIL"
    env.fctEditor.editor:setExpression("\\0el {" .. input_text .. "}")
    env.fctEditor.editor.curpos = 20
    check(env.fctEditor.editor.filter.arrowLeft() == false and
          env.fctEditor.editor.filter.arrowRight() == false,
          "an interior caret leaves horizontal math-editor movement with the native widget")
    env.on.help()
    local seen, fits = scan_reader(100)
    check(seen:find("INPUT_TAIL", 1, true) and fits,
          "unsubmitted math has a complete wrapped alternative to native horizontal scrolling")
    env.on.escapeKey()
    check(env.fctEditor:getExpression() == input_text and env.theView:getFocus() == env.fctEditor,
          "closing the text reader preserves the entry and returns its focus")

    env.addME(input_text, answer_text)
    local focused = env.histME2[#env.histME2]
    env.theView:setFocus(focused)
    env.on.help()
    seen, fits = scan_reader(100)
    check(seen:find("INPUT_TAIL", 1, true) and seen:find("ANSWER_TAIL", 1, true) and fits,
          "a selected history entry exposes both complete expressions without clipped math boxes")
    env.on.escapeKey()
    check(env.theView:getFocus() == focused and focused.editor.visible and focused.editor.x >= 0,
          "closing the history reader restores the selected native editor")

    local manifest = {}
    for key, value in pairs(fake_manifest) do manifest[key] = value end
    manifest.id = "nps." .. string.rep("manifest", 40) .. "MANIFEST_TAIL"
    module.capability_manifest = function() return manifest end
    module.integrate = function(expression)
        local record, first = {}, {}
        for key, value in pairs(fake_result) do record[key] = value end
        for key, value in pairs(fake_result.steps[1]) do first[key] = value end
        first.name = string.rep("method ", 40) .. "METHOD_TAIL"
        first.kind, first.phase = "transformation", "solve"
        first.detail = string.rep("explain this transformation ", 40) .. "EXPLANATION_TAIL"
        first.after = answer_text
        record.steps, record.step_count = {first}, 1
        record.original_expression, record.normalized_expression = expression, expression
        record.display_result = answer_text
        return record
    end
    env = loadIsolated(module)
    env.on.paint(gc)
    env.steps.variable = string.rep("variable", 30) .. "VARIABLE_TAIL"
    env.fctEditor.editor:setExpression("\\0el {!i " .. input_text .. "}")
    env.on.enterKey()
    for _, width in ipairs({320, 180}) do
        platform.window.width = function() return width end
        env.resizeGC(gc)
        drawn, draw_calls = {}, {}
        env.on.paint(gc)
        check(table.concat(drawn):find("T text", 1, true) ~= nil,
              "abbreviated walkthrough text names its reader shortcut at " .. width)
        env.on.charIn("t")
        seen, fits = scan_reader(160)
        check(seen:find("INPUT_TAIL", 1, true) and seen:find("ANSWER_TAIL", 1, true)
              and seen:find("VARIABLE_TAIL", 1, true) and seen:find("MANIFEST_TAIL", 1, true)
              and seen:find("METHOD_TAIL", 1, true) and seen:find("EXPLANATION_TAIL", 1, true) and fits,
              "full request, result, variable, module and step text remain reachable at " .. width)
        env.on.escapeKey()
        check(env.steps.active and env.steps.view == "list",
              "the reader returns to the same walkthrough without changing its view")
        env.on.enterKey()
        env.steps.detail = 2
        seen, fits = scan_reader(160)
        check(seen:find("EXPLANATION_TAIL", 1, true) and seen:find("ANSWER_TAIL", 1, true) and fits,
              "existing step detail scrolls the complete explanation and oversized formula at " .. width)
        env.on.escapeKey()
    end
    platform.window.width = saved_width
    env.resizeGC(gc)
    env.steps.walkthrough = "hint"
    env.steps.revealed = 1
    local secret = {}
    for key, value in pairs(env.steps.result.steps[1]) do secret[key] = value end
    secret.name, secret.after = "HIDDEN_STEP", "HIDDEN_ANSWER"
    env.steps.result.steps[2] = secret
    env.steps.result.step_count = 2
    env.steps.result.display_result = "HIDDEN_ANSWER"
    env.on.charIn("t")
    seen = scan_reader(160)
    check(not seen:find("HIDDEN_STEP", 1, true) and not seen:find("HIDDEN_ANSWER", 1, true)
          and seen:find("INPUT_TAIL", 1, true),
          "full-text reading preserves withheld answers and later hints")
    env.on.escapeKey()
    env.on.escapeKey()
    env.openPhysicsFixtures()
    env.PHYSICS_FIXTURES[1].label = string.rep("fixture ", 40) .. "FIXTURE_TAIL"
    env.PHYSICS_FIXTURES[1].problem = string.rep("long structured problem ", 30) .. "PROBLEM_TAIL"
    drawn, draw_calls = {}, {}
    env.on.paint(gc)
    local footer_fits = true
    for _, call in ipairs(draw_calls) do
        if call.y >= 190 and call.y < 226 then footer_fits = footer_fits and call.y + 12 <= 226 end
    end
    check(footer_fits, "guided preview lines stop before the footer controls")
    env.on.charIn("t")
    seen, fits = scan_reader(160)
    check(seen:find("FIXTURE_TAIL", 1, true) and seen:find("PROBLEM_TAIL", 1, true) and fits,
          "abbreviated guided labels and problem previews retain complete readable text")
    env.on.escapeKey()
    env.PHYSICS_FIXTURES[1].run = function()
        error("short error\n" .. string.rep("additional failure context ", 40) .. "RAW_ERROR_TAIL", 0)
    end
    env.on.enterKey()
    env.on.paint(gc)
    env.on.charIn("t")
    seen, fits = scan_reader(160)
    check(seen:find("RAW_ERROR_TAIL", 1, true) and fits,
          "a shortened multiline solver error retains its complete original in the reader")
    env.on.escapeKey()
    check(env.physicsBrowser.active, "closing an error reader restores the guided browser")
    end)()
end

do
    (function()
    local module = copyModule()
    local requests = 0
    module.integrate = function(expression)
        requests = requests + 1
        local record = {}
        for key, value in pairs(fake_result) do record[key] = value end
        record.steps, record.step_count = {}, 32
        record.original_expression, record.normalized_expression = expression, expression
        for i = 1, record.step_count do
            local step = {}
            for key, value in pairs(fake_result.steps[1]) do step[key] = value end
            step.depth, step.phase, step.kind = i - 1, "check", "check"
            step.name = "nested " .. i
            step.goal = string.rep("verify expression ", 30) .. "NESTED_TAIL"
            step.before, step.after = nil, nil
            record.steps[i] = step
        end
        return record
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.fctEditor.editor:setExpression("\\0el {!i x}")
    env.on.enterKey()
    for i = 2, 32 do env.on.arrowDown() end
    check(env.steps.focus == 32 and requests == 1,
          "key navigation reaches the deepest recorded step with one request")
    for _, width in ipairs({320, 180}) do
        platform.window.width = function() return width end
        env.resizeGC(gc)
        drawn, draw_calls = {}, {}
        env.on.paint(gc)
        local phase, title, fits = nil, nil, true
        for _, call in ipairs(draw_calls) do
            if call.x + gc:getStringWidth(call.text) > width then fits = false end
            if call.text == "CHECK" then phase = call end
            if call.text:find("nested 32", 1, true) then title = call end
        end
        check(fits, "deep step indentation keeps every label inside " .. width .. " pixels")
        check(phase and title and phase.x + gc:getStringWidth(phase.text) < title.x,
              "a deep step retains a readable name beside its phase at " .. width)
        env.on.charIn("t")
        local seen = {}
        for i = 1, 100 do
            drawn, draw_calls = {}, {}
            env.on.paint(gc)
            for _, call in ipairs(draw_calls) do
                if call.y >= 15 and call.y < 225 then seen[#seen + 1] = call.text end
            end
            env.on.arrowDown()
        end
        check(table.concat(seen):find("NESTED_TAIL", 1, true) ~= nil,
              "the deep step's complete goal remains reachable in the existing reader at " .. width)
        env.on.escapeKey()
        check(env.steps.focus == 32 and env.steps.view == "list" and requests == 1,
              "closing the deep step reader preserves selection without solving again")
    end
    platform.window.width = function() return 320 end
    end)()
end

do
    (function()
    local module, calls = copyModule(), 0
    module.caseval = function() calls = calls + 1 return "unused" end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.dispinfos = false
    calls = 0
    local inputResize, firstWidth = env.fctEditor.resize
    env.fctEditor.resize = function(self, w, h)
        firstWidth = firstWidth or w
        return inputResize(self, w, h)
    end
    env.resizeME(env.fctEditor.editor, 300, 20)
    check(firstWidth >= 300, "input sizing does not apply the history column limit")
    env.fctEditor.resize = inputResize
    for i = 1, 40 do env.addME("equation_" .. i, "answer_" .. i) end
    for i = 1, 40 do
        env.theView:setFocus(env.histME1[i])
        env.reposView()
        local row = env.histME1[i]
        check(row.y >= 0 and row.y + row.h <= env.fctEditor.y,
              "history navigation contains ordinary equation " .. i)
    end
    local original = string.rep("1234567890 + ", 50) .. "HISTORY_TAIL"
    env.addME("x", original)
    local row = env.histME2[#env.histME2]
    for _, width in ipairs({320, 180}) do
        platform.window.width = function() return width end
        env.resizeGC(gc)
        env.theView:setFocus(row)
        env.reposView()
        drawn, draw_calls = {}, {}
        env.on.paint(gc)
        local cue = false
        for _, call in ipairs(draw_calls) do
            if call.text:find("HELP", 1, true) and call.x >= 0 and call.y >= 0
               and call.x + gc:getStringWidth(call.text) <= width
               and call.y + 12 <= env.fctEditor.y then cue = true end
        end
        check(row.y >= 0 and row.y + row.h <= env.fctEditor.y,
              "oversized history frame fits the history viewport at " .. width)
        check(row.editor.plain and row.editor.wrapWidth == 0,
              "oversized history uses native rich text wrapping at " .. width)
        check(cue, "oversized history exposes its full text shortcut at " .. width)
        check(row.editor.y + row.editor.boxh <= row.y + row.h - 12,
              "native history content leaves its full text shortcut unobscured at " .. width)
        check(row:getExpression() == original, "history retains its original expression at " .. width)
        env.on.help()
        local seen = {}
        for i = 1, 180 do
            drawn, draw_calls = {}, {}
            env.on.paint(gc)
            for _, call in ipairs(draw_calls) do
                if call.y >= 15 and call.y < 225 then seen[#seen + 1] = call.text end
            end
            env.on.arrowDown()
        end
        check(table.concat(seen):find("HISTORY_TAIL", 1, true) ~= nil,
              "history Help reader reaches the exact result tail at " .. width)
        env.on.escapeKey()
        check(env.theView:getFocus() == row and calls == 0,
              "history reader restores selection without evaluating at " .. width)
    end
    platform.window.width = function() return 320 end
    env.resizeGC(gc)
    env.addME("x", "x/2")
    local short = env.histME2[#env.histME2]
    check(not short.editor.plain and mathBoxFits(short.editor),
          "fitting history equations preserve measured two dimensional rendering")
    short.editor.mathHeight = 700
    short.editor:announceSize()
    env.theView:setFocus(short)
    env.reposView()
    check(short.editor.plain and short.y >= 0 and short.y + short.h <= env.fctEditor.y,
          "native tall equation measurement cannot place focused history above the viewport")
    short.editor.mathHeight = nil
    env.fsize = 13
    env.applyFontSizeChange()
    check(not short.editor.plain and mathBoxFits(short.editor),
          "font changes restore fitting history to measured math")
    env.addME("x", "x+x+x+x+x+x+x")
    local responsive = env.histME2[#env.histME2]
    check(not responsive.editor.plain and mathBoxFits(responsive.editor),
          "a moderate expression fits the wide history column")
    platform.window.width = function() return 180 end
    env.resizeGC(gc)
    check(responsive.editor.plain,
          "narrowing the viewport remeasures history and switches oversized math to text")
    responsive.editor.deferSize = true
    platform.window.width = function() return 320 end
    env.resizeGC(gc)
    check(not responsive.editor.visible,
          "unmeasured history is hidden until a delayed native size callback arrives")
    responsive.editor.deferSize = false
    responsive.editor:announceSize()
    check(not responsive.editor.plain and responsive.editor.visible and mathBoxFits(responsive.editor),
          "a delayed native callback restores fitting math at its measured viewport width")
    env.addME("x", string.rep("7", 411))
    local unbroken = env.histME2[#env.histME2]
    env.theView:setFocus(unbroken)
    env.reposView()
    check(unbroken.editor.plain and unbroken.editor.natural > unbroken.editor.boxw
          and unbroken.fullTextCueHeight > 0 and unbroken.h < 60,
          "an unbreakable token retains an explicit reader cue without consuming the viewport")
    env.theView:setFocus(row)
    env.fctEditor.editor:setExpression("\\0el {}")
    env.on.enterKey()
    check(env.fctEditor:getExpression() == original and calls == 0,
          "Enter reuses complete original history without evaluating or copying its cue")
    local saved = env.on.save()
    check(saved.history[#saved.history - 3][2] == original,
          "saving oversized history retains the complete original result")
    env.fsize = 18
    env.applyFontSizeChange()
    env.theView:setFocus(row)
    env.reposView()
    check(row.y >= 0 and row.y + row.h <= env.fctEditor.y,
          "font changes remeasure and bound oversized history")
    end)()
end

do
    (function()
    local module, calls = copyModule(), 0
    -- Answers version() the way the device build does, or the shell refuses it as a version mismatch.
    module.caseval = function(expression)
        calls = calls + 1
        if expression == "version()" then return "giac for TI Nspire CX 1.9.0, (c) B. Parisse" end
        return "2"
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.dispinfos = false
    calls = 0
    env.addME("history_input", "history_result")
    local input, history = env.fctEditor, env.histME2[1]
    local expression = string.rep("x/", 150) .. "INPUT_TAIL"
    input.editor:setExpression("\\0el {" .. expression .. "}")
    input.editor.mathHeight = 700
    input.editor:announceSize()
    for _, dimensions in ipairs({{320, 240}, {180, 160}}) do
        local width, height = dimensions[1], dimensions[2]
        platform.window.width = function() return width end
        platform.window.height = function() return height end
        env.resizeGC(gc)
        env.fsize = 16
        env.applyFontSizeChange()
        env.theView:setFocus(input)
        drawn, draw_calls = {}, {}
        env.on.paint(gc)
        check(input.y >= height / 2 - 1 and input.y + input.h <= height,
              "oversized input remains inside its viewport share at " .. width)
        check(not input.editor.readOnly and not input.editor.plain and input:getExpression() == expression,
              "bounded input preserves native math editing and complete expression at " .. width)
        local cue = false
        for _, call in ipairs(draw_calls) do
            if call.text:find("HELP", 1, true) and call.y >= input.y
               and call.x >= 0 and call.x + gc:getStringWidth(call.text) <= width
               and call.y + 12 <= height and call.y >= input.editor.y + input.editor.boxh then cue = true end
        end
        check(cue, "oversized input has an unobscured full text shortcut at " .. width)
        env.theView:setFocus(history)
        env.reposView()
        check(history.y >= 0 and history.y + history.h < input.y,
              "oversized input leaves focused history fully reachable at " .. width)
        check(not input.editor.invalidResize and not history.editor.invalidResize
              and input.editor.boxw > 0 and input.editor.boxh > 0
              and history.editor.boxw > 0 and history.editor.boxh > 0,
              "editor resizing uses only positive native dimensions at " .. width)
        env.theView:setFocus(input)
        env.on.help()
        local seen = {}
        for i = 1, 180 do
            drawn, draw_calls = {}, {}
            env.on.paint(gc)
            for _, call in ipairs(draw_calls) do
                if call.y >= 15 and call.y < height - 15 then seen[#seen + 1] = call.text end
            end
            env.on.arrowDown()
        end
        check(table.concat(seen):find("INPUT_TAIL", 1, true) ~= nil,
              "bounded input Help exposes its exact expression tail at " .. width)
        env.on.escapeKey()
        check(env.theView:getFocus() == input and input.y >= 0 and input.h <= height / 2,
              "leaving Help restores bounded editable input at " .. width)
        check(env.on.save().expression == expression and calls == 0,
              "bounded input saves its complete expression without evaluating at " .. width)
    end
    platform.window.width = function() return 320 end
    platform.window.height = function() return 240 end
    input.editor.mathHeight = nil
    input.editor:setExpression("\\0el {1+1}")
    env.steps.automatic = false
    env.resizeGC(gc)
    env.on.enterKey()
    check(calls == 1 and env.steps.histText[#env.steps.histText][2]:find("2", 1, true),
          "ordinary Enter still evaluates after recovering from oversized input")
    end)()
end

do
    (function()
    for _, resultFocus in ipairs({false, true}) do
        for _, tallInput in ipairs({false, true}) do
            local module, calls = copyModule(), 0
            module.caseval = function() calls = calls + 1 return "unused" end
            local env = loadIsolated(module)
            env.on.paint(gc)
            env.dispinfos = false
            calls = 0
            env.addME("OLD_INPUT_TAIL", "OLD_RESULT_TAIL")
            env.addME("middle", string.rep("x + ", 50))
            env.addME("new", "3")
            if tallInput then
                env.fctEditor.editor.mathHeight = 700
                env.fctEditor.editor:announceSize()
            end
            local target = env.histME2[2]
            target.editor.deferSize = true
            platform.window.width = function() return 180 end
            env.resizeGC(gc)
            local selected = resultFocus and env.histME2[1] or env.histME1[1]
            env.theView:setFocus(selected)
            env.reposView()
            check(selected.y >= 0 and selected.y + selected.h <= env.fctEditor.y,
                  "history starts visible before a delayed newer row measurement")
            target.editor.deferSize = false
            target.editor:announceSize()
            env.on.paint(gc)
            check(selected.y >= 0 and selected.y + selected.h <= env.fctEditor.y,
                  "delayed reflow keeps focused history visible without manual repositioning "
                  .. tostring(resultFocus) .. "/" .. tostring(tallInput))
            check(env.theView:getFocus() == selected and calls == 0,
                  "delayed reflow preserves focus and does not evaluate")
            local offset = env.ioffset
            for i = 1, 100 do target.editor:announceSize() end
            env.on.paint(gc)
            check(env.ioffset == offset and env.theView:getFocus() == selected,
                  "repeated native size notifications leave the bounded scroll offset stable")
            env.on.help()
            local seen = {}
            for i = 1, 70 do
                drawn, draw_calls = {}, {}
                env.on.paint(gc)
                for _, call in ipairs(draw_calls) do
                    if call.y >= 15 and call.y < 225 then seen[#seen + 1] = call.text end
                end
                env.on.arrowDown()
            end
            check(table.concat(seen):find("OLD_INPUT_TAIL", 1, true)
                  and table.concat(seen):find("OLD_RESULT_TAIL", 1, true),
                  "reflowed history retains original input and result in Help")
            env.on.escapeKey()
            check(env.theView:getFocus() == selected and selected.y >= 0
                  and selected.y + selected.h <= env.fctEditor.y and calls == 0,
                  "Help returns to visible reflowed history without changing focus")
            platform.window.width = function() return 320 end
        end
    end
    end)()
end

do
    (function()
    local module, requests = copyModule(), 0
    local record = {}
    for key, value in pairs(fake_result) do record[key] = value end
    record.steps = {
        {kind="transformation", phase="solve", name="Scale the equation", goal="Isolate x", depth=0,
         verified=true, failed=false, before="3*x=12", after="x=4", action="Divide both sides by 3",
         short="Equal division preserves equality", detail="CLASSROOM_DETAIL_TAIL",
         domain="3 != 0", assumes="x is real", rule="eq.divide", checks="exact relation: passed"},
        {kind="plan", phase="plan", name="Choose a method", goal="Isolate the unknown", depth=0,
         verified=true, failed=false, short="Use inverse operations"},
        {kind="check", phase="check", name="Check the solution", goal="Test the original equation", depth=0,
         verified=true, failed=false, before="12=12", after="true", action="Substitute x=4"},
        {kind="branch", phase="solve", name="Nonzero coefficient", goal="Split by coefficient", depth=0,
         verified=true, failed=false, case="a != 0", resolution="rejected", settled_by="a=0"},
        {kind="transformation", phase="solve", name="Unproved operation", goal="Inspect a candidate", depth=0,
         verified=false, failed=false, before="x=1", after="x=2", action="Add one to x"},
    }
    record.step_count = #record.steps
    module.integrate = function(expression)
        requests = requests + 1
        record.original_expression, record.normalized_expression = expression, expression
        return record
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.fctEditor.editor:setExpression("\\0el {!i x}")
    env.on.enterKey()
    local function scan(reader)
        check(env.steps.active, "classroom fixture stays active: " .. tostring(env.steps.status))
        env.steps.stepScroll = 0
        if reader then env.readFullText() else env.steps.view = "step" end
        local seen = {}
        for i = 1, 90 do
            drawn, draw_calls = {}, {}
            env.on.paint(gc)
            for _, call in ipairs(draw_calls) do
                if call.y >= 15 and call.y < 225 then seen[#seen + 1] = call.text end
            end
            env.on.arrowDown()
        end
        if reader then env.on.escapeKey() end
        return table.concat(seen, " "):gsub("%s", "")
    end
    for _, detail in ipairs({1, 2}) do
        env.steps.detail, env.steps.focus = detail, 1
        for _, reader in ipairs({false, true}) do
            local seen = scan(reader)
            check(seen:find("Do:Dividebothsidesby3", 1, true) and seen:find("Write:", 1, true)
                  and seen:find("Startingfrom:", 1, true),
                  "verified StepInfo exposes the recorded instruction and writing labels " .. detail .. "/" .. tostring(reader))
            check(seen:find("Requires:3≠0", 1, true) and seen:find("Assumes:xisreal", 1, true),
                  "StepInfo conditions remain readable at every detail level " .. detail .. "/" .. tostring(reader))
            check((seen:find("CLASSROOM_DETAIL_TAIL", 1, true) ~= nil) == (reader or detail == 2),
                  "expanded explanations remain available without crowding standard StepInfo")
        end
    end
    for _, reader in ipairs({false, true}) do
        env.steps.focus = 2
        local seen = scan(reader)
        check(seen:find("Strategy:Isolatetheunknown", 1, true) and not seen:find("Write:", 1, true),
              "plan records describe strategy without instructing a mathematical line")
        env.steps.focus = 3
        seen = scan(reader)
        check(seen:find("Check:Substitutex=4", 1, true) and seen:find("Expected:", 1, true)
              and seen:find("Observed:", 1, true) and not seen:find("Write:", 1, true),
              "check records distinguish method, expected relation and observation")
        env.steps.focus = 4
        seen = scan(reader)
        check(seen:find("Condition:", 1, true) and seen:find("ruledout", 1, true)
              and not seen:find("Write:", 1, true), "case records expose their condition and resolution")
        env.steps.focus = 5
        for _, failed in ipairs({false, true}) do
            record.steps[5].failed = failed
            seen = scan(reader)
            check(not seen:find("Do:", 1, true) and not seen:find("Write:", 1, true)
                  and seen:find("Attempted:Addonetox", 1, true) and seen:find("Unverified:", 1, true),
                  "unverified or failed work is not presented as a line to copy")
        end
    end
    record.steps[1].after = "PARENT_ANSWER_SECRET"
    record.steps[1].action = "Write PARENT_ANSWER_SECRET"
    record.steps[1].short = "Reason PARENT_ANSWER_SECRET"
    record.steps[1].detail = "Detail PARENT_ANSWER_SECRET"
    record.steps[1].checks = "Passed PARENT_ANSWER_SECRET"
    record.steps[2].depth = 1
    env.steps.focus, env.steps.walkthrough, env.steps.revealed = 1, "hint", 1
    for _, reader in ipairs({false, true}) do
        local seen = scan(reader)
        check(not seen:find("PARENT_ANSWER_SECRET", 1, true),
              "a parent record cannot reveal its unfinished descendant's answer in StepInfo or its reader")
    end
    env.steps.view = "list"
    env.on.backtabKey()
    check(not table.concat((function()
        local rows = {}
        for _, row in ipairs(env.steps.rows or {}) do rows[#rows + 1] = row.text end
        return rows
    end)()):find("PARENT_ANSWER_SECRET", 1, true),
          "the shared hint projection also withholds parent output in the walkthrough list")
    env.steps.revealed = 2
    env.steps.focus = 1
    check(scan(true):find("PARENT_ANSWER_SECRET", 1, true) ~= nil,
          "finishing a parent's recorded descendants makes its instruction and output available")
    check(record.steps[1].action == "Write PARENT_ANSWER_SECRET" and requests == 1,
          "hint projection retains recorded actions without changing data or calling the solver again")
    end)()
end

do
    (function()
    local module, evaluations = copyModule(), 0
    module.caseval = function(command)
        if command ~= "version()" then evaluations = evaluations + 1 end
        return "unused"
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.dispinfos = false
    local expression = string.rep("x+", 120) .. "EDITOR_HELP_TAIL"
    env.fctEditor.editor:setExpression("\\0el {" .. expression .. "}")
    env.addME(expression, string.rep("7", 200) .. "HISTORY_HELP_TAIL")
    local help, delivered = env.on.help, 0
    env.on.help = function(...) delivered = delivered + 1 return help(...) end
    for _, editor in ipairs({env.fctEditor, env.histME1[1], env.histME2[1]}) do
        env.theView:setFocus(editor)
        env.reposView()
        local filter = editor.editor.filter.help
        check(type(filter) == "function", "native MathEditor filter explicitly handles Help")
        if type(filter) == "function" then
            local before = delivered
            check(editor.editor:hasFocus() and editor.editor.visible,
                  "the selected editor owns visible native focus before Help")
            check(filter() == true and delivered == before + 1,
                  "native Help forwards once and consumes the event")
            check(not editor.editor:hasFocus() and not editor.editor.visible,
                  "the reader hides its editor and releases native focus")
            local seen, title = {}, false
            for i = 1, 120 do
                drawn, draw_calls = {}, {}
                env.on.paint(gc)
                title = title or table.concat(drawn):find("Full text", 1, true) ~= nil
                for _, call in ipairs(draw_calls) do
                    if call.y >= 15 and call.y < 225 then seen[#seen + 1] = call.text end
                end
                env.on.arrowDown()
            end
            local text = table.concat(seen):gsub("%s", "")
            check(title and text:find("EDITOR_HELP_TAIL", 1, true)
                  and (editor == env.fctEditor or text:find("HISTORY_HELP_TAIL", 1, true)),
                  "native Help opens the reader with complete selected input and history")
            check(editor.editor.filter.escapeKey() == true and env.theView:getFocus() == editor,
                  "native Escape returns from Help to the same editor")
            check(editor.editor:hasFocus() and editor.editor.visible and editor.editor.x >= 0,
                  "reader close immediately restores the selected native editor focus")
            env.on.paint(gc)
            local owners = 0
            for _, widget in ipairs(env.theView.focusList) do
                if widget.editor and widget.editor:hasFocus() then owners = owners + 1 end
            end
            check(editor.editor:hasFocus() and editor.editor.visible and owners == 1,
                  "reader close keeps exactly its selected native focus after paint")
            check(delivered == before + 1 and evaluations == 0,
                  "reading and closing native Help neither repeats Help nor evaluates mathematics")
        end
    end
    check(env.fctEditor:getExpression() == expression and env.on.save().expression == expression,
          "native Help routing preserves unsent input and saved content")
    end)()
end

do
    (function()
    for _, selected in ipairs({"input", "history input", "history result"}) do
        local env = loadIsolated(copyModule())
        env.on.paint(gc)
        env.dispinfos = false
        env.fctEditor.editor:setExpression("\\0el {12345}")
        env.addME("3+4", "7")
        local editor = selected == "input" and env.fctEditor
            or selected == "history input" and env.histME1[1] or env.histME2[1]
        env.theView:setFocus(editor)
        env.reposView()
        editor.editor.filter.help()
        env.on.escapeKey()
        local owner, count = nil, 0
        for _, widget in ipairs(env.theView.focusList) do
            if widget.editor and widget.editor.visible and widget.editor:hasFocus() then
                owner, count = widget, count + 1
            end
        end
        check(count == 1 and owner == editor,
              "Clear has exactly one restored native recipient: " .. selected)
        local consumed = owner and owner.editor.filter.clearKey()
        check(consumed == true and (selected == "input" and env.fctEditor:getExpression() == ""
              or selected ~= "input" and #env.histME1 == 0 and #env.histME2 == 0),
              "native Clear reaches the restored editor filter: " .. selected)
    end
    local env = loadIsolated(copyModule())
    local view = env.View(platform.window)
    local function widget()
        return {
            focused = false, acquired = 0, released = 0,
            setFocus = function(self) self.focused = true self.acquired = self.acquired + 1 end,
            releaseFocus = function(self) self.focused = false self.released = self.released + 1 end,
        }
    end
    local first, second = widget(), widget()
    view.focusList = {first, second}
    view:setFocus(first)
    first.focused = false
    view:setFocus(first)
    check(view:getFocus() == first and first.focused and first.acquired == 2 and first.released == 0,
          "same logical focus reasserts widget focus without a release cycle")
    view:setFocus(first)
    check(first.focused and first.released == 0 and not second.focused,
          "repeated focus acquisition keeps one owner without toggling focus off")
    view:setFocus(second)
    check(view:getFocus() == second and second.focused and second.acquired == 1
          and not first.focused and first.released == 1,
          "changing logical focus releases only the previous widget")
    view:setFocus(nil)
    check(view:getFocus() == nil and not second.focused and second.released == 1,
          "clearing logical focus releases the native owner")
    end)()
end

do
    (function()
    local module, calls = copyModule(), 0
    module.caseval = function(command)
        if command ~= "version()" then calls = calls + 1 end
        return "unused"
    end
    module.walkthrough = function() calls = calls + 1 return nil end
    local first_editor = #editors + 1
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.dispinfos = false
    local record = {}
    for key, value in pairs(fake_result) do record[key] = value end
    local action = "Add -2 times row 1 to row 2. Keep row 1 unchanged."
    local why = "Adding a multiple of a different row is reversible: subtract the same multiple to restore the target row."
    local before, after = "[[1,2,3],[2,4,8]]", "[[1,2,3],[0,0,2]]"
    record.steps = {
        {kind="transformation", phase="solve", name="Add a row multiple", goal="Eliminate the first entry in row 2",
         depth=0, claim="row equivalent", verified=true, failed=false, before=before, after=after,
         action=action, short=why, rule="matrix.row-add-multiple", checks="exact row operation: passed"},
        {kind="check", phase="check", name="Check each matrix entry", goal="Verify the recorded row event",
         depth=1, claim="no claim", verified=true, failed=false, action="Compare every target and unchanged entry"},
    }
    record.step_count, record.input, record.canonical = 2, before, after
    env.steps.result, env.steps.focus, env.steps.walkthrough, env.steps.revealed = record, 1, "full", 2
    env.openSteps()
    local saved_width = platform.window.width
    local function compact(s) return s:gsub("%s", "") end
    local function ownMathBox(body)
        for index = first_editor, #editors do
            local editor = editors[index]
            if editor.visible and editor.readOnly and editor.x and editor.x > -1000
                and editor.expr == "\\0el {" .. body .. "}" and mathBoxFits(editor) then return editor end
        end
    end
    local function scan(reader)
        env.steps.stepScroll = 0
        if reader then env.readFullText() else env.steps.view = "step" end
        local pages, fit, before_visible, after_visible = {}, true, false, false
        local bad
        for _ = 1, 90 do
            drawn, draw_calls = {}, {}
            env.on.paint(gc)
            local page = {}
            for _, call in ipairs(draw_calls) do
                if not bad and (call.x < 0 or call.x + gc:getStringWidth(call.text) > platform.window:width()
                    or call.y < 0 or call.y + 12 > platform.window:height()) then
                    bad = "text " .. call.text .. " x=" .. call.x .. " y=" .. call.y
                end
                fit = fit and call.x >= 0 and call.x + gc:getStringWidth(call.text) <= platform.window:width()
                    and call.y >= 0 and call.y + 12 <= platform.window:height()
                if call.y >= 15 and call.y < platform.window:height() - 15 then page[#page + 1] = call.text end
            end
            for index = first_editor, #editors do
                local editor = editors[index]
                if editor.readOnly and editor.visible and editor.x and editor.x >= 0 then
                    if not bad and (editor.x + editor.boxw > platform.window:width()
                        or editor.y < 0 or editor.y + editor.boxh > platform.window:height()) then
                        bad = "editor " .. editor.expr .. " x=" .. editor.x .. " y=" .. editor.y
                    end
                    fit = fit and editor.x + editor.boxw <= platform.window:width()
                        and editor.y >= 0 and editor.y + editor.boxh <= platform.window:height()
                end
            end
            before_visible = before_visible or ownMathBox(record.steps[1].before) ~= nil
            after_visible = after_visible or ownMathBox(record.steps[1].after) ~= nil
            pages[#pages + 1] = compact(table.concat(page))
            env.on.arrowDown()
        end
        if reader then env.on.escapeKey() end
        if not fit then print("ROW_BOUND", tostring(reader), platform.window:width(), bad) end
        return table.concat(pages, "\n"), fit, before_visible, after_visible
    end
    for _, width in ipairs({320, 180}) do
        platform.window.width = function() return width end
        env.resizeGC(gc)
        for _, reader in ipairs({false, true}) do
            local seen, fit, from_math, to_math = scan(reader)
            check(fit and seen:find(compact("Do: " .. action), 1, true)
                  and seen:find(compact("Why: " .. why), 1, true)
                  and seen:find("Write:", 1, true) and seen:find("Startingfrom:", 1, true),
                  "row presentation paints the concrete operation and reason within bounds " .. width .. "/" .. tostring(reader))
            check((from_math or seen:find(before, 1, true)) and (to_math or seen:find(after, 1, true)),
                  "row presentation exposes both complete rectangular matrices " .. width .. "/" .. tostring(reader))
            if reader then
                check(seen:find("claim:rowequivalent", 1, true), "the row-equivalence claim is readable in Full Text")
            end
        end
    end
    env.steps.walkthrough, env.steps.revealed, env.steps.focus = "hint", 1, 1
    for _, reader in ipairs({false, true}) do
        local seen, fit, _, to_math = scan(reader)
        check(fit and not to_math and not seen:find("Write:", 1, true)
              and not seen:find(compact(action), 1, true) and not seen:find(after, 1, true),
              "an unrevealed child withholds the parent row result and operation " .. tostring(reader))
    end
    env.on.tabKey()
    env.steps.focus = 1
    local shown, fit = scan(true)
    check(fit and shown:find(compact("Do: " .. action), 1, true) and shown:find(after, 1, true),
          "revealing the child makes the recorded row operation and matrix readable")
    local original_rows, transformed_rows, original_cells, transformed_cells = {}, {}, {}, {}
    for row = 1, 4 do
        local original, transformed = {}, {}
        for column = 1, 6 do
            local numerator = 1000000 + row * 100 + column
            original[column] = tostring(numerator) .. "/1000033"
            transformed[column] = tostring(row == 2 and numerator - 2 * (1000100 + column) or numerator) .. "/1000033"
            original_cells[#original_cells + 1] = original[column]
            transformed_cells[#transformed_cells + 1] = transformed[column]
        end
        original_rows[row], transformed_rows[row] = "[" .. table.concat(original, ", ") .. "]", "[" .. table.concat(transformed, ", ") .. "]"
    end
    record.steps[1].before = "[" .. table.concat(original_rows, ", ") .. "]"
    record.steps[1].after = "[" .. table.concat(transformed_rows, ", ") .. "]"
    record.input, record.canonical = record.steps[1].before, record.steps[1].after
    for _, width in ipairs({320, 180}) do
        platform.window.width = function() return width end
        env.resizeGC(gc)
        for _, reader in ipairs({false, true}) do
            local seen, fit, from_math, to_math = scan(reader)
            local complete = true
            for _, cell in ipairs(original_cells) do complete = complete and seen:find(cell, 1, true) ~= nil end
            for _, cell in ipairs(transformed_cells) do complete = complete and seen:find(cell, 1, true) ~= nil end
            check(fit and complete and not from_math and not to_math,
                  "oversized exact 4 by 6 row matrices have a complete bounded text fallback " .. width .. "/" .. tostring(reader))
        end
    end
    check(calls == 0, "row presentation and hint navigation perform no symbolic computation")
    platform.window.width = saved_width
    env.resizeGC(gc)
    end)()
end

if os.getenv("NPS_COMMAND_MODULE") then
    (function()
    local fixture, loaded = nps_split, package.loaded.nps_split
    nps_split, package.loaded.nps_split = nil, nil
    local native = assert(package.loadlib(os.getenv("NPS_COMMAND_MODULE"), "luaopen_nps_split"))()
    nps_split, package.loaded.nps_split = fixture, loaded
    local backend = luagiac
    luagiac = nil
    local command = "diff((x^2+1)*(sin(x)+cos(x)),x)"
    local record = native.walkthrough(command, "x")
    check(record.solved and #record.steps == 8, "deferred math layout uses a real eight-step product derivative")
    local module, requests = copyModule(), 0
    module.walkthrough = function() requests = requests + 1 return record end
    local first_editor = #editors + 1
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.fctEditor.editor:setExpression("\\0el {" .. command .. "}")
    env.on.enterKey()
    env.steps.focus, env.steps.view = 5, "step"
    local function showing(editor)
        return editor and editor.visible and editor.x and editor.x >= 0
    end
    local function find_editor(expression)
        for index = first_editor, #editors do
            local editor = editors[index]
            if editor.readOnly and editor.expr == "\\0el {" .. expression .. "}" then return editor end
        end
    end
    local editor
    for scroll = 0, 20 do
        env.steps.stepScroll = scroll
        env.on.paint(gc)
        env.on.paint(gc)
        editor = find_editor(record.steps[5].after)
        if showing(editor) then break end
    end
    check(showing(editor), "a short recorded derivative establishes the native Write measurement")
    if editor then
        local old_listener, old_width, old_height = editor.onSize, editor.natural, editor.needH
        editor.deferSize = true
        env.on.arrowLeft()
        env.on.arrowLeft()
        env.on.arrowLeft()
        check(env.steps.focus == 2, "left navigation selects the longer recorded product-rule result")
        env.steps.stepScroll = 0
        env.on.paint(gc)
        local waiting_listener = editor.onSize
        local premature = false
        for scroll = 0, 25 do
            env.steps.stepScroll = scroll
            old_listener(editor, old_width, old_height)
            env.on.paint(gc)
            premature = premature or showing(editor)
        end
        check(not premature, "a prior expression callback never places the unmeasured longer Write expression")
        local unchanged_listener = editor.onSize == waiting_listener
        check(unchanged_listener, "waiting for an expression measurement does not repeatedly replace the request")
        editor.deferSize = false
        editor:announceSize()
        env.on.paint(gc)
        check(not showing(editor), "the current oversized derivative measurement selects wrapped fallback")
        local larger_listener = editor.onSize
        editor.deferSize = true
        local original_width = platform.window.width
        platform.window.width = function() return 180 end
        env.resizeGC(gc)
        env.on.paint(gc)
        premature = false
        for scroll = 0, 25 do
            env.steps.stepScroll = scroll
            larger_listener(editor, 6, 13)
            env.on.paint(gc)
            premature = premature or showing(editor)
        end
        check(not premature, "a previous width callback cannot admit a reconfigured native frame")
        editor.deferSize = false
        editor:announceSize()
        for i = 1, 4 do env.on.paint(gc) end
        env.readFullText()
        local lines, fits = {}, true
        for i = 1, 180 do
            drawn, draw_calls = {}, {}
            env.on.paint(gc)
            for _, call in ipairs(draw_calls) do
                fits = fits and call.x >= 0 and call.x + gc:getStringWidth(call.text) <= 180
                    and call.y >= 0 and call.y + 12 <= 240
                if call.y == env.STEP_LINE then lines[#lines + 1] = call.text end
            end
            env.on.arrowDown()
        end
        check(fits and table.concat(lines):gsub("%s", ""):find(record.steps[2].after:gsub("%s", ""), 1, true),
              "the complete recorded derivative remains reachable while native measurements are delayed")
        env.on.escapeKey()
        env.steps.walkthrough, env.steps.revealed = "hint", 2
        env.readFullText()
        local leaked = false
        for i = 1, 90 do
            drawn = {}
            env.on.paint(gc)
            leaked = leaked or table.concat(drawn):find("Write:", 1, true) ~= nil
            env.on.arrowDown()
        end
        check(not leaked and requests == 1, "deferred rendering preserves hint withholding without another solve")
        env.on.escapeKey()
        platform.window.width = original_width
        env.resizeGC(gc)
    end
    local new_editor, unsupported_font = D2Editor.newRichText, nil
    D2Editor.newRichText = function()
        local strict_editor = new_editor()
        local set_font = strict_editor.setFontSize
        function strict_editor:setFontSize(size)
            if size and not ({[7]=true, [9]=true, [10]=true, [11]=true, [12]=true, [16]=true, [24]=true})[size] then
                unsupported_font = size
                error("unsupported handheld editor font " .. tostring(size))
            end
            return set_font(self, size)
        end
        return strict_editor
    end
    local strict_env = loadIsolated(module)
    strict_env.on.paint(gc)
    strict_env.fctEditor.editor:setExpression("\\0el {" .. command .. "}")
    strict_env.on.enterKey()
    for i = 1, 4 do strict_env.on.paint(gc) end
    D2Editor.newRichText = new_editor
    check(not unsupported_font and strict_env.steps.active,
          "oversized native mathematics uses only documented handheld editor font sizes")
    luagiac = backend
    end)()
end

if os.getenv("NPS_DETERMINANT_RECORDS") then
    (function()
    local records = assert(loadfile(os.getenv("NPS_DETERMINANT_RECORDS")))()
    local saved_width = platform.window.width
    local function compact(value) return value:gsub("%s", "") end
    for _, name in ipairs({"swap", "fraction"}) do
        local record = assert(records[name])
        check(record.mode == "determinant" and record.solved and record.has_result and not record.answer_only,
              "the determinant UI fixture is an actual successful native scalar walkthrough: " .. name)
        local module, requests, evaluations = copyModule(), 0, 0
        module.walkthrough = function(command)
            requests = requests + 1
            assert(command == record.request_expression)
            return record
        end
        module.caseval = function(command)
            if command ~= "version()" then evaluations = evaluations + 1 end
            return nps_split.caseval(command)
        end
        local first_editor = #editors + 1
        local env = loadIsolated(module)
        env.on.paint(gc)
        local selected = false
        for _, category in ipairs(env.menu) do
            for index = 2, #category do
                local item = category[index]
                if type(item) == "table" and item[1]:find("Determinant", 1, true) == 1 then
                    item[2]()
                    selected = true
                end
            end
        end
        check(selected and env.fctEditor:getExpression() == "det(", "the determinant menu retains its input template")
        env.fctEditor:addString(record.request_expression:sub(5))
        env.on.enterKey()
        check(env.steps.active and env.steps.result == record and requests == 1 and evaluations == 0,
              "the real determinant record opens from its ordinary menu command: " .. name)
        local function scan(reader)
            env.steps.view, env.steps.stepScroll, env.steps.detail = "step", 0, 2
            if reader then env.readFullText() end
            local lines, fits = {}, true
            for i = 1, 180 do
                drawn, draw_calls = {}, {}
                env.on.paint(gc)
                local top = reader and env.STEP_LINE or env.steps.headerHeight
                local layout = not reader and env.steps.detailLayout
                local first = layout and layout.items[env.steps.stepScroll + 1]
                local firstBottom = top + (first and first.height or env.STEP_LINE)
                for _, call in ipairs(draw_calls) do
                    fits = fits and call.x >= 0 and call.x + gc:getStringWidth(call.text) <= platform.window:width()
                        and call.y >= 0 and call.y + 12 <= platform.window:height()
                    if call.y >= top and call.y < firstBottom then lines[#lines + 1] = call.text end
                end
                for index = first_editor, #editors do
                    local editor = editors[index]
                    if editor.visible and editor.readOnly and editor.x and editor.x >= 0 then
                        fits = fits and mathBoxFits(editor) and editor.x + editor.boxw <= platform.window:width()
                            and editor.y >= 0 and editor.y + editor.boxh <= platform.window:height()
                        if editor.y >= top and editor.y < firstBottom then lines[#lines + 1] = editor.expr:sub(7, -2) end
                    end
                end
                env.on.arrowDown()
            end
            if reader then env.on.escapeKey() end
            return compact(table.concat(lines)), fits
        end
        for _, width in ipairs({320, 180}) do
            platform.window.width = function() return width end
            env.resizeGC(gc)
            local transformations = 0
            for index, step in ipairs(record.steps) do
                if step.kind == "transformation" then
                    transformations = transformations + 1
                    env.steps.focus = index
                    for _, reader in ipairs({false, true}) do
                        local text, fits = scan(reader)
                        check(fits and text:find(compact("Do: " .. step.action), 1, true) and
                              text:find(compact("Write: " .. step.after), 1, true) and
                              text:find(compact("Why: " .. step.short), 1, true),
                              "recorded determinant Do, Write and Why remain complete: " .. name .. "/" ..
                              tostring(index) .. "/" .. width .. "/" .. tostring(reader))
                    end
                end
            end
            check(transformations > 0, "the determinant presentation exercises actual transformations")
            env.steps.walkthrough, env.steps.revealed, env.steps.focus = "hint", 1, 1
            local withheld, fits = scan(true)
            check(fits and not withheld:find("Answer:", 1, true),
                  "the determinant full reader withholds the final answer before hint progression")
            while env.steps.revealed < #record.steps do env.on.tabKey() end
            local shown, complete = scan(true)
            check(complete and shown:find(compact("Answer: " .. record.result), 1, true),
                  "the final determinant becomes readable when every hint is revealed")
            env.steps.walkthrough = "full"
        end
        check(requests == 1 and evaluations == 0, "determinant guidance and hints do not recompute the solve")
    end
    platform.window.width = saved_width
    end)()
end

do
    (function()
    for _, overlay in ipairs({"list", "detail", "hint", "physics", "reader"}) do
        for _, selected in ipairs({"input", "history input", "history result"}) do
            for _, event in ipairs({"charIn", "tabKey", "arrowLeft", "arrowRight",
                                   "backspaceKey", "deleteKey", "clearKey", "mouseDown"}) do
                local module, requests = copyModule(), 0
                module.caseval = function(command)
                    if command ~= "version()" then requests = requests + 1 end
                    return nps_split.caseval(command)
                end
                local env = loadIsolated(module)
                env.on.paint(gc)
                env.dispinfos = false
                env.addME("19/4", "19/4")
                env.fctEditor.editor:setExpression("\\0el {" .. string.rep("x+", 80) .. "12345}", 9)
                local editor = selected == "input" and env.fctEditor
                    or selected == "history input" and env.histME1[1] or env.histME2[1]
                env.theView:setFocus(editor)
                env.reposView()
                editor.editor.curpos = 9
                local record = {}
                for key, value in pairs(fake_result) do record[key] = value end
                record.steps = {}
                for index = 1, 2 do
                    local step = {}
                    for key, value in pairs(fake_result.steps[1]) do step[key] = value end
                    step.kind, step.depth = "transformation", 0
                    step.name = "Filter routing step " .. index
                    step.after = index == 1 and "x+1" or "FILTER_WITHHELD_ANSWER"
                    record.steps[index] = step
                end
                record.step_count, record.display_result = 2, "FILTER_WITHHELD_ANSWER"
                env.steps.result = record
                if overlay == "physics" then env.openPhysicsFixtures()
                elseif overlay == "reader" then env.readFullText()
                else
                    env.steps.walkthrough = overlay == "hint" and "hint" or "full"
                    env.steps.revealed = overlay == "hint" and 1 or 2
                    env.steps.view = overlay == "detail" and "step" or "list"
                    env.stepsToggleDetail()
                    env.openSteps()
                end
                check(overlay == "reader" or env.steps.active or env.physicsBrowser.active,
                      "the native-filter fixture opens its requested overlay: " .. overlay)
                drawn = {}
                env.on.paint(gc)
                local before_page = table.concat(drawn)
                local input = env.fctEditor.editor:getExpression()
                local first, second = env.histME1[1], env.histME2[1]
                local first_text, second_text = first.editor:getExpression(), second.editor:getExpression()
                local focused, captured = env.theView:getFocus(), env.theView.mouseCaptured
                local cursor = editor.editor.curpos
                local delivered, handler = 0, env.on[event]
                if handler then
                    env.on[event] = function(...)
                        delivered = delivered + 1
                        return handler(...)
                    end
                end
                local consumed = editor.editor.filter[event](event == "charIn" and "t" or editor.x + 1, editor.y + 1)
                local label = overlay .. "/" .. selected .. "/" .. event
                check(consumed == true, "an active overlay consumes its native editor event: " .. label)
                check(env.fctEditor.editor:getExpression() == input and
                      env.histME1[1] == first and env.histME2[1] == second and
                      first.editor:getExpression() == first_text and second.editor:getExpression() == second_text and
                      #env.steps.histText == 1 and #env.histME1 == 1 and #env.histME2 == 1,
                      "overlay keys preserve unsent input and both history editors: " .. label)
                check(env.theView:getFocus() == focused and env.theView.mouseCaptured == captured and
                      editor.editor.curpos == cursor,
                      "overlay events cannot move hidden focus, capture or caret: " .. label)
                check(delivered == (handler and 1 or 0),
                      "native overlay events reach the guarded handler exactly once: " .. label)
                drawn = {}
                env.on.paint(gc)
                local page = table.concat(drawn)
                if event == "charIn" then
                    check(page:find("Full text", 1, true) ~= nil,
                          "T reaches full text through every native editor: " .. label)
                    if overlay == "hint" then
                        local leaked = false
                        for _ = 1, 50 do
                            drawn = {}
                            env.on.paint(gc)
                            leaked = leaked or table.concat(drawn):find("FILTER_WITHHELD_ANSWER", 1, true) ~= nil
                            env.on.arrowDown()
                        end
                        check(not leaked and env.steps.revealed == 1,
                              "native T preserves unrevealed hint answers: " .. label)
                    end
                    env.on.escapeKey()
                elseif event == "tabKey" and overlay == "hint" then
                    check(env.steps.revealed == 2, "native Tab advances the hint: " .. label)
                elseif event == "tabKey" and overlay == "reader" then
                    check(page ~= before_page, "native Tab pages the full-text reader: " .. label)
                elseif event == "arrowRight" and overlay == "detail" then
                    check(env.steps.focus == 2, "native Right moves to the next recorded step: " .. label)
                end
                if overlay == "reader" and event ~= "charIn" then env.on.escapeKey() end
                if env.steps.active then env.closeSteps() end
                if env.physicsBrowser.active then env.on.escapeKey() end
                local restored = overlay == "reader" and editor or env.fctEditor
                check(env.theView:getFocus() == restored and restored.editor:hasFocus() and restored.editor.visible,
                      "closing the overlay restores its existing editor focus: " .. label)
                check(requests == 0, "overlay filter navigation performs no evaluation: " .. label)
            end
        end
    end
    for _, selected in ipairs({"input", "history input", "history result"}) do
        for _, event in ipairs({"charIn", "tabKey", "arrowLeft", "arrowRight",
                               "backspaceKey", "deleteKey", "clearKey"}) do
            local env = loadIsolated(copyModule())
            env.on.paint(gc)
            env.dispinfos = false
            env.addME(string.rep("x+", 30) .. "1", string.rep("y+", 30) .. "2")
            env.fctEditor.editor:setExpression("\\0el {12345}", 9)
            local editor = selected == "input" and env.fctEditor
                or selected == "history input" and env.histME1[1] or env.histME2[1]
            env.theView:setFocus(editor)
            env.reposView()
            editor.editor.curpos = 9
            local consumed = editor.editor.filter[event]("z")
            local label = selected .. "/" .. event
            if event == "tabKey" then
                check(consumed == true and env.theView:getFocus() ~= editor,
                      "native Tab retains ordinary shell focus cycling: " .. label)
            elseif event == "clearKey" then
                check(consumed == true and (selected == "input" and env.fctEditor:getExpression() == ""
                      or selected ~= "input" and #env.histME1 == 0 and #env.histME2 == 0),
                      "native Clear retains ordinary shell editing: " .. label)
            elseif event == "backspaceKey" or event == "deleteKey" then
                check(selected == "input" and consumed == false and #env.histME1 == 1
                      or selected ~= "input" and consumed == true and #env.histME1 == 0 and #env.histME2 == 0,
                      "native deletion retains ordinary input and history behavior: " .. label)
            elseif event == "charIn" then
                check(consumed == (selected == "input" and false or editor.readOnly),
                      "native characters retain ordinary input and history behavior: " .. label)
            else
                check(consumed == false, "interior native cursor movement remains with the editor: " .. label)
            end
        end
    end
    end)()
end

-- A solve opens its walkthrough before enterHandler records the row. That row is created after the
-- overlay hid the existing editor set, so its own native visibility has to be cleared separately.
do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    env.steps.result = fake_result
    env.openSteps()
    env.addME("overlay_input", "overlay_answer")
    local input, answer = env.histME1[1], env.histME2[1]
    check(env.steps.active and not input.editor.visible and not answer.editor.visible and
          input.editor.x == -10000 and answer.editor.x == -10000,
          "a history row created after Steps opens is hidden and parked under the overlay")
    env.closeSteps()
    env.on.paint(gc)
    check(input.editor.visible and answer.editor.visible and input.editor.x >= 0 and answer.editor.x >= 0,
          "closing Steps restores the newly recorded history row on screen")
    env.theView:setFocus(answer)
    env.on.enterKey()
    check(env.fctEditor:getExpression() == "overlay_answer",
          "the restored result row remains usable for recall")
    end)()
end

do
    (function()
    for _, case in ipairs({
        {"fraction", "1/2", 11},
        {"determinant", "(det([[3, 4], [0, 2]]) * ((-1)^-1))", 11},
        {"smaller font", "(12345678901234567890123456789012345/123456789)", 7},
    }) do
        local module, requests = copyModule(), 0
        module.walkthrough = function() requests = requests + 1 return nil end
        local env = loadIsolated(module)
        env.on.paint(gc)
        local first_editor = #editors + 1
        local record = {}
        for key, value in pairs(fake_result) do record[key] = value end
        record.steps = {{
            kind = "transformation", phase = "solve", name = "Keep the exact value",
            goal = "Write the equivalent expression", depth = 0,
            claim = "equivalent expression", verified = true, failed = false,
            action = "Write the equivalent expression", after = case[2],
        }}
        record.step_count, record.input, record.display_result = 1, case[2], case[2]
        env.steps.result, env.steps.view, env.steps.focus = record, "step", 1
        env.steps.walkthrough, env.steps.revealed = "full", 1
        env.openSteps()
        drawn, draw_calls = {}, {}
        env.on.paint(gc)
        local measured, displayed, write
        for _, call in ipairs(draw_calls) do
            if call.text == "Write:" then write = call end
        end
        for index = first_editor, #editors do
            local editor = editors[index]
            if editor.expr == "\\0el {" .. case[2] .. "}" and editor.measuredAt and editor.natural > 0 then
                measured = true
                if write and editor.visible and editor.readOnly and
                    write.y == editor.y + math.max(0, math.floor((editor.needH - 12) / 2)) and
                    editor.x > write.x and editor.font == case[3] and mathBoxFits(editor) and
                    editor.x + editor.boxw <= platform.window:width() and
                    editor.y + editor.boxh <= platform.window:height() then displayed = true end
            end
        end
        check(measured, "the native stub supplies synchronous positive dimensions: " .. case[1])
        check(displayed, "the first detail paint displays the fitting native equation: " .. case[1])
        check(requests == 0, "first-paint equation layout does not repeat the solver: " .. case[1])
    end
    end)()
end

do
    (function()
    for _, overlay in ipairs({"reader", "steps", "physics"}) do
        local module, requests = copyModule(), 0
        module.caseval = function(command)
            if command ~= "version()" then requests = requests + 1 end
            return nps_split.caseval(command)
        end
        local env = loadIsolated(module)
        env.on.paint(gc)
        env.fctEditor.editor:setExpression("\\0el {retained_input}")
        env.addME("retained_history", "retained_answer")
        env.theView:setFocus(env.histME2[1])
        local label = "full text"
        if overlay == "reader" then
            env.on.help()
        elseif overlay == "steps" then
            env.steps.result = fake_result
            env.openSteps()
            label = "steps"
        else
            env.openPhysicsFixtures()
            label = "physics browser"
        end
        requests = 0
        local original, inject = gc.fillRect, true
        gc.fillRect = function(...)
            if inject then inject = false error("injected overlay paint failure", 0) end
            return original(...)
        end
        drawn = {}
        local survived = pcall(env.on.paint, gc)
        gc.fillRect = original
        check(survived, "overlay paint catches a one-shot native error: " .. overlay)
        check(table.concat(drawn, "\n"):find(label .. " paint failed", 1, true) ~= nil,
              "overlay paint failure remains readable: " .. overlay)
        check(pcall(env.on.paint, gc), "the overlay can paint again after its failure: " .. overlay)
        env.on.escapeKey()
        check(env.fctEditor:getExpression() == "retained_input" and #env.histME1 == 1 and
              env.histME2[1]:getExpression() == "retained_answer",
              "overlay paint failure preserves input and history: " .. overlay)
        local restored = overlay == "reader" and env.histME2[1] or env.fctEditor
        check(env.theView:getFocus() == restored and restored.editor:hasFocus() and restored.editor.visible,
              "overlay paint failure permits restoring native focus: " .. overlay)
        check(requests == 0, "overlay paint recovery never reruns the solver: " .. overlay)
    end
    end)()
end

do
    (function()
    local function choose(env, label)
        for _, group in ipairs(env.menu) do
            for _, item in ipairs(group) do
                if type(item) == "table" and item[1] == label then item[2]() return end
            end
        end
        error("missing menu callback: " .. label)
    end
    for _, focused in ipairs({"entry", "history"}) do
        local module = copyModule()
        module.os_msgbox = function() return 2 end
        local env = loadIsolated(module)
        env.on.paint(gc)
        env.dispinfos = false
        env.addME("old_input", "old_result")
        env.fctEditor.editor:setExpression("\\0el {12345}", 8)
        env.theView:setFocus(focused == "entry" and env.fctEditor or env.histME2[1])
        env.fsize = 14
        local entry, old_input, old_result = env.fctEditor, env.histME1[1], env.histME2[1]
        choose(env, "Clear History")
        env.on.paint(gc)
        check(env.fctEditor == entry and entry:getExpression() == "12345" and entry.editor.curpos == 8,
              "Clear History preserves the pending editor and caret from " .. focused)
        check(#env.histME1 == 0 and #env.histME2 == 0 and #env.steps.histText == 0 and
              not old_input.editor.visible and not old_result.editor.visible,
              "Clear History removes both native history columns and saved text from " .. focused)
        check(env.theView:getFocus() == entry and entry.editor:hasFocus() and entry.editor.x >= 0,
              "Clear History restores usable entry focus from " .. focused)
        check(env.fsize == 14, "Clear History preserves the selected font size")
    end
    for _, start in ipairs({"steps", "reader"}) do
        local env = loadIsolated(copyModule())
        env.on.paint(gc)
        env.fctEditor.editor:setExpression("\\0el {UNENTERED}")
        env.addME("old_history", "old_answer")
        env.theView:setFocus(env.histME2[1])
        env.steps.result = { input = "OLD_WALKTHROUGH_INPUT", steps = {}, outcome = "unsupported" }
        if start == "steps" then env.openSteps() else env.on.help() end
        choose(env, "Guided physics problems")
        check(env.physicsBrowser.active and not env.steps.active,
              "Physics replaces the previous overlay from " .. start)
        check(not env.histME2[1].editor:hasFocus() and not env.fctEditor.editor:hasFocus(),
              "Physics releases every shell editor focus from " .. start)
        env.on.charIn("T")
        local text = ""
        for _ = 1, 50 do
            drawn = {}
            env.on.paint(gc)
            text = text .. table.concat(drawn, "\n")
            env.on.arrowDown()
        end
        check(not text:find("OLD_WALKTHROUGH_INPUT", 1, true) and text:find("cubic centimetres", 1, true),
              "Full Text describes the visible physics selection from " .. start)
        env.on.escapeKey()
        check(env.physicsBrowser.active and not env.fctEditor.editor.visible and
              not env.fctEditor.editor:hasFocus(), "Full Text returns to Physics from " .. start)
        env.on.escapeKey()
        env.on.paint(gc)
        check(not env.steps.active and not env.physicsBrowser.active and env.fctEditor.editor.visible and
              env.fctEditor.editor:hasFocus() and env.fctEditor.editor.x >= 0,
              "Physics closes to the visible focused entry from " .. start)
        check(env.fctEditor.editor.filter.charIn("9") == false and
              env.fctEditor:getExpression() == "UNENTERED", "the closed overlay releases native typing")
    end
    for _, label in ipairs({"Square root", "Open Script Editor", "Factor Integer  ifactor(n)"}) do
        local env = loadIsolated(copyModule())
        env.on.paint(gc)
        env.fctEditor.editor:setExpression("\\0el {UNENTERED}")
        env.on.help()
        choose(env, label)
        check(env.fctEditor:getExpression() == "UNENTERED" and not env.fctEditor.editor:hasFocus(),
              "Full Text refuses hidden entry mutation from " .. label)
        env.on.escapeKey()
        choose(env, label)
        check(env.fctEditor:getExpression() ~= "UNENTERED", "the shell still accepts " .. label)
    end
    for _, overlay in ipairs({"reader", "steps", "physics"}) do
        local module, requests = copyModule(), 0
        module.os_msgbox = function() return 2 end
        module.caseval = function(command)
            if command ~= "version()" then requests = requests + 1 end
            return nps_split.caseval(command)
        end
        local env = loadIsolated(module)
        env.on.paint(gc)
        env.fctEditor.editor:setExpression("\\0el {UNENTERED}")
        env.addME("old_history", "old_answer")
        env.theView:setFocus(env.histME2[1])
        local removed = env.histME2[1]
        if overlay == "reader" then
            env.on.help()
        elseif overlay == "steps" then
            env.steps.result = fake_result
            env.openSteps()
        else
            choose(env, "Guided physics problems")
        end
        choose(env, "Clear History")
        env.on.paint(gc)
        check(#env.histME1 == 0 and #env.histME2 == 0 and #env.steps.histText == 0 and
              env.fctEditor:getExpression() == "UNENTERED", "clearing history is scoped under " .. overlay)
        check(not env.fctEditor.editor.visible and not env.fctEditor.editor:hasFocus() and
              not removed.editor.visible and not removed.editor:hasFocus(),
              "clearing history leaves shell editors hidden under " .. overlay)
        env.on.escapeKey()
        env.on.paint(gc)
        check(env.theView:getFocus() == env.fctEditor and env.fctEditor.editor:hasFocus() and
              env.fctEditor.editor.visible and env.fctEditor.editor.x >= 0 and
              not removed.editor:hasFocus(), "closing " .. overlay .. " cannot restore deleted history focus")
        check(requests == 0, "clearing history under " .. overlay .. " never evaluates the pending entry")
    end
    for _, source in ipairs({"physics", "reader", "physics reader"}) do
        local env = loadIsolated(copyModule())
        env.on.paint(gc)
        env.fctEditor.editor:setExpression("\\0el {UNENTERED}")
        if source ~= "reader" then choose(env, "Guided physics problems") end
        if source ~= "physics" then env.on.help() end
        env.steps.result = fake_result
        env.openSteps()
        drawn = {}
        env.on.paint(gc)
        check(env.steps.active and not env.physicsBrowser.active and
              not table.concat(drawn, "\n"):find("Full text", 1, true),
              "opening Steps replaces " .. source)
        check(not env.fctEditor.editor:hasFocus() and not env.fctEditor.editor.visible,
              "opening Steps keeps the entry parked from " .. source)
        env.on.escapeKey()
        env.on.paint(gc)
        check(not env.steps.active and not env.physicsBrowser.active and env.fctEditor.editor:hasFocus() and
              env.fctEditor.editor.visible and env.fctEditor:getExpression() == "UNENTERED",
              "Steps closes once to the unchanged entry from " .. source)
    end
    end)()
end

do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local result = {}
    for key, value in pairs(fake_result) do result[key] = value end
    local step = {}
    for key, value in pairs(fake_result.steps[2]) do step[key] = value end
    step.depth = 0
    step.before = "int(x,x)"
    step.after = "x^2/2"
    result.steps = {step}
    result.step_count = 1
    result.assumptions = nil
    env.steps.result = result
    env.steps.detail = 2
    env.stepsToggleDetail()
    env.openSteps()
    drawn = {}
    env.on.paint(gc)
    check(mathBoxExact("∫(x,x)") ~= nil,
          "the step list uses the integral sign")
    env.steps.view = "step"
    step.before = "int(" .. string.rep("x+", 60) .. "x,x)"
    drawn = {}
    for _ = 1, 40 do env.on.paint(gc) env.on.arrowDown() end
    check(table.concat(drawn, "\n"):find("∫(", 1, true) ~= nil and
          not table.concat(drawn, "\n"):find("int(", 1, true),
          "oversized step expressions keep mathematical signs in wrapped text")
    env.on.charIn("t")
    drawn = {}
    for _ = 1, 40 do env.on.paint(gc) env.on.arrowDown() end
    check(table.concat(drawn, "\n"):find("∫(", 1, true) ~= nil,
          "the full text reader uses mathematical signs")
    check(step.before:sub(1, 4) == "int(" and result.steps[1] == step,
          "display notation leaves the native derivation unchanged")
    env.on.escapeKey()
    env.steps.view = "result"
    for _, notation in ipairs({
        {"int (x,x)", "∫ (x,x)"}, {"integrate(x,x)", "∫(x,x)"},
        {"d(x^2,x)", "(x^2,x)"}, {"diff(x^2,x)", "(x^2,x)"},
        {"diff(x^5,x,3)", "(x^5,x,3)"}, {"d(sin(t),t,12)", "(sin(t),t,12)"},
        {"int(x^2,x,-2,7)", "∫(x^2,x,-2,7)"},
        {"sum(k^2,k,1,12)", "∑(k^2,k,1,12)"},
        {"product(k,k,2,10)", "∏(k,k,2,10)"},
        {"diff(x,x,2,7)", "diff(x,x,2,7)"},
        {"int(x,x,2)", "int(x,x,2)"}, {"sum(x)", "sum(x)"},
        {"product(k,k,1,9,2)", "product(k,k,1,9,2)"},
        {"sqrt(x)", "√(x)"}, {"sum(x,x,1,4)", "∑(x,x,1,4)"},
        {"product(x,x,1,4)", "∏(x,x,1,4)"}, {"pi<=infinity", "π≤∞"},
        {"x>=y", "x≥y"}, {"x~=y", "x≈y"}, {"x==y", "x≡y"}, {"x!=y", "x≠y"},
        {'"int(x)"', '"int(x)"'}, {'"a\\\"int(x)"', '"a\\\"int(x)"'},
        {"interval(x)", "interval(x)"}, {"αint(x)", "αint(x)"}, {"intα(x)", "intα(x)"},
    }) do
        result.display_result = notation[1]
        env.steps.stepScroll = 0
        env.on.paint(gc)
        check(mathBoxExact(notation[2]) ~= nil, "native notation: " .. notation[1])
        check(result.display_result == notation[1], "notation preserves the source: " .. notation[1])
    end
    end)()
end

do
    (function()
    for _, fixture in ipairs({{agrees=true, icon=0}, {agrees=false, icon=1}, {icon=2}}) do
        local module = copyModule()
        local icon_calls = {}
        module.ui_icon = function(_, icon, x, y, width, height)
            icon_calls[#icon_calls + 1] = {icon=icon, x=x, y=y}
            check(x >= 0 and y >= 0 and x + 16 <= width and y + 16 <= height,
                  "trust icon stays in the visible viewport")
            return true
        end
        local env = loadIsolated(module)
        local record = {}
        for key, value in pairs(fake_result) do record[key] = value end
        record.agrees = fixture.agrees
        env.steps.result = record
        env.openSteps()
        drawn = {}
        env.on.paint(gc)
        check(#icon_calls == 1 and icon_calls[1].icon == fixture.icon,
              "trust icon distinguishes agreement, disagreement and unchecked results")
        check(table.concat(drawn, "\n"):find("TRUST", 1, true),
              "native status icons preserve their visible text label")
        module.ui_icon = function() return false end
        drawn = {}
        env.on.paint(gc)
        check(table.concat(drawn, "\n"):find("TRUST", 1, true),
              "a refused native icon keeps the text status usable")
    end
    end)()
end

do
    (function()
    local env = loadIsolated(copyModule())
    local record = {}
    for key, value in pairs(fake_result) do record[key] = value end
    record.result = string.rep("x+", 180) .. "ANSWER_TAIL"
    record.canonical, record.display_result = record.result, record.result
    record.assumptions = string.rep("x > 0 and ", 60) .. "DOMAIN_TAIL"
    env.steps.result = record
    env.openSteps()
    env.readFullText()
    local original_measure = gc.getStringWidth
    local measurements = 0
    gc.getStringWidth = function(self, text)
        measurements = measurements + 1
        return original_measure(self, text)
    end
    env.on.paint(gc)
    local cold = measurements
    for i = 1, 10 do env.on.paint(gc) end
    check(cold > 100 and measurements - cold < 100,
          "unchanged full-text paints reuse wrapped content instead of remeasuring every paragraph")
    local original_width, original_height = platform.window.width, platform.window.height
    for _, width in ipairs({96, 180, 320}) do
        platform.window.width = function() return width end
        platform.window.height = function() return 96 end
        env.resizeGC(gc)
        local seen, fits = {}, true
        for i = 1, 240 do
            drawn, draw_calls = {}, {}
            env.on.paint(gc)
            for _, call in ipairs(draw_calls) do
                if call.y >= env.STEP_LINE and call.y < 96 - env.STEP_LINE then
                    fits = fits and call.x >= 0 and call.x + original_measure(gc, call.text) <= width
                        and call.y + 12 <= 96 - env.STEP_LINE
                    seen[#seen + 1] = call.text
                end
            end
            env.on.arrowDown()
        end
        local complete = table.concat(seen):gsub("%s", "")
        check(fits and complete:find("ANSWER_TAIL", 1, true) and complete:find("DOMAIN_TAIL", 1, true),
              "answers and conditions stay inside the viewport and fully reachable at width " .. width)
        env.on.escapeKey()
        env.readFullText()
    end
    gc.getStringWidth = original_measure
    platform.window.width, platform.window.height = original_width, original_height
    env.on.escapeKey()
    end)()
end

do
    (function()
    local module = copyModule()
    local encodes, decodes, painted_images, fallback = 0, 0, 0, 0
    module.ui_icon_image = function(icon) encodes = encodes + 1 return "RETAINED_ICON_" .. icon end
    module.ui_icon = function() fallback = fallback + 1 return true end
    local original_new, original_draw = image.new, gc.drawImage
    image.new = function(encoded)
        if encoded:sub(1, 14) == "RETAINED_ICON_" then
            decodes = decodes + 1
            return {native_icon = true}
        end
        return original_new(encoded)
    end
    gc.drawImage = function(self, retained, x, y)
        if retained.native_icon then painted_images = painted_images + 1
        else original_draw(self, retained, x, y) end
    end
    local env = loadIsolated(module)
    local record = {}
    for key, value in pairs(fake_result) do record[key] = value end
    record.agrees = true
    env.steps.result = record
    env.openSteps()
    for i = 1, 4 do env.on.paint(gc) end
    check(encodes == 1 and decodes == 1 and painted_images == 4 and fallback == 0,
          "repainting a retained status image does not rebuild or decode its bitmap")
    record.agrees = false
    env.on.paint(gc)
    check(encodes == 2 and decodes == 2 and painted_images == 5,
          "a changed trust state selects its own retained symbol")
    image.new = function(encoded)
        if encoded:sub(1, 14) == "RETAINED_ICON_" then
            decodes = decodes + 1
            error("bitmap decoder refused the symbol")
        end
        return original_new(encoded)
    end
    record.agrees = nil
    for i = 1, 4 do env.on.paint(gc) end
    check(encodes == 3 and decodes == 3 and painted_images == 5 and fallback == 4,
          "a refused bitmap is decoded once and uses the native fallback on every paint")
    image.new, gc.drawImage = original_new, original_draw
    end)()
end

do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local input = env.fctEditor
    env.template("limit(,x,-infinity)", 13)
    check(input.editor.border == 1 and input.editor.borderColor == 0xffffff,
          "editable templates retain the native border while the shell hides its color")
    env.toggleBorders()
    check(input.editor.border == 1 and input.editor.borderColor == 0,
          "showing editor borders changes their color without disabling native layout")
    env.toggleBorders()
    check(input.editor.border == 1 and input.editor.borderColor == 0xffffff,
          "hiding editor borders preserves the native template layout")
    input:addString("x")
    check(input:getExpression() == "limit(x,x,-infinity)",
          "border changes preserve the template and its first editable slot")
    end)()
end

do
    (function()
    local original_new, original_draw = image.new, gc.drawImage
    local original_start, original_stop = timer.start, timer.stop
    local original_width, original_height = platform.window.width, platform.window.height
    local function fixture(failure)
        local module, state = copyModule(), {opens = 0, closes = 0, frames = 0, decodes = 0, paints = 0,
            native = 0, notices = 0, scheduled = 0, stopped = 0, changed = true, failure = failure}
        module.ui_menu_open = function(width, height, title, labels, descriptions)
            state.opens, state.width, state.height = state.opens + 1, width, height
            state.labels, state.title, state.changed = labels, title, true
            state.descriptions = descriptions
            return state.failure ~= "open"
        end
        module.ui_menu_close = function() state.closes = state.closes + 1 end
        module.ui_menu_select = function(index)
            if state.failure == "select" and state.opens > 0 and state.frames > 0 then return false end
            state.selected, state.changed = index, true
            return true
        end
        module.ui_menu_scroll = function(delta)
            state.scroll = delta
            state.selected = state.scroll_selection or state.selected
            if state.failure == "missing selection" then return true end
            if state.failure == "invalid selection" then return true, 25 end
            return state.failure ~= "scroll", state.selected
        end
        module.ui_menu_frame = function()
            state.frames = state.frames + 1
            if state.failure == "frame" then return nil, false end
            local encoded = state.changed and "RETAINED_MENU_FRAME" or nil
            state.changed = false
            return encoded, true
        end
        module.os_menu = function(title, labels)
            state.native = state.native + 1
            check(title == "Templates" and #labels == 20, "native fallback receives the complete template list")
            return state.choice
        end
        module.os_msgbox = function(_, message)
            state.notices, state.notice = state.notices + 1, message
            return 1
        end
        if failure == "missing" then module.ui_menu_open = nil end
        image.new = function(encoded)
            if encoded ~= "RETAINED_MENU_FRAME" then return original_new(encoded) end
            state.decodes = state.decodes + 1
            if state.failure == "decode" then error("menu image refused") end
            return {retained_menu = true}
        end
        gc.drawImage = function(self, frame, x, y)
            if not frame.retained_menu then return original_draw(self, frame, x, y) end
            if state.failure == "draw" then error("menu image paint refused") end
            state.paints = state.paints + 1
            check(x == 0 and y == 0, "retained frame aligns with its viewport")
        end
        timer.start = function() state.scheduled = state.scheduled + 1 end
        timer.stop = function() state.stopped = state.stopped + 1 end
        local env, _, palette = loadIsolated(module)
        env.on.paint(gc)
        env.fctEditor.editor:setExpression("\\0el {}")
        env.fctEditor:fixContent()
        state.editor = env.fctEditor
        state.palette = palette
        state.open = function()
            for _, entry in ipairs(palette[4]) do
                if type(entry) == "table" and entry[1] == "Browse templates" then return entry[2]() end
            end
            error("template browser action missing")
        end
        return env, state
    end
    local filtered_env, filtered_state = fixture()
    filtered_state.editor:addString("x+1")
    filtered_state.open()
    local filter = filtered_state.editor.editor.filter
    check(filter.charIn("hidden") == true and filtered_state.editor:getExpression() == "x+1",
          "retained picker consumes text delivered through a parked native editor")
    check(filter.clearKey() == true and filtered_state.editor:getExpression() == "x+1",
          "retained picker shields existing input from a native Clear callback")
    filter.tabKey()
    check(filtered_state.selected == 2 and not filtered_state.editor.editor.visible,
          "native editor Tab routes to retained selection without restoring hidden focus")
    filter.escapeKey()
    check(filtered_state.editor:getExpression() == "x+1" and filtered_state.editor.editor.visible,
          "native Escape restores the unchanged editor after a retained menu")
    local scrolled_env, scrolled_state = fixture()
    scrolled_state.open()
    scrolled_state.scroll_selection = 20
    scrolled_env.on.arrowRight()
    scrolled_env.on.enterKey()
    scrolled_state.editor:addString("x")
    check(scrolled_state.editor:getExpression() == "limit(x,x,-infinity)",
          "Enter inserts the visible selection returned by retained scrolling")
    local env, state = fixture()
    local solves = calls.giac
    state.open()
    check(state.opens == 1 and #state.labels == 22 and not state.editor.editor.visible,
          "the application opens all templates in a retained viewport and parks its editor")
    check(state.labels[1] == "Fraction" and state.labels[6] == "Indefinite integral" and
          #state.descriptions == #state.labels, "retained templates separate concise names from guidance")
    for index, description in ipairs(state.descriptions) do
        check(type(description) == "string" and #description > 0 and #description <= 256 and
              not description:find("[^ -~]"), "each template has supported readable guidance: " .. state.labels[index])
    end
    check(state.descriptions[17]:find("smaller values", 1, true) and
          state.descriptions[18]:find("larger values", 1, true), "one-sided guidance distinguishes both approach directions")
    -- CALC-010. The two new templates are the last of the calculus block and their guidance has to
    -- keep the linearization an approximation rather than promising an equality.
    check(state.labels[21] == "Tangent line at a point" and state.labels[22] == "Linearization at a point" and
          state.descriptions[22]:find("approximation", 1, true) ~= nil,
          "the tangent templates are offered and the linearization says it approximates")
    for i = 1, 4 do env.on.paint(gc) end
    check(state.decodes == 1 and state.paints == 4, "unchanged menu frames reuse the decoded image")
    env.on.charIn("hidden")
    check(state.editor:getExpression() == "", "typing in the menu cannot change its hidden editor")
    env.on.arrowUp()
    check(state.selected == 22, "up from the first template reaches the final template")
    env.on.tabKey()
    check(state.selected == 1, "Tab wraps the retained selection")
    env.on.arrowRight()
    check(state.scroll == -48, "Right scrolls down within an oversized template label")
    env.on.arrowDown()
    platform.window.width, platform.window.height = function() return 180 end, function() return 120 end
    env.on.paint(gc)
    check(state.opens == 2 and state.selected == 2 and state.width == 180 and state.height == 120,
          "resizing rebuilds the retained viewport while preserving selection")
    env.on.arrowUp()
    env.on.enterKey()
    state.editor:addString("7")
    check(state.editor:getExpression() == "(7)/()" and state.editor.editor.visible,
          "choosing a retained template restores the editor and its editable first slot")
    state.open()
    env.on.escapeKey()
    check(state.editor:getExpression() == "(7)/()" and state.editor.editor.visible and calls.giac == solves,
          "dismissing the picker preserves input without invoking the solver")
    state.open()
    env.steps.result = fake_result
    env.openSteps()
    check(state.closes >= 3 and env.steps.active, "opening a walkthrough closes the retained picker")
    local opens = state.opens
    state.open()
    check(state.opens == opens, "template picker cannot cover an active walkthrough")
    env.closeSteps()
    state.open()
    local before_read = state.closes
    env.readFullText()
    check(state.closes == before_read + 1, "Full Text closes the retained picker before taking focus")
    env.on.escapeKey()
    platform.window.width, platform.window.height = original_width, original_height
    for _, failure in ipairs({"frame", "decode", "draw", "select", "scroll", "missing selection", "invalid selection"}) do
        env, state = fixture(failure)
        state.editor:addString("x+1")
        state.open()
        local painted = pcall(env.on.paint, gc)
        if failure == "select" then env.on.arrowDown() end
        if failure == "scroll" or failure == "missing selection" or failure == "invalid selection" then
            env.on.arrowRight()
        end
        check(painted and state.native == 0 and state.scheduled == 1,
              failure .. " failure defers recovery until after painting")
        local frames, decodes = state.frames, state.decodes
        pcall(env.on.paint, gc)
        check(state.frames == frames and state.decodes == decodes,
              failure .. " failure prevents further retained render and decode calls")
        env.on.timer()
        check(state.native == 0 and state.notices == 1 and state.notice:find("Menu, then Templates", 1, true),
              failure .. " recovery uses the managed TI tool palette without a polling menu")
        check(state.editor.editor.visible and state.editor:getExpression() == "x+1",
              failure .. " recovery restores unchanged input before showing guidance")
        state.palette[2][2][2]()
        state.editor:addString("9")
        check(state.editor:getExpression() == "x+1(9)/()" and state.editor.editor.visible,
              failure .. " recovery leaves native template insertion usable")
        env.on.timer()
        state.open()
        check(state.native == 0 and state.notices == 2 and state.opens == 1,
              failure .. " recovery never retries the renderer or enters a polling menu")
    end
    for _, failure in ipairs({"missing", "open"}) do
        env, state = fixture(failure)
        state.editor:addString("x+1")
        state.open()
        check(state.native == 0 and state.notices == 1 and state.editor:getExpression() == "x+1" and
                  state.editor.editor.visible,
              failure .. " retained renderer restores input and explains the native menu immediately")
        state.palette[2][2][2]()
        check(state.editor:getExpression() == "x+1()/()", failure .. " recovery preserves native templates")
    end
    env, state = fixture("missing")
    env.nps_nspire.os_msgbox = nil
    state.editor:addString("x+1")
    state.open()
    check(state.native == 0 and env.steps.status == "Menu, then Templates" and
              state.editor.editor.visible and state.editor:getExpression() == "x+1",
          "unavailable native dialogs retain visible recovery guidance and existing input")
    image.new, gc.drawImage = original_new, original_draw
    timer.start, timer.stop = original_start, original_stop
    platform.window.width, platform.window.height = original_width, original_height
    end)()
end

assert((function()
    local env = loadIsolated(copyModule())
    local original = {}
    for index, category in ipairs(env.menu) do original[index] = category end
    local probe = assert(loadfile("tests/target/retained_failure.lua"))
    setfenv(probe, env)
    check(pcall(probe), "failure diagnostic composes with the actual V4 document")
    local preserved = #env.menu == #original + 1
    for index, category in ipairs(original) do preserved = preserved and env.menu[index] == category end
    check(preserved and env.menu[2][1] == "Templates",
          "failure diagnostic preserves every product menu index and the actual template inventory")
    check(env.menu[#env.menu][1] == "Failure probe", "failure controls append to the product menus")
    return true
end)())

do
    (function()
    local module, requests = copyModule(), 0
    module.walkthrough = function() requests = requests + 1 return nil end
    local env = loadIsolated(module)
    local record = {}
    for key, value in pairs(fake_result) do record[key] = value end
    record.input = string.rep("input term ", 100) .. "INPUT_TAIL"
    record.display_result = string.rep("x+", 180) .. "ANSWER_TAIL"
    record.assumptions = string.rep("x > 0 and ", 80) .. "DOMAIN_TAIL"
    env.steps.result, env.steps.view = record, "result"
    env.openSteps()
    local original_measure, measurements = gc.getStringWidth, 0
    gc.getStringWidth = function(self, text)
        measurements = measurements + 1
        return original_measure(self, text)
    end
    env.on.paint(gc)
    local cold = measurements
    for i = 1, 10 do env.on.paint(gc) end
    local warm = measurements - cold
    print("detail width measurements: cold=" .. cold .. ", ten unchanged=" .. warm)
    check(cold > 500 and warm < cold * 2,
          "unchanged detail paints avoid at least eighty percent of repeated width measurements")
    local seen = {}
    for i = 1, 240 do
        drawn = {}
        env.on.paint(gc)
        seen[#seen + 1] = table.concat(drawn)
        env.on.arrowDown()
    end
    local complete = table.concat(seen):gsub("%s", "")
    check(complete:find("INPUT_TAIL", 1, true) and complete:find("ANSWER_TAIL", 1, true)
          and complete:find("DOMAIN_TAIL", 1, true), "cached detail scrolling reaches every content tail")
    record.assumptions = "CHANGED_CONDITION"
    env.steps.stepScroll = 0
    local before = measurements
    env.on.paint(gc)
    check(measurements - before > cold / 2, "changing detail content rebuilds its measured layout")
    seen = {}
    for i = 1, 240 do
        drawn = {}
        env.on.paint(gc)
        seen[#seen + 1] = table.concat(drawn)
        env.on.arrowDown()
    end
    complete = table.concat(seen):gsub("%s", "")
    check(complete:find("CHANGED_CONDITION", 1, true) and not complete:find("DOMAIN_TAIL", 1, true),
          "changed conditions replace every cached line of the previous condition")
    record.input = string.rep(" ", 40000) .. "INPUT_TAIL"
    env.on.paint(gc)
    check(env.steps.detailLayout == nil, "detail retention bounds original text as well as wrapped lines")
    local original_width = platform.window.width
    platform.window.width = function() return 96 end
    record.input = string.rep("a ", 6000)
    env.on.paint(gc)
    check(env.steps.detailLayout == nil, "detail retention bounds line objects even for compact text")
    platform.window.width = original_width
    record.input = "short input"
    env.on.paint(gc)
    check(env.steps.detailLayout ~= nil, "a bounded detail can be retained after an oversized document")
    check(requests == 0, "detail layout and scrolling never recompute a solve")
    gc.getStringWidth = original_measure
    env.closeSteps()
    check(env.steps.detailLayout == nil, "closing the viewer releases its retained detail")
    end)()
end

do
    (function()
    local create, original_height = D2Editor.newRichText, platform.window.height
    D2Editor.newRichText = function()
        local editor = create()
        local set_font = editor.setFontSize
        editor.setFontSize = function(self, size)
            self.mathHeight = size * 7
            set_font(self, size)
        end
        return editor
    end
    local env = loadIsolated(copyModule())
    local record = {}
    for key, value in pairs(fake_result) do record[key] = value end
    record.input, record.display_result = "x", "1/2"
    env.steps.result, env.steps.view = record, "result"
    env.openSteps()
    local first = #editors + 1
    platform.window.height = function() return 96 end
    env.on.paint(gc)
    local answer
    for index = first, #editors do
        if editors[index].expr == "\\0el {1/2}" then answer = editors[index] end
    end
    check(answer and answer.font == 7, "a short viewport selects a fitting native math font")
    platform.window.height = function() return 240 end
    env.on.paint(gc)
    check(answer and answer.font == 11, "a taller viewport restores the preferred native math font")
    platform.window.height, D2Editor.newRichText = original_height, create
    env.closeSteps()
    end)()
end

do
    (function()
    local env = loadIsolated(copyModule())
    env.steps.result = {steps={}}
    env.openSteps()
    drawn = {}
    env.on.paint(gc)
    check(not table.concat(drawn):find("steps paint failed", 1, true),
          "an incomplete result preserves first-use expression normalization during painting")
    local opened = pcall(env.readFullText)
    check(opened, "an incomplete result remains inspectable in Full Text without an outcome")
    if opened then
        drawn = {}
        env.on.paint(gc)
        check(table.concat(drawn):find("unavailable", 1, true),
              "Full Text identifies the missing result status as unavailable")
    end
    env.closeSteps()
    end)()
end

do
    (function()
    local module, formatted, submitted, owned = copyModule(), {}, {}, {}
    local create = D2Editor.newRichText
    D2Editor.newRichText = function()
        local editor = create()
        owned[#owned + 1] = editor
        local setExpression = editor.setExpression
        editor.setExpression = function(self, expression, position)
            if self.readOnly then submitted[#submitted + 1] = expression end
            return setExpression(self, expression, position)
        end
        return editor
    end
    module.math_display = function(expression)
        formatted[#formatted + 1] = expression
        if expression == "expanded^-1" then return string.rep("x+", 2048) .. "FORMAT_TAIL" end
        return expression
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    local record = {}
    for key, value in pairs(fake_result) do record[key] = value end
    record.input, record.display_result, record.assumptions = "bounded preview", "1/2", "DOMAIN_TAIL"
    env.steps.result, env.steps.view = record, "result"
    env.openSteps()
    env.on.paint(gc)
    local initial_submissions = #submitted
    record.display_result = string.rep("x^-1+", 820) .. "ANSWER_TAIL"
    local original_width = platform.window.width
    for _, width in ipairs({96, 320}) do
        platform.window.width = function() return width end
        env.steps.stepScroll = 0
        local seen = {}
        for _ = 1, 380 do
            drawn = {}
            env.on.paint(gc)
            seen[#seen + 1] = table.concat(drawn)
            env.on.arrowDown()
        end
        local visible = table.concat(seen):gsub("%s", "")
        check(visible:find("ANSWER_TAIL", 1, true) and visible:find("DOMAIN_TAIL", 1, true),
              "oversized preview retains answer and condition tails at width " .. width)
    end
    check(#submitted == initial_submissions and #formatted == 0,
          "oversized previews bypass native formatting and typesetting at every viewport")
    local stale = false
    for _, editor in ipairs(owned) do
        stale = stale or (editor.readOnly and editor.visible and editor.x and editor.x >= 0)
    end
    check(not stale, "a rejected preview never leaves the previous native answer visible")
    platform.window.width = original_width
    record.display_result, env.steps.stepScroll = string.rep("x", 2048), 0
    env.on.paint(gc)
    check(#submitted > initial_submissions, "a preview at the byte budget still reaches native measurement")
    initial_submissions = #submitted
    record.display_result = string.rep("x", 2049)
    env.on.paint(gc)
    check(#submitted == initial_submissions, "a preview one byte over budget bypasses native measurement")
    record.display_result, env.steps.stepScroll = "expanded^-1", 0
    for _ = 1, 10 do env.on.paint(gc) end
    check(#submitted == initial_submissions and #formatted == 1,
          "an oversized formatted expansion is remembered and never reaches the native typesetter")
    record.display_result, env.steps.stepScroll = "2/3", 0
    env.on.paint(gc)
    check(#submitted > initial_submissions and mathBoxExact("2/3") ~= nil,
          "short mathematics resumes native display after an oversized preview")
    env.closeSteps()
    D2Editor.newRichText = create
    end)()
end

assert((function()
    local env = loadIsolated(copyModule())
    local original = {}
    for index, category in ipairs(env.menu) do original[index] = category end
    local probe = assert(loadfile("tests/target/ui_preview_device.lua"))
    setfenv(probe, env)
    check(pcall(probe), "preview diagnostic composes with the actual V4 document")
    local preserved = #env.menu == #original + 1
    for index, category in ipairs(original) do preserved = preserved and env.menu[index] == category end
    check(preserved, "preview diagnostic preserves product menus")
    local controls = env.menu[#env.menu]
    controls[3][2]()
    check(#env.steps.result.display_result > 2048 and env.steps.view == "result",
          "device fixture exceeds native preview admission in the actual result viewer")
    check(not env.steps.result.solved and not env.steps.result.has_result and
          env.steps.result.status == "rendering fixture",
          "the rendering fixture makes no mathematical verification claim")
    local seen = {}
    for _ = 1, 240 do
        drawn = {}
        env.on.paint(gc)
        seen[#seen + 1] = table.concat(drawn)
        env.on.arrowDown()
    end
    local visible = table.concat(seen):gsub("%s", "")
    check(visible:find("FINAL_TERM", 1, true) and visible:find("CONDITION_END", 1, true),
          "device fixture exposes its expression and condition tails through ordinary scrolling")
    controls[2][2]()
    env.on.paint(gc)
    check(env.steps.stepScroll == 0 and env.steps.result.display_result == "1/2",
          "short device fixture resets scrolling after oversized content")
    env.closeSteps()
    return true
end)())

do
    (function()
    local module, requests = copyModule(), 0
    module.walkthrough = function() requests = requests + 1 return nil end
    local env = loadIsolated(module)
    local record = {}
    for key, value in pairs(fake_result) do record[key] = value end
    record.input = string.rep("input term ", 160) .. "INPUT_END"
    record.display_result = string.rep("x+", 1200) .. "ANSWER_END"
    record.assumptions = string.rep("x > 0 and ", 120) .. "DOMAIN_END"
    env.steps.result, env.steps.view = record, "list"
    env.openSteps()
    local original_measure, measurements, scale = gc.getStringWidth, 0, 1
    gc.getStringWidth = function(self, text)
        measurements = measurements + 1
        return original_measure(self, text) * scale
    end
    draw_calls = {}
    env.on.paint(gc)
    local cold, baseline = measurements, draw_calls
    for _ = 1, 10 do
        draw_calls = {}
        env.on.paint(gc)
        local same = #draw_calls == #baseline
        for index, call in ipairs(draw_calls) do
            local before = baseline[index]
            same = same and before and call.text == before.text and call.x == before.x and call.y == before.y
        end
        check(same, "cached summaries preserve every visible label and its position")
    end
    local warm = measurements - cold
    print("summary width measurements: cold=" .. cold .. ", ten unchanged=" .. warm)
    check(cold > 1000 and warm < cold * 2, "unchanged summary paints avoid eighty percent of repeated measurements")
    local original_width = platform.window.width
    platform.window.width = function() return 180 end
    local before_resize = measurements
    env.resizeGC(gc)
    env.on.paint(gc)
    check(measurements - before_resize > cold / 2, "summary cache invalidates for a narrower viewport")
    platform.window.width = original_width
    env.resizeGC(gc)
    env.on.paint(gc)
    env.readFullText()
    local seen = {}
    for _ = 1, 100 do
        drawn = {}
        env.on.paint(gc)
        seen[#seen + 1] = table.concat(drawn)
        env.on.tabKey()
    end
    local complete = table.concat(seen):gsub("%s", "")
    check(complete:find("INPUT_END", 1, true) and complete:find("ANSWER_END", 1, true)
          and complete:find("DOMAIN_END", 1, true), "summary retention preserves complete reader access")
    env.on.escapeKey()
    local before = measurements
    scale = 1.5
    env.on.paint(gc)
    check(measurements - before > cold / 2, "summary cache invalidates when font widths change")
    scale = 1
    env.on.paint(gc)
    record.assumptions = "CHANGED_DOMAIN"
    draw_calls = {}
    env.on.paint(gc)
    check(env.steps.summaryLayout and env.steps.summaryLayout.entries.assumptions.source == "CHANGED_DOMAIN",
          "changed summary conditions replace their retained lines")
    record.input = string.rep("x ", 20000)
    env.on.paint(gc)
    local layout = env.steps.summaryLayout
    check(layout and not layout.entries.input and layout.bytes <= 32768 and layout.lines <= 512,
          "oversized input cannot exceed the summary retention budget")
    record.input = string.rep(" ", 33000)
    env.on.paint(gc)
    check(not env.steps.summaryLayout.entries.input,
          "summary byte retention is bounded even when whitespace produces no lines")
    platform.window.width = function() return 96 end
    env.resizeGC(gc)
    record.input = string.rep("long_input_term ", 600)
    env.on.paint(gc)
    check(not env.steps.summaryLayout.entries.input,
          "summary line retention is bounded independently of source bytes")
    platform.window.width = original_width
    env.resizeGC(gc)
    record.input = "short input"
    env.on.paint(gc)
    local original_height = gc.getStringHeight
    local previous = env.steps.summaryLayout.entries.input
    gc.getStringHeight = function(self, text) return original_height(self, text) * 1.5 end
    env.on.paint(gc)
    check(env.steps.summaryLayout.entries.input ~= previous,
          "summary cache invalidates when only font height changes")
    gc.getStringHeight = original_height
    gc.getStringWidth = original_measure
    check(requests == 0, "summary painting does no solver work")
    env.closeSteps()
    check(env.steps.summaryLayout == nil, "closing the viewer releases retained summary lines")
    end)()
end

do
    (function()
    local create, original_width = D2Editor.newRichText, platform.window.width
    local owned, submissions, formatted, requests, deferred = {}, 0, 0, 0, false
    D2Editor.newRichText = function()
        local editor = create()
        owned[#owned + 1] = editor
        local announce, set = editor.announceSize, editor.setExpression
        editor.announceSize = function(self)
            if deferred and self.readOnly then return end
            if self.readOnly and self.onSize and self.expr:find("lim(", 1, true) then
                self.natural, self.needH, self.measuredAt = 108, 40, self.boxw
                self.onSize(self, self.natural, self.needH)
            else announce(self) end
        end
        editor.setExpression = function(self, expression, position)
            if self.readOnly then submissions = submissions + 1 end
            return set(self, expression, position)
        end
        return editor
    end
    local command = "limit((3*x^2+1)/(2*x^2-5),x,infinity)"
    local normalized = "limit(((3*(x^2)+1)*((2*(x^2)-5)^(-1))),x,infinity)"
    local record = { outcome = "limit evaluated", mode = "limit", status = "solved and verified", solved = true,
        original_expression = command, normalized_expression = command, request_expression = command,
        result = "3/2", agrees = true,
        step_count = 12, steps = {} }
    for index = 1, 12 do
        record.steps[index] = { kind = "transformation", phase = "solve", name = "Limit rule " .. index,
            goal = "Simplify the expression", short = "Compare the leading terms", action = "Divide leading coefficients",
            before = normalized, after = "(" .. index .. " * (2^(-1)))", depth = 0, verified = true, failed = false }
    end
    local module = copyModule()
    module.walkthrough = function() requests = requests + 1 return record end
    local format = function(expression)
        if expression == normalized then return command end
        return expression:gsub("%((%d+) %* %(2%^%(%-1%)%)%)", "%1/2")
    end
    if os.getenv("NPS_COMMAND_MODULE") then
        local fixture, loaded = nps_split, package.loaded.nps_split
        nps_split, package.loaded.nps_split = nil, nil
        local native = assert(package.loadlib(os.getenv("NPS_COMMAND_MODULE"), "luaopen_nps_split"))()
        nps_split, package.loaded.nps_split = fixture, loaded
        format = function(expression) return native.math_display(expression) or expression end
    end
    module.math_display = function(expression)
        formatted = formatted + 1
        return format(expression)
    end
    local expected = format(command):gsub("^limit", "lim"):gsub("infinity", "∞")
    check(format(normalized):find("/", 1, true) and not format(normalized):find("^(-1)", 1, true),
          "the working expression formatter produces a fraction from the recorded reciprocal power")
    local env = loadIsolated(module)
    env.on.paint(gc)
    local function enter()
        env.fctEditor.editor:setExpression("\\0el {" .. command .. "}")
        env.on.enterKey()
    end
    for _, width in ipairs({320, 180}) do
        platform.window.width = function() return width end
        env.resizeGC(gc)
        enter()
        env.on.paint(gc)
        local input
        for _, editor in ipairs(owned) do
            if editor.readOnly and editor.visible and editor.x and editor.x >= 0 and mathBoxFits(editor)
                and editor.y < env.steps.headerHeight and editor.expr == "\\0el {" .. expected .. "}" then input = editor end
        end
        check(input,
              "the first list paint typesets the full input limit at width " .. width)
        local first_work = false
        for _, editor in ipairs(owned) do
            first_work = first_work or (editor.visible and editor.readOnly and editor.x and editor.x >= 0
                and editor.y >= env.steps.headerHeight and mathBoxFits(editor) and editor.expr == "\\0el {" .. expected .. "}")
        end
        check(first_work, "the first working limit is a native stacked fraction at width " .. width)
        local seen_work, fits, bands_fit = false, true, true
        for index = 1, 12 do
            env.on.paint(gc)
            local previous = env.steps.headerHeight
            for _, band in ipairs(env.steps.bands or {}) do
                bands_fit = bands_fit and band.top >= previous and band.bottom > band.top
                    and band.bottom <= platform.window:height() - env.STEP_LINE
                previous = band.bottom
            end
            local selected = false
            for _, band in ipairs(env.steps.bands or {}) do selected = selected or band.step == index end
            check(selected, "variable-height rows keep the selected step visible: " .. width .. "/" .. index)
            for _, editor in ipairs(owned) do
                if editor.visible and editor.readOnly and editor.x and editor.x >= 0 then
                    fits = fits and mathBoxFits(editor) and editor.x + editor.boxw <= width
                        and editor.y + editor.boxh <= platform.window:height() - env.STEP_LINE
                    if editor.y >= env.steps.headerHeight then
                        seen_work = seen_work or editor.expr == "\\0el {" .. format(record.steps[index].after) .. "}"
                        local contained = false
                        for _, band in ipairs(env.steps.bands or {}) do
                            contained = contained or (editor.y >= band.top and editor.y + editor.boxh <= band.bottom)
                        end
                        fits = fits and contained
                    end
                end
            end
            if index < 12 then env.on.arrowDown() end
        end
        check(seen_work and fits and bands_fit,
              "native fraction working and pointer bands fit without overlapping the footer at width " .. width)
        local before_format, before_submit, before_requests = formatted, submissions, requests
        for _ = 1, 10 do env.on.paint(gc) end
        check(formatted == before_format and submissions == before_submit and requests == before_requests,
              "unchanged mathematical lists reuse native formatting and measurements at width " .. width)
        local created = #owned
        for _ = 1, 12 do env.on.arrowUp() env.on.paint(gc) end
        for _ = 1, 12 do env.on.arrowDown() env.on.paint(gc) end
        check(#owned == created, "revisiting long mathematical lists reuses the bounded editor pool")
        local band = env.steps.bands[#env.steps.bands]
        if band then
            env.on.mouseUp(20, (band.top + band.bottom) / 2)
            check(env.steps.focus == band.step, "a variable-height row tap selects the rendered step")
        end
        env.closeSteps()
    end
    platform.window.width = original_width
    env.resizeGC(gc)
    enter()
    for _, expression in ipairs({"limit(x,x,0)", "limit(x,x,0,-1)"}) do
        record.input = expression
        env.on.paint(gc)
        check(mathBoxExact(expression:gsub("limit", "lim")), "valid limit arity uses the native limit template")
    end
    for _, expression in ipairs({"limit(x,x)", "limit(x,x,0,1,2)", "limit(x,,0)"}) do
        record.input = expression
        env.on.paint(gc)
        check(mathBoxExact(expression), "unsupported limit signatures preserve every original argument")
    end
    check(record.steps[1].after == "(1 * (2^(-1)))" and record.original_expression == command,
          "native working notation leaves the mathematical record unchanged")
    env.closeSteps()
    deferred = true
    record.steps[1].after = "13/17"
    enter()
    record.input = "limit(z,z,0)"
    drawn = {}
    env.on.paint(gc)
    check(not mathBoxExact("lim(z,z,0)") and not mathBoxExact("13/17") and
          table.concat(drawn):find("13/17", 1, true) and not table.concat(drawn):find("paint failed", 1, true),
          "pending native input and working measurements retain a useful text fallback")
    deferred = false
    for _, editor in ipairs(owned) do editor:announceSize() end
    env.on.paint(gc)
    check(mathBoxExact("lim(z,z,0)") and mathBoxExact("13/17"),
          "delayed native measurements restore complete input and working notation")
    env.closeSteps()
    record.steps[1].after = string.rep("x+", 1100) .. "WORK_TAIL"
    enter()
    drawn = {}
    env.on.paint(gc)
    local oversized = false
    for _, editor in ipairs(owned) do
        oversized = oversized or (editor.readOnly and #editor.expr > 2055)
    end
    check(not oversized and table.concat(drawn):find("T text", 1, true),
          "oversized working rows bypass native editors and retain the text reader control")
    env.readFullText()
    local complete = {}
    for _ = 1, 120 do
        drawn = {}
        env.on.paint(gc)
        complete[#complete + 1] = table.concat(drawn)
        env.on.tabKey()
    end
    check(table.concat(complete):gsub("%s", ""):find("WORK_TAIL", 1, true),
          "an oversized working expression remains complete in the full text reader")
    env.on.escapeKey()
    env.closeSteps()
    record.steps[1].after, record.steps[12].after, record.result = "(1 * (2^(-1)))", "SECRET_FINAL", "SECRET_FINAL"
    env.stepsSetProgression("hint")
    enter()
    env.on.paint(gc)
    local leaked = false
    for _, editor in ipairs(owned) do
        leaked = leaked or (editor.visible and editor.readOnly and editor.x and editor.x >= 0
            and editor.expr:find("SECRET_FINAL", 1, true) ~= nil)
    end
    env.readFullText()
    drawn = {}
    for _ = 1, 80 do env.on.paint(gc) env.on.arrowDown() end
    check(not leaked and not table.concat(drawn):find("SECRET_FINAL", 1, true),
          "native list previews and the full text reader withhold unrevealed final work")
    env.on.escapeKey()
    local before_requests = requests
    for _ = 1, 11 do env.on.tabKey() env.on.paint(gc) end
    check(mathBoxExact("SECRET_FINAL") and requests == before_requests,
          "the final hint restores its native answer without recomputing the walkthrough")
    env.closeSteps()
    D2Editor.newRichText = create
    end)()
end

do
    (function()
    local create, width, height = D2Editor.newRichText, platform.window.width, platform.window.height
    local owned = {}
    D2Editor.newRichText = function()
        local editor = create()
        owned[#owned + 1] = editor
        local announce = editor.announceSize
        editor.announceSize = function(self)
            local calculus = self.expr:find("lim(", 1, true) or self.expr:find("∫(", 1, true)
            if self.readOnly and self.onSize and (calculus or self.expr:find("/", 1, true)) then
                self.natural = calculus and 118 or 24
                self.needH = math.floor((calculus and 70 or 48) * self.font / 9)
                self.measuredAt = self.boxw
                self.onSize(self, self.natural, self.needH)
            else announce(self) end
        end
        return editor
    end
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local record = { outcome = "limit evaluated", mode = "limit", status = "solved and verified",
        input = "limit((3*x^2+1)/(2*x^2-5),x,infinity)", result = "3/2", agrees = true,
        solved = true, step_count = 2, steps = {
            { kind = "transformation", phase = "solve", name = "Leading terms", goal = "Compare coefficients",
              before = "limit((3*x^2+1)/(2*x^2-5),x,infinity)", after = "3/2", depth = 0, verified = true },
            { kind = "check", phase = "check", name = "Independent check", goal = "Confirm the answer",
              before = "3/2", after = "3/2", depth = 0, verified = true },
        } }
    for _, problem in ipairs({{record.input, "3/2"}, {"int(x^2,x,0,1)", "1/3"}}) do
    record.input, record.result = problem[1], problem[2]
    record.steps[1].before, record.steps[1].after = problem[1], problem[2]
    record.steps[2].before, record.steps[2].after = problem[2], problem[2]
    env.steps.result, env.steps.view, env.steps.detail, env.steps.scroll = record, "list", 2, 0
    env.stepsToggleDetail()
    env.openSteps()
    platform.window.height = function() return 212 end
    for _, viewport in ipairs({320, 180}) do
        platform.window.width = function() return viewport end
        env.resizeGC(gc)
        for index = 1, 2 do
            env.steps.focus = index
            drawn, draw_calls = {}, {}
            env.on.paint(gc)
            local heading = false
            for _, call in ipairs(draw_calls) do
                heading = heading or (call.y >= env.steps.headerHeight and
                    call.text:find(index == 1 and "Leading terms" or "Independent", 1, true) ~= nil)
            end
            check(heading, "a physically tall selected step keeps its heading visible: " .. viewport .. "/" .. index)
            check(#env.steps.bands > 1 and env.steps.bands[1].step == index,
                  "the selected heading begins the viewport with useful working below it")
            local native_work = false
            for _, editor in ipairs(owned) do
                if editor.readOnly and editor.visible and editor.x and editor.x >= 0 then
                    check(mathBoxFits(editor) and editor.font >= 7 and editor.x + editor.boxw <= viewport and
                          editor.y + editor.boxh <= 212 - env.STEP_LINE, "tall native boxes stay inside the viewport")
                    native_work = native_work or editor.y >= env.steps.headerHeight
                    for _, other in ipairs(owned) do
                        if other ~= editor and other.readOnly and other.visible and other.x and other.x >= 0 then
                            check(editor.x + editor.boxw <= other.x or other.x + other.boxw <= editor.x or
                                  editor.y + editor.boxh <= other.y or other.y + other.boxh <= editor.y,
                                  "paired summary boxes and mathematical working never overlap")
                        end
                    end
                end
            end
            if viewport == 320 then check(native_work, "the handheld-sized viewport retains native working below its heading") end
            if index == 1 then check(env.steps.listOverflow, "a partial tall step records its remaining work") end
            if env.steps.listOverflow then
                check(table.concat(drawn):find("ENTER", 1, true), "partial step previews identify how to read the remaining work")
            end
        end
    end
    env.steps.focus, env.steps.view = 1, "step"
    env.steps.stepScroll = 0
    local reached = false
    for _ = 1, 80 do
        env.on.paint(gc)
        for _, editor in ipairs(owned) do
            reached = reached or (editor.readOnly and editor.visible and editor.x and editor.x >= 0 and
                editor.y >= env.steps.headerHeight and editor.expr == "\\0el {" .. problem[2] .. "}")
        end
        env.on.arrowDown()
    end
    check(reached, "all working omitted from a tall list preview remains reachable in details")
    env.closeSteps()
    end
    platform.window.width, platform.window.height, D2Editor.newRichText = width, height, create
    end)()
end

-- A check that disagreed and a backend that never answered both used to read as the check that passed.
do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    next_step_result = {
        outcome = "integrated", detail = "", solved = true, has_result = true, answer_only = false,
        status = "solved and verified", result = "ln(x)", canonical = "ln(x)",
        result_form = "elementary closed form", giac_tag = "exact", giac_raw = "ln(x)",
        nodes = 4, step_count = 1, rewrites = 1, giac_calls = 1,
        steps = { { kind = "transformation", phase = "solve", name = "Logarithmic integral",
                    goal = "Integrate the term", short = "log rule", claim = "equivalent expression",
                    verified = true, failed = false, depth = 0, before = "(x^-1)", after = "ln(x)" } },
    }
    env.runSteps("integrate", "int(1/x,x)")
    for _, case in ipairs({
        { "solved and verified", "EXACT", true, { "UNCHECKED", "CHECK FAILED" } },
        { "verification failed", "EXACT + CHECK FAILED", false, { "UNCHECKED" } },
        { "resource limit reached", "EXACT + UNCHECKED", nil, { "CHECK FAILED" } },
        { "dependency unavailable", "EXACT + UNCHECKED", nil, { "CHECK FAILED" } },
        { "solved but unchecked", "EXACT + UNCHECKED", nil, { "CHECK FAILED" } },
        { "partially solved", "EXACT + UNCHECKED", nil, { "CHECK FAILED" } },
        { "conditionally solved", "EXACT + CONDITIONAL", true, { "UNCHECKED", "CHECK FAILED" } },
        -- agrees true on purpose. That is what a bridge cross-check leaves behind, and it is the
        -- arm that would have let this status fall through the last branch reading as verified.
        { "solved and corroborated", "EXACT + CORROBORATED", true, { "UNCHECKED", "CHECK FAILED" } },
    }) do
        env.steps.result.status, env.steps.result.agrees = case[1], case[3]
        for _ = 1, 3 do env.on.paint(gc) end
        drawn, draw_calls = {}, {}
        env.on.paint(gc)
        local text = table.concat(drawn, " ")
        check(text:find(case[2], 1, true) ~= nil,
              "the class word reads the check the record recorded: " .. case[1] .. " wants " .. case[2])
        check(text:find(case[1], 1, true) ~= nil,
              "and the status it came from stays beside it: " .. case[1])
        local wrong = nil
        for _, word in ipairs(case[4]) do
            if text:find(word, 1, true) then wrong = word end
        end
        check(wrong == nil,
              "and does not also read as another verdict: " .. case[1] .. " also said " .. tostring(wrong))
    end
    env.closeSteps()
    end)()
end

-- The chain behind the ANSWER box reached for the outcome, so four terminal states looked like answers.
do
    (function()
    local create = D2Editor.newRichText
    local owned = {}
    D2Editor.newRichText = function()
        local editor = create()
        owned[#owned + 1] = editor
        return editor
    end
    local module = copyModule()
    local terminal = { outcome = "", status = "", detail = "" }
    -- The shape lua_module.cc gives a typed failure: no result, no canonical, no steps, no context.
    module.walkthrough = function(text)
        return { outcome = terminal.outcome, detail = terminal.detail, solved = false,
                 answer_only = false, status = terminal.status, nodes = 0, step_count = 0,
                 rewrites = 0, giac_calls = 0, steps = {}, mode = "integrate",
                 request_expression = text }
    end
    module.unit_conversion = function()
        return { outcome = "resource exceeded", detail = "conversion budget exhausted", solved = false,
                 answer_only = false, status = "resource limit reached", nodes = 0, step_count = 0,
                 rewrites = 0, giac_calls = 0, steps = {} }
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    -- Joined with a space rather than a newline, because the trust line wraps across draw calls.
    local function frame()
        for _ = 1, 3 do env.on.paint(gc) end
        drawn, draw_calls = {}, {}
        env.on.paint(gc)
        return table.concat(drawn, " ")
    end
    local function boxHolding(text)
        for _, editor in ipairs(owned) do
            if editor.readOnly and editor.visible and (editor.x or -99999) > -1000
               and editor.expr and editor.expr:find(text, 1, true) then return true end
        end
        return false
    end
    for _, case in ipairs({
        { "resource exceeded", "resource limit reached", "the interval budget is gone" },
        { "unsupported form", "unsupported", "no rule for this form" },
        { "invalid input", "invalid input", "unbalanced bracket" },
        { "cancelled", "cancelled", "the student pressed escape" },
    }) do
        terminal.outcome, terminal.status, terminal.detail = case[1], case[2], case[3]
        env.fctEditor.editor:setExpression("\\0el {int(1/x,x)}")
        env.on.enterKey()
        local text = frame()
        check(text:find("ANSWER", 1, true) == nil and not boxHolding(case[1]),
              "a record that produced nothing draws no answer: " .. case[1])
        check(text:find(case[2], 1, true) ~= nil and text:find(case[3], 1, true) ~= nil,
              "and the trust line names the state it did end in: " .. case[2])
        check(env.steps.histText[#env.steps.histText][2]:find(case[1], 1, true) ~= nil,
              "and the history still records the outcome rather than losing it: " .. case[1])
        env.on.charIn("t")
        check(table.concat(drawn, "\n"):find("Answer: ", 1, true) == nil,
              "and the full text offers no answer line either: " .. case[1])
        env.on.escapeKey()
        env.closeSteps()
    end
    env.openPhysicsFixtures()
    env.physicsBrowser.focus = 1
    env.on.enterKey()
    local guided = frame()
    check(rawget(env.steps.result, "display_result") == nil,
          "a guided fixture whose engine refused stamps no answer onto the record")
    check(guided:find("ANSWER", 1, true) == nil and not boxHolding("resource exceeded"),
          "so the guided answer box stays empty rather than showing the refusal")
    check(env.steps.histText[#env.steps.histText][2]:find("resource exceeded", 1, true) ~= nil,
          "while the guided history keeps the refusal, which is what fixture seven exists to show")
    env.closeSteps()
    D2Editor.newRichText = create
    end)()
end

-- A caught paint leaves its math boxes on screen, and the message about the failure cannot cover them.
do
    (function()
    local create = D2Editor.newRichText
    local owned = {}
    D2Editor.newRichText = function()
        local editor = create()
        owned[#owned + 1] = editor
        return editor
    end
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local function placed()
        local count = 0
        for _, editor in ipairs(owned) do
            if editor.readOnly and editor.visible and (editor.x or -99999) > -1000 then count = count + 1 end
        end
        return count
    end
    next_step_result = nil
    env.fctEditor.editor:setExpression("\\0el {!s 2*x+5=13}")
    env.on.enterKey()
    env.on.paint(gc)
    check(placed() > 0, "a healthy viewer frame places its math boxes: " .. placed())
    -- An incidental trigger: what is under test is the recovery, not this field.
    env.steps.result.mode = {}
    drawn = {}
    env.on.paint(gc)
    check(table.concat(drawn, "\n"):find("paint failed", 1, true) ~= nil,
          "a viewer paint that raises says so on the canvas")
    check(placed() == 0, "and parks the boxes that would otherwise sit over the message: " .. placed())
    for _ = 1, 5 do env.on.paint(gc) end
    check(placed() == 0, "and repeated failures do not put them back: " .. placed())
    env.steps.result.mode = "solve"
    env.on.paint(gc)
    check(placed() > 0, "and the frame after a recovered record draws its boxes again: " .. placed())
    env.on.escapeKey()
    D2Editor.newRichText = create
    end)()
end

-- The error handler discards the View and builds another, so the editors the old one held are its to release.
do
    (function()
    local create = D2Editor.newRichText
    local owned = {}
    D2Editor.newRichText = function()
        local editor = create()
        owned[#owned + 1] = editor
        return editor
    end
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    env.fctEditor.editor:setExpression("\\0el {1+1}")
    env.on.enterKey()
    local function onScreen()
        local count = 0
        for _, editor in ipairs(owned) do
            if editor.visible and (editor.x or -99999) > -1000 then count = count + 1 end
        end
        return count
    end
    check(onScreen() == 3, "one history row and an input line are three editors on screen: " .. onScreen())
    for round = 1, 3 do
        env.myErrorHandler(1, "boom " .. round)
        local orphans = 0
        for _, row in ipairs(env.histME1) do
            local held = false
            for _, widget in ipairs(env.theView.widgetList) do held = held or widget == row end
            if not held then orphans = orphans + 1 end
        end
        check(onScreen() == 3, "a script error releases the editors of the View it replaced: round " ..
              round .. " left " .. onScreen() .. " on screen")
        check(orphans == 0, "and keeps no history row behind the View that owns it: round " ..
              round .. " orphaned " .. orphans)
        check(#env.steps.histText == 1 and env.steps.histText[1][1] == "Script error",
              "and the rebuilt history holds only what the new View drew: round " .. round)
    end
    D2Editor.newRichText = create
    end)()
end

-- A zero-size window is expected, but leaving inited true claims a View the retry gate then never rebuilds.
do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local width, height = platform.window.width, platform.window.height
    platform.window.width = function() return 0 end
    platform.window.height = function() return 0 end
    -- A nil here resolves through to the global, so the last document's View has to be out of the way.
    local shared = rawget(_G, "theView")
    _G.theView = nil
    local handled = pcall(env.myErrorHandler, 1, "boom")
    check(handled, "the error handler survives a window it cannot build a View in")
    check(env.inited == false and rawget(env, "theView") == nil,
          "and says the GUI is down rather than claiming a View it does not have")
    platform.window.width, platform.window.height = width, height
    local resized = pcall(env.on.resize)
    check(resized and env.inited == true and rawget(env, "theView") ~= nil,
          "so the next resize rebuilds it instead of indexing a nil View")
    check(pcall(env.on.paint, gc), "and the paint after that draws on the rebuilt View")
    _G.theView = shared
    end)()
end

-- currentFocus indexes focusList, so a path that shortens the list must leave it valid or zero.
do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local function focusable()
        local o = { acceptsFocus = true, visible = true, x = 0, y = 0, w = 10, h = 10, dx1 = 0, dy1 = 0 }
        function o:repos() end
        function o:resize() end
        function o:setFocus() self.focused = true end
        function o:releaseFocus() self.focused = false end
        function o:contains() return false end
        return o
    end
    local view = env.View({ invalidate = function() end })
    local only = view:add(focusable())
    view:setFocus(only)
    check(view:getFocus() == only, "a view focuses the one widget it was given")
    view:remove(only)
    check(view.currentFocus == 0 and view:getFocus() == nil,
          "removing the focused widget leaves no focus rather than an index into an empty list")
    local replacement = view:add(focusable())
    check(pcall(view.setFocus, view, replacement) and view:getFocus() == replacement,
          "and the next widget added after that removal can still take focus")
    local empty = env.View({ invalidate = function() end })
    check(pcall(empty.tabForward, empty), "tabbing forward with nothing to focus is refused quietly")
    check(pcall(empty.tabBackward, empty), "tabbing backward with nothing to focus is refused quietly")

    -- View:hide is the only writer of visible, and until this block nothing in the document called
    -- it, so every visible guard read a field that was always true. These four drive it, because a
    -- guard nobody has watched work is not coverage.
    local tabs = env.View({ invalidate = function() end })
    tabs:add(focusable())
    tabs:add(focusable())
    tabs:add(focusable())
    -- Read the order out of the list rather than assuming it. View:add inserts at the front, so
    -- naming these after the calls that made them describes the reverse of the traversal.
    local one, two, three = tabs.focusList[1], tabs.focusList[2], tabs.focusList[3]
    tabs:setFocus(one)
    tabs:hide(two)
    check(not two.visible, "View:hide is what writes visible, and it is reachable")
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
    -- Before the traversal was bounded this recursed until the stack gave out, which on the handheld
    -- is a reset rather than an error a pcall can see.
    check(pcall(tabs.tabForward, tabs) and pcall(tabs.tabBackward, tabs),
          "tabbing with every widget hidden returns instead of recursing forever")
    end)()
end

-- The footer count is a position among steps. A record with none has no position to report.
do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local function stepless(progression)
        env.closeSteps()
        env.stepsSetProgression(progression)
        env.steps.result = nil
        next_step_result = {
            outcome = "unsupported form", status = "no rule applied", solved = false,
            steps = {}, result = "x", giac_raw = "x", mode = "integrate",
            original_expression = "sec(x)^3", normalized_expression = "sec(x)^3",
            request_expression = "sec(x)^3",
        }
        env.fctEditor.editor:setExpression("\\0el {!i sec(x)^3}")
        env.on.enterKey()
        drawn = {}
        env.on.paint(gc)
        return table.concat(drawn, " ")
    end
    local full = stepless("full")
    check(env.steps.active and #env.steps.result.steps == 0,
          "a recorded outcome with no steps still opens its viewer")
    -- The positive string rather than the absence of the wrong one. paintOverlay swallows a painter
    -- raise and draws its banner, so an absence-only check here passes on a footer never drawn.
    check(full:find("no steps", 1, true) ~= nil and full:find("1/0", 1, true) == nil,
          "a walkthrough with no steps says so rather than counting a first step out of none")
    local hint = stepless("hint")
    check(env.steps.active and env.steps.walkthrough == "hint",
          "the same record reopens under hint progression")
    check(hint:find("no steps", 1, true) ~= nil and hint:find("hint 0/0", 1, true) == nil,
          "and hint progression says the same rather than reporting nought revealed of nought")
    end)()
end

-- An overflowing list changes how much room the footer has, not what the record is. A hint
-- walkthrough with every step revealed has no next hint to offer, so both cues must reach that
-- conclusion from the same visibility decision rather than from the two-valued mode alone.
do
    (function()
    -- Past MATH_PREVIEW_BYTES the focused math row is drawn as text, which is what sets listOverflow.
    local long_after = "(" .. string.rep("x + ", 700) .. "x)"
    local function twoStepRecord(after_one, after_two)
        return {
            outcome = "solved", detail = "", solved = true, status = "solved and verified",
            result = "x = 4", result_form = "exact", giac_tag = "exact", agrees = true,
            step_count = 2, rewrites = 2, giac_calls = 0, nodes = 6,
            original_expression = "2*x+5=13", normalized_expression = "2*x+5=13",
            steps = {
                { kind = "transformation", phase = "solve", name = "Isolate the term",
                  goal = "Subtract five from both sides", short = "Undo the addition",
                  claim = "equivalent expression", verified = true, failed = false, depth = 0,
                  before = "2*x+5=13", after = after_one, action = "Subtract 5", rule = "s.subtract" },
                { kind = "transformation", phase = "solve", name = "Divide by the coefficient",
                  goal = "Divide both sides by two", short = "Undo the multiplication",
                  claim = "equivalent expression", verified = true, failed = false, depth = 0,
                  after = after_two, action = "Divide by 2", rule = "s.divide" },
            },
        }
    end
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    local function footerFor(record, progression, reveals)
        env.closeSteps()
        env.stepsSetProgression(progression)
        env.steps.result = nil
        next_step_result = record
        env.fctEditor.editor:setExpression("\\0el {!s 2*x+5=13}")
        env.on.enterKey()
        for _ = 1, reveals do env.on.tabKey() end
        for _ = 1, 4 do env.on.paint(gc) end
        drawn = {}
        env.on.paint(gc)
        return table.concat(drawn, " ")
    end

    local exhausted = footerFor(twoStepRecord(long_after, long_after), "hint", 1)
    check(env.steps.view == "list" and env.steps.listOverflow and env.steps.walkthrough == "hint" and
          env.steps.revealed == 2 and env.steps.focus == 2,
          "a hint record with both steps revealed still overflows its list")
    check(exhausted:find("hint 2/2", 1, true) ~= nil and
          exhausted:find("ANSWER shown", 1, true) ~= nil and
          exhausted:find("TAB next hint", 1, true) == nil,
          "so its overflow footer states the answer is shown rather than offering a hint that is gone")

    local remaining = footerFor(twoStepRecord(long_after, long_after), "hint", 0)
    check(env.steps.listOverflow and env.steps.revealed == 1 and env.steps.focus == 1,
          "the same record with one hint still to come overflows the same way")
    check(remaining:find("hint 1/2", 1, true) ~= nil and
          remaining:find("TAB next hint", 1, true) ~= nil and
          remaining:find("ANSWER shown", 1, true) == nil,
          "and that overflow footer still offers the hint that exists")

    local fits = footerFor(twoStepRecord("x = 8", "x = 4"), "hint", 1)
    check(not env.steps.listOverflow and env.steps.revealed == 2,
          "the short-expression record reveals both steps without overflowing")
    check(fits:find("hint 2/2", 1, true) ~= nil and
          fits:find("ANSWER shown", 1, true) ~= nil and
          fits:find("TAB next hint", 1, true) == nil,
          "and reaches the same conclusion, so overflow decides the wording and not the fact")

    local full = footerFor(twoStepRecord(long_after, long_after), "full", 0)
    check(env.steps.listOverflow and env.steps.walkthrough == "full",
          "a full walkthrough overflows that list too")
    check(full:find("ENTER all work", 1, true) ~= nil and
          full:find("ANSWER shown", 1, true) == nil and
          full:find("TAB next hint", 1, true) == nil,
          "and keeps its own overflow cue rather than borrowing a hint one")
    end)()
end

-- A progression change cannot rewrite the walkthrough already on screen, so it must not say it did.
do
    (function()
    local env = loadIsolated(copyModule())
    env.on.paint(gc)
    env.stepsSetProgression("full")
    next_step_result = nil
    env.fctEditor.editor:setExpression("\\0el {!s 2*x+5=13}")
    env.on.enterKey()
    check(env.steps.active and env.steps.walkthrough == "full",
          "a full walkthrough is open before the progression changes")
    local said = env.stepsSetProgression("hint")
    check(env.steps.walkthrough == "full",
          "switching progression leaves the open walkthrough as it was recorded")
    check(said == env.steps.status and said:find("next", 1, true) ~= nil and
          said:find("Tab reveals", 1, true) == nil,
          "so it reports the setting as taking effect on the next solve, not on this one")
    env.closeSteps()
    env.steps.result = nil
    local plain = env.stepsSetProgression("hint")
    check(plain == "walkthrough: hint, Tab reveals",
          "with no walkthrough recorded the setting is simply in force")
    end)()
end

-- Reopening a retained record must not discard a progression message about the next solve. The
-- message belongs to that retained viewer state, but preparing another record replaces it.
do
    (function()
    local module = copyModule()
    module.solve = function(expression)
        local record = {}
        for key, value in pairs(fake_result) do record[key] = value end
        record.original_expression = expression
        record.normalized_expression = expression
        return record
    end
    local env = loadIsolated(module)
    env.on.paint(gc)
    env.stepsSetProgression("full")
    env.fctEditor.editor:setExpression("\\0el {!s 2*x+5=13}")
    env.on.enterKey()
    env.closeSteps()

    local progression_message = env.stepsSetProgression("hint")
    env.runSteps("reopen", "")
    check(env.steps.active and env.steps.status == progression_message,
          "reopening preserves the progression message written for the retained record")

    env.closeSteps()
    env.fctEditor.editor:setExpression("\\0el {!s 3*x=6}")
    env.on.enterKey()
    check(env.steps.active and env.steps.status ~= progression_message and
          env.steps.status:find("hint 1/", 1, true) == 1 and
          env.steps.status:find("Tab reveals next", 1, true) ~= nil,
          "a new record replaces the retained progression message with its own status: " ..
              tostring(env.steps.status))
    end)()
end

-- Both one-sided limit markers are the same marker, so recall has to strip either one.
do
    (function()
    local env = loadIsolated(copyModule())
    check(env.cleanAns1("2*x ≟ 2.0") == "2*x", "recall keeps the exact side of an approximation")
    local plus = env.cleanAns1("lim x^(+)")
    local minus = env.cleanAns1("lim x^(-)")
    check(minus == plus,
          "a limit from the left is cleaned the same way as one from the right: " ..
          string.format("%q vs %q", minus, plus))
    check(minus:find("(-)", 1, true) == nil,
          "so no one-sided marker survives into the editor")
    end)()
end

writeEvidence()
print(string.format("nps_v4 ui: %d checks, %d failed", checks, failures))
os.exit(failures == 0 and 0 or 1)
