-- The contents page is the way into the book, so it has to list every topic and every row it lands
-- on has to open something. Counting rows is not enough: a list that renders and does not navigate
-- would pass that and fail the reader.
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

local failures = 0
local function check(name, got, want)
	if got ~= want then
		failures = failures + 1
		print(string.format("  FAIL  %s\n        want %s\n        got  %s", name, tostring(want), tostring(got)))
	end
end

-- The app must open on the contents, not on a chapter.
local first = render()
check("opens on the contents page", string.find(first, "pick what your question", 1, true) ~= nil, true)

-- Walk the whole list. Every stop must open a topic, and escape must come back. Distinct opened
-- pages are counted by their text, because the contents render does not encode which row is picked.
local distinct, headers = {}, 0
local count = 0
for _ = 1, 200 do
	on.enterKey()
	local opened = render()
	if string.find(opened, "pick what your question", 1, true) then
		headers = headers + 1
	elseif not distinct[opened] then
		distinct[opened] = true
		count = count + 1
	end
	on.escapeKey()
	if not string.find(render(), "pick what your question", 1, true) then
		failures = failures + 1
		print("  FAIL  escape did not return to the contents")
		break
	end
	on.arrowDown()
end

check("no contents row opens nothing", headers, 0)
check("the walk reached every topic", count >= 35, true)

-- A chapter key must move the contents, not open a second kind of menu.
on.charIn("4")
local jumped = render()
check("a chapter key stays on the contents", string.find(jumped, "pick what your question", 1, true) ~= nil, true)
check("a chapter key scrolls to that chapter",
	string.find(jumped, "Motion in two dimensions", 1, true) ~= nil, true)

print(failures == 0
	and ("toc walk: " .. count .. " distinct topics, every one opened and returned")
	or ("toc walk: " .. failures .. " FAILED"))
os.exit(failures == 0 and 0 or 1)
