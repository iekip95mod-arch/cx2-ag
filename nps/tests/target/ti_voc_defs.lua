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

print(failures == 0 and "def scan: every definition opens a sentence"
	or ("def scan: " .. failures .. " FAILED"))
os.exit(failures == 0 and 0 or 1)
