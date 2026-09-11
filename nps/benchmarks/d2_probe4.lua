-- The last unknown behind MATH-011: whether setExpression on a box that already exists re-typesets
-- its contents, or only lays out what a box was born with.
--
-- It decides what a template can be here. Typing leaves content linear and the division key gives a
-- literal slash, so if a re-set stacks a fraction then a template is an insert plus a re-set and it
-- looks native. If it does not, a template can only be text and the two-dimensional form appears
-- after enter rather than while the student fills it in.
--
-- Driven from the tool palette rather than from a key, because a focused editor eats charIn, tab
-- and the arrows before on.* ever sees them. That is also where the real feature goes.

local typed
local built = false
local note = "menu, then Fraction"

local function insert(skeleton)
	if not typed then return end
	local raw, curpos, selstart = typed:getExpressionSelection()
	if not raw then return end
	local at = math.min(curpos or 0, selstart or 0)
	local head = raw:sub(1, at)
	local tail = raw:sub(at + 1)
	typed:setExpression(head .. skeleton .. tail, at + #skeleton)
	note = "inserted " .. skeleton
	platform.window:invalidate()
end

toolpalette.register({
	{ "Templates",
		{ "Fraction", function() insert("(1)/(2)") end },
		{ "Root", function() insert("sqrt(9)") end },
		{ "Reset", function()
			typed:setExpression("\\0el {}", 0)
			note = "reset"
			platform.window:invalidate()
		end },
	},
})

function on.paint(gc)
	if not built then
		typed = D2Editor.newRichText()
		typed:setFontSize(12)
		typed:setBorder(1)
		typed:move(4, 60)
		typed:resize(300, 70)
		typed:createMathBox()
		typed:setFocus(true)
		built = true
	end
	gc:setFont("sansserif", "r", 9)
	gc:setColorRGB(0, 0, 0)
	gc:drawString(note, 4, 4, "top")
	gc:setColorRGB(0, 0, 160)
	gc:drawString("raw " .. tostring(typed:getExpression()), 4, 22, "top")
	gc:drawString("a stacked 1 over 2 below means a re-set typesets", 4, 138, "top")
end
