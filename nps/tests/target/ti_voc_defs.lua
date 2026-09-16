-- Pulling a formula out of a definition can take the sentence it was embedded in with it, leaving a
-- clause that starts mid-thought. That still renders and still passes the walk, because the walk only
-- asks whether a definition is there. This reads the source and asks whether each one starts.
local src = assert(io.open("lua/ti_voc.lua")):read("*a")

local function lines(text)
	local out, start = {}, 1
	while true do
		local stop = string.find(text, "\n", start, true)
		if not stop then
			out[#out + 1] = string.sub(text, start)
			return out
		end
		out[#out + 1] = string.sub(text, start, stop - 1)
		start = stop + 1
	end
end

local function trim(s)
	return (string.gsub(s, "^%s*(.-)%s*$", "%1"))
end

local failures = 0
local rows = lines(src)
local name, awaiting = nil, false
for i = 1, #rows do
	local row = trim(rows[i])
	local found = string.match(row, '^{ name = "([^"]+)"')
	if found then name = found end
	if awaiting then
		local first = string.match(row, '^"(.-)",?$')
		if first then
			local word = string.match(first, "^(%a+)")
			if word and word == string.lower(word) then
				failures = failures + 1
				print(string.format("  FAIL  '%s' opens mid-sentence: %s", tostring(name), first))
			end
			awaiting = false
		end
	end
	if row == "def = {" then awaiting = true end
end

-- A term named after a procedure has to say how to carry the procedure out. Naming what it yields
-- reads as a definition and leaves a student who has forgotten the hand unable to use it.
local function entry(term)
	local at = string.find(src, 'name = "' .. term .. '"', 1, true)
	if not at then
		return nil
	end
	local stop = string.find(src, "} },", at, true)
	return string.sub(src, at, stop or #src)
end

local rhr = entry("Right-hand rule")
if not rhr then
	failures = failures + 1
	print("  FAIL  there is no 'Right-hand rule' term")
else
	for _, word in ipairs({ "fingers", "thumb" }) do
		if not string.find(rhr, word, 1, true) then
			failures = failures + 1
			print(string.format("  FAIL  'Right-hand rule' never mentions the %s, so it gives no procedure", word))
		end
	end
end

-- The two apps sit side by side, and a reader who cross-checks one against the other must not find
-- the same constant quoted to a different precision.
local g = entry("Free-fall acceleration")
if not g then
	failures = failures + 1
	print("  FAIL  there is no 'Free-fall acceleration' term")
else
	local at, loose = 1, 0
	while true do
		local found = string.find(g, "9.8", at, true)
		if not found then
			break
		end
		if string.sub(g, found + 3, found + 3) ~= "0" then
			loose = loose + 1
		end
		at = found + 3
	end
	if loose > 0 then
		failures = failures + 1
		print("  FAIL  'Free-fall acceleration' states g to two figures, ti_info states 9.80")
	end
end

print(failures == 0 and "def scan: every definition opens a sentence, g reads 9.80, the hand is described"
	or ("def scan: " .. failures .. " FAILED"))
os.exit(failures == 0 and 0 or 1)
