-- A vocabulary app fails in two ways that still render: a term that cannot be reached by typing its
-- own name, and a term with no definition behind it. Both are checked here rather than counting rows.
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
assert(loadstring(assert(io.open("lua/ti_voc.lua")):read("*a")))()

local function render()
	drawn = {}
	on.paint(gc)
	local out = table.concat(drawn, "\n")
	drawn = nil
	return out
end

local function whole()
	local seen = {}
	for _ = 1, 30 do
		seen[#seen + 1] = render()
		on.arrowDown()
	end
	return table.concat(seen, "\n")
end

local failures = 0
local function check(name, got, want)
	if got ~= want then
		failures = failures + 1
		print(string.format("  FAIL  %s\n        want %s\n        got  %s", name, tostring(want), tostring(got)))
	end
end

-- Collect every term name off the list, paging through it.
local names = {}
on.escapeKey()
for _ = 1, 60 do
	local page = render()
	for line in string.gmatch(page, "[^\n]+") do
		if string.find(line, "%a") and not string.find(line, "ti_voc", 1, true)
			and not string.find(line, "type to narrow", 1, true) and #line > 4 then
			names[line] = true
		end
	end
	on.arrowDown()
end

local count = 0
for _ in pairs(names) do count = count + 1 end
check("the list shows a plausible number of terms", count >= 30, true)

-- Every term must be reachable by typing its own name, and must have a definition behind it.
local checked = 0
for name in pairs(names) do
	on.escapeKey()
	on.escapeKey()
	for i = 1, #name do
		on.charIn(string.sub(name, i, i))
	end
	local listed = render()
	if not string.find(listed, name, 1, true) then
		failures = failures + 1
		print("  FAIL  typing '" .. name .. "' does not find it")
	else
		on.enterKey()
		local body = whole()
		checked = checked + 1
		-- The header repeats the name, so a page with only the header and nothing else is a term
		-- carrying no definition.
		local _, headers = string.gsub(body, name, "")
		if #body - #name * headers < 60 then
			failures = failures + 1
			print("  FAIL  '" .. name .. "' has no definition body")
		end
	end
end

-- A formula attached to a term has to reach the screen. The definitions stayed readable after the
-- formulas were added, so a dropped eq block would leave every check above still passing.
local FORMULAS = {
	["Constant acceleration"] = { "v = v₀ + a t", "v² = v₀² + 2 a Δx", "Δx = v t − a t²/2" },
	["Density"] = { "ρ = m / V" },
	["Projectile motion"] = { "y = (v₀ sinθ₀) t − g t²/2" },
	["Free-fall acceleration"] = { "a = −g,  g = 9.8 m/s²" },
	-- The bars are the whole point of these two rows. Without them both sides read as the same
	-- product, which is how the scalar product came to be written as a circular statement.
	["Magnitude"] = { "|a| = √(ax² + ay²)" },
	["Scalar product"] = { "a · b = |a| |b| cosφ" },
	["Vector product"] = { "|a × b| = |a| |b| sinφ" },
}
for name, wanted in pairs(FORMULAS) do
	on.escapeKey()
	on.escapeKey()
	for i = 1, #name do
		on.charIn(string.sub(name, i, i))
	end
	on.enterKey()
	local body = whole()
	for _, formula in ipairs(wanted) do
		if not string.find(body, formula, 1, true) then
			failures = failures + 1
			print("  FAIL  '" .. name .. "' does not show " .. formula)
		end
	end
end

-- drawString clips instead of wrapping, so an over-long definition line loses its tail silently. The
-- budget matches the one in ti_info_widths.lua and is a calibration rather than a pixel measurement.
local BUDGET = 48
local function glyphs(s)
	local n = 0
	for _ in string.gmatch(s, "[%z\1-\127\194-\244][\128-\191]*") do n = n + 1 end
	return n
end
local wide, reported = 0, {}
on.escapeKey()
on.escapeKey()
for name in pairs(names) do
	on.escapeKey()
	on.escapeKey()
	for i = 1, #name do on.charIn(string.sub(name, i, i)) end
	on.enterKey()
	for line in string.gmatch(whole(), "[^\n]+") do
		if glyphs(line) > BUDGET and not reported[line] then
			reported[line] = true
			wide = wide + 1
			failures = failures + 1
			print(string.format("  FAIL  %d chars, %d over: %s", glyphs(line), glyphs(line) - BUDGET, line))
		end
	end
end

-- A query matching nothing must say so rather than showing everything.
on.escapeKey()
on.escapeKey()
for _, ch in ipairs({ "z", "q", "x" }) do on.charIn(ch) end
check("a query with no match says so", string.find(render(), "no term matches", 1, true) ~= nil, true)

print(failures == 0
	and ("voc walk: " .. checked .. " terms, each typable and each with a definition, formulas shown, none too wide")
	or ("voc walk: " .. failures .. " FAILED"))
os.exit(failures == 0 and 0 or 1)
