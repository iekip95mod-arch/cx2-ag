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

local function glyphs(s)
	local n = 0
	for _ in string.gmatch(s, "[%z\1-\127\194-\244][\128-\191]*") do n = n + 1 end
	return n
end

-- Walk every card and every reference topic so nothing is measured only in the one state it happens
-- to start in.
local function sweep()
	for _ = 1, 60 do
		on.paint(gc)
		on.arrowDown()
	end
	for _ = 1, 20 do
		on.tabKey()
		for _ = 1, 40 do
			on.paint(gc)
			on.arrowDown()
		end
	end
end

on.paint(gc)
sweep()
on.escapeKey()
sweep()

local over, seen = {}, {}
for _, line in ipairs(drawn) do
	local n = glyphs(line)
	if n > BUDGET and not seen[line] then
		seen[line] = true
		over[#over + 1] = { n = n, text = line }
	end
end
table.sort(over, function(a, b) return a.n > b.n end)

if #over == 0 then
	print("width walk: " .. #drawn .. " drawn strings, none wider than " .. BUDGET .. " characters")
	os.exit(0)
end
for _, row in ipairs(over) do
	print(string.format("  FAIL  %d chars, %d over: %s", row.n, row.n - BUDGET, row.text))
end
print("width walk: " .. #over .. " line(s) wider than " .. BUDGET)
os.exit(1)
