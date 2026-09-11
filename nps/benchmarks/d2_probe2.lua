-- Round two of the D2Editor question. Round one used idealised expressions; the viewer feeds
-- engine output, so these are the strings the module actually emits, plus the two things round one
-- could not answer: what a full width fraction does when it is still too wide, and what the OS
-- hands back after a student fills a native template.
--
-- The bottom editor is the only editable one. Tab reaches it, the templates key opens a form, and
-- whatever getExpression returns is drawn underneath as plain text. If that reads back as linear
-- syntax then the OS already supplies MATH-011's templates half and there is nothing to build.

local SAMPLES = {
	{ "answer", "v = 17 m/s" },
	{ "vector", "(3 i + 4 j) m" },
	{ "branch", "x = 3 or x = -3" },
	{ "undef", "undef" },
	{ "diff U+2202", "\226\136\130(x^2,x)" },
	{ "diff d(", "d(x^2,x)" },
}

local WIDE = "(3*x^4-2*x^3+7*x^2-11*x+5)/(x^2-4*x+4)"

local ROW = 20
local LABEL_W = 56
local BOX_X = LABEL_W + 2
local BOX_W = 130
local SIZE_X = BOX_X + BOX_W + 3

local rows = {}
local sizes = {}
local wide, wideSize
local typed, typedSize
local built = false

local function newBox(x, y, w, h, expr, onSize)
	local e = D2Editor.newRichText()
	e:setFontSize(9)
	e:setBorder(1)
	e:setBorderColor(0xc0c0c0)
	e:move(x, y)
	e:resize(w, h)
	e:setSizeChangeListener(function(editor, ew, eh)
		onSize(ew .. "x" .. eh)
		platform.window:invalidate()
		return true
	end)
	if expr then
		e:setExpression("\\0el {" .. expr .. "}", 0)
		e:setReadOnly(true)
	end
	e:setFocus(false)
	return e
end

local function build()
	for i, sample in ipairs(SAMPLES) do
		rows[i] = newBox(BOX_X, (i - 1) * ROW + 1, BOX_W, ROW - 2, sample[2],
		                 function(s) sizes[i] = s end)
	end
	local y = #SAMPLES * ROW + 2
	wide = newBox(2, y, 300, ROW - 2, WIDE, function(s) wideSize = s end)
	typed = newBox(2, y + ROW + 12, 300, 24, nil, function(s) typedSize = s end)
	typed:createMathBox()
	built = true
end

function on.paint(gc)
	if not built then build() end
	gc:setFont("sansserif", "r", 7)
	for i, sample in ipairs(SAMPLES) do
		local y = (i - 1) * ROW + 3
		gc:setColorRGB(0, 0, 0)
		gc:drawString(sample[1], 2, y, "top")
		gc:setColorRGB(140, 140, 140)
		gc:drawString(sizes[i] or "?", SIZE_X, y, "top")
	end
	local y = #SAMPLES * ROW + 2
	gc:setColorRGB(140, 140, 140)
	gc:drawString("wide " .. (wideSize or "?"), 240, y + ROW - 8, "top")
	gc:setColorRGB(0, 0, 160)
	local raw = typed and typed:getExpression() or ""
	gc:drawString("raw: " .. tostring(raw), 2, y + ROW + 38, "top")
	gc:drawString("size " .. (typedSize or "?"), 2, y + ROW + 48, "top")
end

-- Repaint on every key so the readout tracks what is in the box.
function on.charIn()
	platform.window:invalidate()
end

function on.enterKey()
	platform.window:invalidate()
end
