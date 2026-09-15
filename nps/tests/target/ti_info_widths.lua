-- drawString clips at the right edge instead of wrapping, so a line too wide for the screen loses
-- its tail and the reader never learns there was one. Measuring the source text is not enough,
-- because the escapes in it are one glyph each once loaded, so this drives the real paint path and
-- measures what is actually handed to drawString.
--
-- The budget is characters rather than pixels because the stub has no font metrics, so it is a
-- calibration rather than a measurement: body text is font size 9 drawn at x=8 in a 320 px window,
-- and 48 sits under the longest line that has already been deployed and read back off a screenshot.
-- A line at 49 is not proven to clip. Anything well past this is worth shortening rather than
-- arguing about, and a real pixel measurement would need getStringWidth on the device.
local BUDGET = 48

local gc = {}
local drawn = {}
local function noop() return gc end
for _, name in ipairs({ "setColorRGB", "setFont", "drawLine", "fillRect", "clipRect" }) do
	gc[name] = noop
end
gc.drawString = function(_, s)
	drawn[#drawn + 1] = tostring(s)
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

-- Walks byte by byte rather than matching a pattern. The obvious pattern for counting codepoints
-- accepts a lead byte carrying no continuations, and a lead byte alone is exactly what a truncated
-- \ddd escape leaves behind, so the pattern version cannot see the defect this exists to catch.
local function glyphs(s)
	local n, i = 0, 1
	while i <= #s do
		local b = string.byte(s, i)
		local need
		if b < 0x80 then need = 0
		elseif b < 0xC2 then return nil, i
		elseif b < 0xE0 then need = 1
		elseif b < 0xF0 then need = 2
		elseif b < 0xF5 then need = 3
		else return nil, i end
		for k = 1, need do
			local c = string.byte(s, i + k)
			if not c or c < 0x80 or c > 0xBF then return nil, i end
		end
		i = i + need + 1
		n = n + 1
	end
	return n
end

-- Open every row of the contents, scroll it to the end, and come back. The first version of this
-- only arrowed down the contents list and never pressed enter, so it repainted the menu fourteen
-- hundred times and measured no card and no topic at all while reporting 29257 drawn strings. The
-- coverage control below is what would have caught that, so it is part of the check rather than a
-- comment about it.
local function walk()
	for _ = 1, 120 do
		on.enterKey()
		for _ = 1, 60 do
			on.paint(gc)
			on.arrowDown()
		end
		-- Tab moves to the sibling card or topic without going back to the contents, which is how a
		-- page reachable only by tabbing still gets measured.
		for _ = 1, 3 do
			on.tabKey()
			for _ = 1, 40 do
				on.paint(gc)
				on.arrowDown()
			end
		end
		on.escapeKey()
		on.paint(gc)
		on.arrowDown()
	end
end

on.paint(gc)
walk()

local joined = table.concat(drawn, "\n")
local missing = {}
for _, part in ipairs({ "LOOKS LIKE", "YOU NEED", "STEPS", "TRAP", "FORMULA" }) do
	if not string.find(joined, part, 1, true) then missing[#missing + 1] = part end
end
if #missing > 0 then
	print("  FAIL  the walk never rendered a card body, so nothing below was measured")
	print("        never drawn: " .. table.concat(missing, ", "))
	os.exit(1)
end

local over, bad, seen = {}, {}, {}
for _, line in ipairs(drawn) do
	local n, at = glyphs(line)
	if not n then
		if not seen[line] then
			seen[line] = true
			bad[#bad + 1] = { at = at, text = line }
		end
	elseif n > BUDGET and not seen[line] then
		seen[line] = true
		over[#over + 1] = { n = n, text = line }
	end
end
table.sort(over, function(a, b) return a.n > b.n end)

-- A truncated escape leaves a lead byte the font cannot resolve, and the glyph it stood for is simply
-- gone from the line. A dropped minus sign reads as a correct statement with the wrong sign, so this
-- is reported before the width failures rather than after them.
for _, row in ipairs(bad) do
	print(string.format("  FAIL  byte %d is not valid UTF-8, a glyph is missing: %s",
		row.at, string.gsub(row.text, "[\128-\255]", "?")))
end
for _, row in ipairs(over) do
	print(string.format("  FAIL  %d chars, %d over: %s", row.n, row.n - BUDGET, row.text))
end

if #over == 0 and #bad == 0 then
	print("width walk: " .. #drawn .. " drawn strings, none malformed, none wider than " .. BUDGET)
	os.exit(0)
end
print(string.format("width walk: %d malformed, %d wider than %d", #bad, #over, BUDGET))
os.exit(1)
