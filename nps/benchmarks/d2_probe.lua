-- Asks the OS which expression forms a read-only D2Editor typesets, and what each one costs in
-- pixels, because the steps viewer draws every expression with gc:drawString today and the choice
-- between reusing this widget and writing a typesetter turns on the answer.
--
-- Needs no module: it is a plain document, so it opens in a second rather than in the tens of
-- seconds the integrity gate costs.
--
-- Each row is one sample in its own read-only editor. The number beside a row is the natural size
-- the editor reported through its size change listener, so a stacked form announces itself by
-- being taller than a line. Giac spelling and TI spelling are both present for the same maths,
-- since the engine emits the first and the OS may only understand the second.
--
-- Row 9 is the horizontal test: a long expression in a box narrower than it needs. Tab focuses it,
-- then left and right say whether a read-only box scrolls or truncates.

local SAMPLES = {
	{ "frac", "(x^2+1)/(x-3)" },
	{ "power", "x^(-1)" },
	{ "root", "sqrt(x+1)" },
	{ "int giac", "int(x^2,x)" },
	{ "int ti", "\226\136\171(x^2,x)" },
	{ "diff giac", "diff(x^2,x)" },
	{ "vector", "[[3],[4]]" },
	{ "unit", "9.8*_m/_s^2" },
	{ "long", "(3*x^4-2*x^3+7*x^2-11*x+5)/(x^2-4*x+4)+sqrt(2*x+9)" },
}

local ROW = 24
local LABEL_W = 58
local BOX_X = LABEL_W + 2
local BOX_W = 150
local SIZE_X = BOX_X + BOX_W + 4

local editors = {}
local sizes = {}
local built = false
local focused = 0

local function build()
	for i, sample in ipairs(SAMPLES) do
		local e = D2Editor.newRichText()
		e:setFontSize(9)
		e:setBorder(1)
		e:setBorderColor(0xc0c0c0)
		e:move(BOX_X, (i - 1) * ROW + 1)
		e:resize(BOX_W, ROW - 2)
		e:setSizeChangeListener(function(editor, w, h)
			sizes[i] = w .. "x" .. h
			platform.window:invalidate()
			return true
		end)
		e:setExpression("\\0el {" .. sample[2] .. "}", 0)
		e:setReadOnly(true)
		e:setFocus(false)
		editors[i] = e
	end
	built = true
end

function on.paint(gc)
	if not built then build() end
	gc:setFont("sansserif", "r", 7)
	for i, sample in ipairs(SAMPLES) do
		local y = (i - 1) * ROW + 4
		gc:setColorRGB(0, 0, 0)
		gc:drawString(sample[1], 2, y, "top")
		gc:setColorRGB(140, 140, 140)
		gc:drawString(sizes[i] or "?", SIZE_X, y, "top")
	end
	gc:setColorRGB(0, 0, 160)
	gc:drawString("focus " .. focused, SIZE_X, 220, "top")
end

-- Tab walks the focus down the column so the last row can be driven with left and right.
function on.tabKey()
	if focused > 0 and editors[focused] then editors[focused]:setFocus(false) end
	focused = focused + 1
	if focused > #SAMPLES then focused = 1 end
	editors[focused]:setFocus(true)
	platform.window:invalidate()
end

function on.escapeKey()
	if focused > 0 and editors[focused] then editors[focused]:setFocus(false) end
	focused = 0
	platform.window:invalidate()
end
