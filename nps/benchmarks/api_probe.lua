-- Whether a Lua document can ask the OS to serialize itself, which decides whether a Ki checkpoint
-- can reuse the on.save path that already works or has to write a file of its own. Nothing in Ki
-- mentions the document table, so what it holds on this OS has never been looked at.
--
-- Deliberately does not nrequire the module, so it loads immediately and a run costs one open.

local lines = {}

local function note(text)
	lines[#lines + 1] = text
end

local function dump(name, value)
	if type(value) ~= "table" then
		note(name .. " is " .. type(value))
		return
	end
	local keys = {}
	for key, held in pairs(value) do
		keys[#keys + 1] = tostring(key) .. ":" .. string.sub(type(held), 1, 1)
	end
	table.sort(keys)
	if #keys == 0 then
		note(name .. " is an empty table")
		return
	end
	local line = name .. " "
	for index = 1, #keys do
		local candidate = line .. keys[index] .. " "
		if #candidate > 46 then
			note(line)
			line = "  " .. keys[index] .. " "
		else
			line = candidate
		end
	end
	note(line)
end

dump("document", rawget(_G, "document"))
dump("var", rawget(_G, "var"))
dump("toolpalette", rawget(_G, "toolpalette"))

-- The globals list itself, because a save entry point may not live on the document table and there
-- is no documentation here to say where else it would be.
local globals = {}
for key in pairs(_G) do
	globals[#globals + 1] = tostring(key)
end
table.sort(globals)
note(#globals .. " globals")
local line = "  "
for index = 1, #globals do
	local candidate = line .. globals[index] .. " "
	if #candidate > 46 then
		note(line)
		line = "  " .. globals[index] .. " "
	else
		line = candidate
	end
end
note(line)

function on.paint(gc)
	gc:setFont("sansserif", "r", 7)
	for index = 1, #lines do
		gc:drawString(lines[index], 2, index * 11 - 9, "top")
	end
end
