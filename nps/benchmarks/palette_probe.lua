-- Measures where the TI framework truncates a tool palette label, which the host harness cannot see
-- because the palette is drawn by the framework and never touches our graphics context.
--
-- Each label ends with #, so a label still showing its # was not truncated. The limit is a rendered
-- width rather than a character count, so English text and all-W text are measured separately: the
-- first is what Ki ships, the second is the worst case a label could reach at the same length.

local english = "Solve the equation for x and check the result twice over again"

local function realistic(n)
	return english:sub(1, n - 1) .. "#"
end

local function widest(n)
	return string.rep("W", n - 1) .. "#"
end

local function noop()
end

toolpalette.register({
	{"English",
		{realistic(44), noop},
		{realistic(47), noop},
		{realistic(50), noop},
		{realistic(53), noop},
		{realistic(56), noop},
		{realistic(60), noop},
	},
	{"Widest",
		{widest(16), noop},
		{widest(19), noop},
		{widest(22), noop},
		{widest(25), noop},
	},
})

function on.paint(gc)
	gc:setFont("sansserif", "r", 10)
	gc:drawString("Press menu. A label still showing its", 4, 4, "top")
	gc:drawString("trailing # was not truncated.", 4, 22, "top")
end
