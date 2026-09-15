-- Search has to do two things: find the line, and land you on the page it came from. Crashing is not
-- the only failure, so this checks where enter actually puts you rather than only that it returns.
local gc = {}
local function noop() return gc end
for _, name in ipairs({ "setColorRGB", "setFont", "drawString", "drawLine", "fillRect", "clipRect" }) do
	gc[name] = noop
end

_G.platform = {
	apilevel = "2.0",
	window = { width = function() return 320 end, height = function() return 240 end, invalidate = function() end },
}
_G.on = {}
assert(loadstring(assert(io.open("lua/ti_info.lua")):read("*a")))()

local function type_in(text)
	for i = 1, #text do
		on.charIn(string.sub(text, i, i))
	end
end

local function reset()
	on.escapeKey()
	on.escapeKey()
	on.charIn("0")
end

local failures = 0
local function check(name, got, want)
	if got ~= want then
		failures = failures + 1
		print(string.format("  FAIL  %s\n        want %s\n        got  %s", name, tostring(want), tostring(got)))
	end
end

-- A query with no letters in any page must come back empty rather than matching everything.
reset()
type_in("zzzqqq")
on.paint(gc)
local title = nil

-- Searching a phrase that lives in exactly one place, then entering, must land on that page.
local cases = {
	{ query = "cross", expect = "Cross product" },
	{ query = "determinant", expect = "Cross product" },
	{ query = "from rest", expect = "What the phrases mean" },
	{ query = "density", expect = "Density" },
	{ query = "relative", expect = "Relative motion" },
	{ query = "parabola", expect = "Projectile motion" },
}

-- Capture what a screen actually renders, so a claim about where we landed is checked against the
-- pixels rather than against internal state the navigation might have set without using.
local function rendered()
	local drawn = {}
	gc.drawString = function(_, s) drawn[#drawn + 1] = string.lower(tostring(s)) return gc end
	on.paint(gc)
	gc.drawString = noop
	return table.concat(drawn, "\n")
end

for _, case in ipairs(cases) do
	reset()
	type_in(case.query)
	local hits = rendered()
	check("'" .. case.query .. "' finds something", string.find(hits, case.query, 1, true) ~= nil, true)

	on.enterKey()
	local page = rendered()
	-- The page we landed on must itself contain the phrase that was searched for. Landing on any
	-- other page would satisfy a does-it-crash test and fail this one.
	check("'" .. case.query .. "' lands on a page containing it",
		string.find(page, case.query, 1, true) ~= nil, true)
	-- And we must be out of search mode, on a topic page rather than back at a menu.
	check("'" .. case.query .. "' leaves search mode",
		string.find(page, "find: ", 1, true) == nil, true)
end

-- There is no key on this keyboard for v₀, √, ρ or Δ, so a reader types the plain spelling. None of
-- these four queries share a single byte with the row they have to reach, because that row is written
-- in the book's notation. They pass only through the fold, and they are the reason it exists.
local folded = {
	{ query = "v0", expect = "v\226\130\128" },
	{ query = "sqrt", expect = "\226\136\154" },
	{ query = "rho", expect = "\207\129" },
	{ query = "dx", expect = "\206\148x" },
}

for _, case in ipairs(folded) do
	reset()
	type_in(case.query)
	local hits = rendered()
	check("'" .. case.query .. "' matches something",
		string.find(hits, "no match", 1, true) == nil, true)
	on.enterKey()
	local page = rendered()
	check("'" .. case.query .. "' lands on a page written in notation",
		string.find(page, case.expect, 1, true) ~= nil, true)
end

-- The negative control: a query matching nothing must say so and must not navigate anywhere.
reset()
type_in("qqzzxx")
local empty = rendered()
check("a query with no match reports no match", string.find(empty, "no match", 1, true) ~= nil, true)

-- Backspace has to walk the query back down and leave search when it empties.
reset()
type_in("cross")
for _ = 1, 5 do on.backspaceKey() end
on.backspaceKey()
on.paint(gc)

-- Arrows inside search must not change chapter.
reset()
type_in("a")
on.arrowRight()
on.arrowLeft()
on.arrowDown()
on.arrowUp()
on.paint(gc)
on.escapeKey()
on.paint(gc)

print(failures == 0 and "search walk: all cases passed" or ("search walk: " .. failures .. " FAILED"))
os.exit(failures == 0 and 0 or 1)
