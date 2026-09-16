-- A worked example is the one place in the reference where a reader copies arithmetic rather than a
-- formula, so a wrong intermediate is worse than a wrong formula: it disagrees with the boxed answer
-- three lines below it and the reader cannot tell which to trust. These checks recompute each worked
-- example from its own stated inputs and require the printed numbers to agree.
_G.platform = {
	apilevel = "2.0",
	window = { width = function() return 320 end, height = function() return 240 end, invalidate = function() end },
}
_G.on = {}
_G.D2Editor = {
	newRichText = function()
		local ed = {}
		local function chain() return ed end
		for _, n in ipairs({ "setFontSize", "setFocus", "setReadOnly", "setBorder", "setVisible", "move", "resize" }) do ed[n] = chain end
		ed.setSizeChangeListener = function(_, fn) ed.listener = fn return ed end
		ed.setExpression = function() if ed.listener then ed.listener(ed, 90, 40) end return ed end
		return ed
	end,
}

local source = assert(io.open("lua/ti_info.lua")):read("*a")
local cards, chapters = assert(loadstring(source .. "\nreturn cards, chapters"))()

local failures = {}
local function check(ok, message)
	if not ok then failures[#failures + 1] = message end
end

-- Every line of prose in both tables, so a claim can be located wherever it was written.
local prose = {}
local function gather(lines)
	for _, line in ipairs(lines or {}) do
		if type(line) == "table" then
			prose[#prose + 1] = tostring(line.alt or line.m)
		else
			prose[#prose + 1] = line
		end
	end
end
for _, card in ipairs(cards) do gather(card.lines) end
for _, chapter in ipairs(chapters) do
	for _, topic in ipairs(chapter.topics or {}) do gather(topic.lines) end
end
local all = table.concat(prose, "\n")

local function number_after(pattern)
	local found = all:match(pattern)
	return found and tonumber(found)
end

-- The projectile-onto-a-cliff example. Its impact speed is built from two components it prints
-- separately, so the three numbers have to satisfy Pythagoras or one of them is a typo.
local vfx = number_after("vfx = v\226\130\128x = ([%-%d%.]+) m/s")
local vfy = number_after("vfy = 36%.4 %+ %(\226\136\1469%.80%)%(5%.50%) = \226\136\146([%d%.]+) m/s")
local vf = number_after("vf = ([%d%.]+) m/s")
check(vfx and vfy and vf, "the cliff example must print vfx, vfy and vf")
if vfx and vfy and vf then
	-- vfy is stated as a magnitude after the minus sign, so it is squared either way.
	local computed = math.sqrt(vfx * vfx + vfy * vfy)
	check(math.abs(computed - vf) < 0.05,
		string.format("the cliff example prints vf = %.1f but its own vfx = %.1f and vfy = -%.1f give %.2f", vf, vfx, vfy, computed))
	-- And vfy itself has to be what step 4's formula produces from the stated launch and time.
	local from_formula = 36.4 + (-9.80) * 5.50
	check(math.abs(-vfy - from_formula) < 0.05,
		string.format("the cliff example prints vfy = -%.1f but v0y + ay t is %.2f", vfy, from_formula))
end

-- g is a magnitude everywhere in this reference and the sign lives on a. A card that says otherwise
-- flips gravity for whoever reads that card rather than the chapter.
check(not all:find("g is negative", 1, true), "no page may say g is negative, because every formula here needs g positive")
for _, forbidden in ipairs({ "written as \226\136\1469.80" }) do
	check(not all:find(forbidden, 1, true), "no page may define g as a negative number")
end
check(all:find("a = \226\136\146g", 1, true), "the reference must state the convention a = -g somewhere")

-- The one formula on the free-fall card that is not general needs its condition attached, because a
-- reader copying it for a thrown ball gets a wrong time with no warning.
local fall = nil
for _, card in ipairs(cards) do if card.name == "Dropped, or thrown straight up" then fall = card end end
check(fall, "the dropped-or-thrown card must exist")
if fall then
	local condition_seen, guarded = false, false
	for _, line in ipairs(fall.lines) do
		if type(line) == "string" and line:find("dropped from rest only", 1, true) then condition_seen = true end
		if type(line) == "table" and tostring(line.m):find("sqrt(2*", 1, true) and condition_seen then guarded = true end
	end
	check(guarded, "t = sqrt(2 dy / a) must be introduced as holding only when the object started from rest")
end

if #failures > 0 then
	print("ti_info_numbers FAILED")
	for _, message in ipairs(failures) do print("  " .. message) end
	os.exit(1)
end
print("ti_info_numbers passed")
