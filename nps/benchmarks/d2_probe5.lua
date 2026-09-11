-- What a read-only box reports, and what it can then be drawn at.
--
-- Round one asked whether parking a box (moving it off screen and hiding it) in the same frame the
-- expression is set corrupts the measurement. It does not: hidden, moved and left alone all reported
-- 81x30 for 0.00000250 m^3 in a 240 wide box, and none of them saw a second listener call after
-- being resized. What clips is placing the box at the width it reported. 81 is the ink and not a
-- frame the same content fits in, so at 81 the m^3 wrapped onto a row the 30 pixel height cut off.
-- Placed at the 240 it was measured at, the whole thing draws with a raised superscript.
--
-- Round two is what happens when the content is wider than the frame, which is the case the steps
-- viewer's From line lands in. Each row below prints what its box reported and then draws at the
-- width it was measured at, so a row that shows its whole expression measured a height it fits in.

local BOXW = 250

local slots = {
	{ name = "short", expr = "0.00000250 m^3" },
	{ name = "long", expr = "quantity((5 * (2^-1)), unit(cm^3, (1 * (1000000^-1)), " ..
	                        "dimension(3, 0, 0)), measured, 3)" },
	{ name = "wordy", expr = "L^3 and L^3" },
}

local built = false
local placed = false

local function build()
	for _, slot in ipairs(slots) do
		slot.calls = 0
		slot.w = 0
		slot.h = 0
		local e = D2Editor.newRichText()
		e:setFontSize(9)
		e:setBorder(0)
		e:setReadOnly(true)
		e:setFocus(false)
		e:setSizeChangeListener(function(_, w, h)
			slot.calls = slot.calls + 1
			slot.w = w
			slot.h = h
			platform.window:invalidate()
			return true
		end)
		slot.editor = e
	end
	built = true
	for _, slot in ipairs(slots) do
		slot.editor:resize(BOXW, 13)
		slot.editor:setExpression("\\0el {" .. slot.expr .. "}", 0)
		slot.editor:move(-10000, -10000)
		slot.editor:setVisible(false)
	end
end

local function place()
	local y = 70
	for _, slot in ipairs(slots) do
		slot.editor:move(64, y)
		slot.editor:resize(BOXW, slot.h)
		slot.editor:setVisible(true)
		y = y + slot.h + 4
	end
	placed = true
end

function on.paint(gc)
	if not built then build() end
	gc:setFont("sansserif", "r", 7)
	gc:setColorRGB(0, 0, 0)
	gc:drawString("measured in a box " .. BOXW .. " wide", 2, 2, "top")
	local y = 14
	for _, slot in ipairs(slots) do
		gc:setColorRGB(0, 0, 160)
		gc:drawString(slot.name .. "  calls " .. slot.calls .. "  " ..
		              slot.w .. "x" .. slot.h, 2, y, "top")
		y = y + 11
	end
	if not placed then
		gc:setColorRGB(120, 120, 120)
		gc:drawString("p draws each one at " .. BOXW .. " by the height it reported", 2, y, "top")
	end
end

-- Whether the font ladder in the viewer can work at all. Stepping a box down only helps if the OS
-- remeasures the content it already holds, so f drops every box to 7 and the call counts above say
-- whether anything was reported back.
function on.charIn(ch)
	if ch == "p" and built then place() end
	if ch == "f" and built then
		for _, slot in ipairs(slots) do slot.editor:setFontSize(7) end
	end
	platform.window:invalidate()
end
