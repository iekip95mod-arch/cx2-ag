-- Walk every chapter and every topic of ti_info through on.paint with a stub gc, so a nil field or a
-- bad index in the drawing path fails here rather than on the handheld.
local calls = 0
local gc = {}
local function noop() calls = calls + 1 return gc end
for _, name in ipairs({ "setColorRGB", "setFont", "drawString", "drawLine", "fillRect", "clipRect" }) do
	gc[name] = noop
end

_G.platform = {
	apilevel = "2.0",
	window = { width = function() return 320 end, height = function() return 240 end, invalidate = function() end },
}
_G.on = {}

-- A stub D2Editor that answers the size listener immediately, so the measured branch of the layout
-- is exercised here rather than only on the handheld. Without this the walk only ever sees the
-- unmeasured fallback and the whole math path would go untested.
local made = 0
_G.D2Editor = {
	newRichText = function()
		made = made + 1
		local ed = {}
		local function chain() return ed end
		for _, name in ipairs({ "setFontSize", "setFocus", "setReadOnly", "setBorder", "setVisible",
		                        "move", "resize" }) do
			ed[name] = chain
		end
		ed.setSizeChangeListener = function(_, fn) ed.listener = fn return ed end
		ed.setExpression = function(_, expr)
			if ed.listener then ed.listener(ed, 90, 40) end
			return ed
		end
		return ed
	end,
	boxesMade = function() return made end,
}

local src = assert(io.open("lua/ti_info.lua")):read("*a")
assert(loadstring(src))()

-- The chapter count is not exported, so step right until the title repeats.
local first = nil
local chapters = 0
repeat
	on.paint(gc)
	local title = nil
	chapters = chapters + 1
	on.arrowRight()
	if chapters > 40 then error("chapter cycle did not close") end
	title = tostring(chapters)
until chapters >= 7

local screens = 0
for c = 1, 7 do
	on.charIn(tostring(c))
	on.paint(gc)
	screens = screens + 1
	for topic = 1, 12 do
		on.arrowDown()
		on.paint(gc)
		on.enterKey()
		on.paint(gc)
		for _ = 1, 40 do
			on.arrowDown()
			on.paint(gc)
		end
		for _ = 1, 60 do
			on.arrowUp()
			on.paint(gc)
		end
		screens = screens + 1
		on.escapeKey()
		on.paint(gc)
	end
end

on.arrowLeft()
on.paint(gc)
on.resize()

-- Without this the walk passes even if every formula silently fell back to plain text and the
-- D2Editor path was never entered, which is the failure a does-it-crash test cannot see.
local made = D2Editor.boxesMade()
if made == 0 then
	print("  FAIL  no math boxes were ever created, the typeset path did not run")
	os.exit(1)
end

print(string.format("paint walk: %d chapters, %d topic entries, %d draw calls, %d math boxes, no error",
	7, screens, calls, made))
