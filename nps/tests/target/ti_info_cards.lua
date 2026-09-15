-- The point of a card is that every one has the same parts in the same order, so a reader under
-- time pressure knows where to look without reading. A card missing STEPS, or one that cannot be
-- reached from the first screen, breaks that promise while still rendering fine.
local gc = {}
local drawn = nil
local function noop() return gc end
for _, name in ipairs({ "setColorRGB", "setFont", "drawLine", "fillRect", "clipRect" }) do
	gc[name] = noop
end
gc.drawString = function(_, s)
	if drawn then drawn[#drawn + 1] = tostring(s) end
	return gc
end

_G.platform = {
	apilevel = "2.0",
	window = { width = function() return 320 end, height = function() return 240 end, invalidate = function() end },
}
_G.on = {}
_G.D2Editor = {
	newRichText = function()
		local ed = {}
		local function chain() return ed end
		for _, n in ipairs({ "setFontSize", "setFocus", "setReadOnly", "setBorder", "setVisible", "move", "resize" }) do
			ed[n] = chain
		end
		ed.setSizeChangeListener = function(_, fn) ed.listener = fn return ed end
		ed.setExpression = function() if ed.listener then ed.listener(ed, 90, 40) end return ed end
		return ed
	end,
}
assert(loadstring(assert(io.open("lua/ti_info.lua")):read("*a")))()

local function render()
	drawn = {}
	on.paint(gc)
	local out = table.concat(drawn, "\n")
	drawn = nil
	return out
end

-- Scroll a page to its end and gather everything it ever drew, since a card is taller than a screen.
local function whole()
	local seen = {}
	for _ = 1, 40 do
		seen[#seen + 1] = render()
		on.arrowDown()
	end
	for _ = 1, 40 do
		on.arrowUp()
	end
	return table.concat(seen, "\n")
end

local failures = 0
local function check(name, got, want)
	if got ~= want then
		failures = failures + 1
		print(string.format("  FAIL  %s\n        want %s\n        got  %s", name, tostring(want), tostring(got)))
	end
end

-- The first screen has to offer recognition, not chapter numbers.
local first = render()
check("first screen asks what the question looks like",
	string.find(first, "WHAT DOES YOUR QUESTION LOOK LIKE", 1, true) ~= nil, true)
check("first screen is not a chapter list",
	string.find(first, "Measurement", 1, true) == nil, true)

-- Walk every card with tab and hold each one to the same shape.
on.enterKey()
local opened = render()
check("enter from the first row opens a card",
	string.find(opened, "LOOKS LIKE", 1, true) ~= nil, true)

local names, n = {}, 0
for _ = 1, 40 do
	local page = whole()
	local title = string.match(page, "^([^\n]+)")
	if names[title] then
		break
	end
	names[title] = true
	n = n + 1
	for _, part in ipairs({ "LOOKS LIKE", "YOU NEED", "STEPS" }) do
		if not string.find(page, part, 1, true) then
			failures = failures + 1
			print("  FAIL  card '" .. tostring(title) .. "' has no " .. part)
		end
	end
	-- A card that uses a symbol it never names is the whole failure this app exists to avoid, and it
	-- renders perfectly, so nothing but a check like this one catches it. The symbols below are the
	-- ones a reader cannot guess: a bare letter tells them nothing on its own.
	--
	-- The search is bounded to the YOU NEED section rather than to everything after its heading. The
	-- first version of this check was not, which made it pass for every card whose formula sits below
	-- that heading, which is all of them.
	-- Collected line by line rather than by string positions, because whole() concatenates one render
	-- per scroll step and the same headings recur in each, which makes any offset arithmetic over the
	-- joined text meaningless. That was the second wrong version of this check.
	local section, collecting = {}, false
	for line in string.gmatch(page, "[^\n]+") do
		if string.find(line, "YOU NEED", 1, true) then
			collecting = true
		elseif collecting then
			for _, heading in ipairs({ "FORMULA", "STEPS", "WORKED", "TRAP", "CHECK IT TWICE" }) do
				if string.sub(line, 1, #heading) == heading then collecting = false end
			end
		end
		if collecting then section[#section + 1] = line end
	end
	section = table.concat(section, "\n")
	for _, symbol in ipairs({ "v\226\130\128", "\206\184", "\207\134", "\207\129", "\206\148x", "\206\148y" }) do
		if string.find(page, symbol, 1, true) and not string.find(section, symbol, 1, true) then
			failures = failures + 1
			print("  FAIL  card '" .. tostring(title) .. "' uses " .. symbol .. " without naming it under YOU NEED")
		end
	end
	on.tabKey()
end

check("tab reaches every card and wraps", n, 14)

-- A phrase from a card body must land on that card, not on a reference page behind it.
on.escapeKey()
for _, ch in ipairs({ "c", "l", "i", "f", "f" }) do on.charIn(ch) end
on.enterKey()
-- It lands scrolled to the matched line, so the header is what identifies the destination rather
-- than the first section heading, which may be above the fold by design.
local landed = render()
check("searching a card phrase opens a card",
	string.find(landed, "Launched at an angle", 1, true) ~= nil, true)

print(failures == 0
	and ("card walk: " .. n .. " cards, each with its sections, reachable and searchable")
	or ("card walk: " .. failures .. " FAILED"))
os.exit(failures == 0 and 0 or 1)
