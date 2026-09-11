-- Settles MATH-011's templates half, and reads the two rows round two clipped.
--
-- The editable box is focused at load, so keys reach it without a tab. Pressing the division key in
-- a 2D editor opens a native fraction template and the caret key leaves it, the same way the
-- superscript box behaves. Whatever the box holds afterwards is drawn underneath exactly as
-- getExpression returns it, and the shell's own unwrapper is applied beside it. If the second line
-- reads as linear syntax then the OS supplies the templates and the shell already converts them.
--
-- Every box is sized from its own size change listener rather than guessed, which is also the
-- two-pass layout the steps viewer will need: place, wait for the callback, resize, repaint.

local READONLY = {
	{ "diff U+2202", "\226\136\130(x^2,x)" },
	{ "diff d(", "d(x^2,x)" },
}

local boxes = {}
local sizes = {}
local typed
local typedSize
local built = false

-- The shell's unwrapper, copied so the probe reports the same string the document would hand the
-- engine rather than a second opinion on it.
local function unwrap(rawexpr)
	local expr = ""
	local n = rawexpr:len()
	local b, bs, bi, status = 0, 0, 0, 0
	local i = 1
	while i <= n do
		local c = rawexpr:sub(i, i)
		if c == "{" then b = b + 1 elseif c == "}" then b = b - 1 end
		if status == 0 then
			if rawexpr:sub(i, i + 5) == "\\0el {" then
				bs = i + 6
				i = i + 5
				status = 1
				bi = b
				b = b + 1
			end
		elseif b == bi then
			status = 0
			expr = expr .. rawexpr:sub(bs, i - 1)
		end
		i = i + 1
	end
	return expr
end

-- The size listener fires while the boxes are still being made, so a layout before the last one
-- exists indexes a nil. Anything the viewer builds this way needs the same guard.
local function layout()
	if not built then return end
	local y = 2
	for i = 1, #READONLY do
		local h = sizes[i] and sizes[i].h or 20
		boxes[i]:move(80, y)
		boxes[i]:resize(200, h)
		sizes[i] = sizes[i] or {}
		sizes[i].y = y
		y = y + h + 3
	end
	typed:move(4, y + 12)
	typed:resize(300, (typedSize and typedSize.h or 24))
	return y
end

local function build()
	for i, sample in ipairs(READONLY) do
		local e = D2Editor.newRichText()
		e:setFontSize(9)
		e:setBorder(1)
		e:setSizeChangeListener(function(_, w, h)
			sizes[i] = { w = w, h = h, y = sizes[i] and sizes[i].y }
			layout()
			platform.window:invalidate()
			return true
		end)
		e:setExpression("\\0el {" .. sample[2] .. "}", 0)
		e:setReadOnly(true)
		e:setFocus(false)
		boxes[i] = e
	end
	typed = D2Editor.newRichText()
	typed:setFontSize(10)
	typed:setBorder(1)
	typed:setSizeChangeListener(function(_, w, h)
		typedSize = { w = w, h = h }
		layout()
		platform.window:invalidate()
		return true
	end)
	typed:createMathBox()
	built = true
	layout()
	typed:setFocus(true)
end

function on.paint(gc)
	if not built then build() end
	gc:setFont("sansserif", "r", 7)
	for i, sample in ipairs(READONLY) do
		local s = sizes[i] or {}
		gc:setColorRGB(0, 0, 0)
		gc:drawString(sample[1], 2, (s.y or 2) + 2, "top")
		gc:setColorRGB(140, 140, 140)
		gc:drawString((s.w or "?") .. "x" .. (s.h or "?"), 284, (s.y or 2) + 2, "top")
	end
	local raw = typed and typed:getExpression() or ""
	local y = 150
	gc:setColorRGB(0, 0, 160)
	gc:drawString("raw  " .. tostring(raw), 2, y, "top")
	gc:drawString("flat " .. unwrap(tostring(raw)), 2, y + 12, "top")
	gc:setColorRGB(140, 140, 140)
	gc:drawString("box " .. ((typedSize and typedSize.w) or "?") .. "x" ..
	              ((typedSize and typedSize.h) or "?"), 2, y + 24, "top")
end

-- t re-applies the box's own raw string through setExpression. Typing leaves content linear, so if
-- this makes the same characters stack then a template is an insert plus a re-set rather than a
-- key the OS has to own, and the shell's addString already does both.
function on.charIn(ch)
	if ch == "t" and typed then
		typed:setExpression(typed:getExpression(), 0)
	end
	platform.window:invalidate()
end
