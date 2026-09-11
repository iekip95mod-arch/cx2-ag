-- Question (h): whether an OS dialog called from our module returns cleanly to a Lua document and
-- leaves its event loop intact. The paint counter is the test: if the counter still advances after a
-- dialog closes, the loop survived.
--
-- It also lists the module's own surface, because nrequire matches by basename across the whole
-- documents tree and a stale copy anywhere wins, which looks exactly like a binding that was never
-- built.

local ok, nps = pcall(function() return nrequire("nps_nspire") end)
local mod = ok and nps or nil

local paints = 0
local log = {"m msg, c conf, 1 rev, 2 three, o menu, n num, k keys"}

-- With two buttons, esc returning the second index and esc returning a literal 2 are the same
-- reading. A reversed pair and a three-button dialog tell them apart, and which one is true decides
-- whether a confirm can be made safe with two buttons at all.
local arrows = {arrowKey = 0, arrowUp = 0, arrowDown = 0, arrowLeft = 0, arrowRight = 0}
local escapes = 0

local function note(line)
	log[#log + 1] = line
	if #log > 12 then table.remove(log, 2) end
end

function on.paint(gc)
	paints = paints + 1
	gc:setFont("sansserif", "r", 9)
	gc:drawString("paints " .. paints .. "  module " .. (mod and "yes" or "no") ..
		"  esc " .. escapes, 4, 2, "top")
	gc:drawString("any " .. arrows.arrowKey .. " up " .. arrows.arrowUp .. " dn " .. arrows.arrowDown ..
		" lf " .. arrows.arrowLeft .. " rt " .. arrows.arrowRight, 170, 2, "top")
	for index = 1, #log do
		gc:drawString(log[index], 4, 2 + index * 16, "top")
	end
end

function on.charIn(ch)
	if not mod then
		note("no module")
	elseif ch == "k" then
		local names = {}
		for name in pairs(mod) do
			names[#names + 1] = name
		end
		table.sort(names)
		note(#names .. " keys")
		local line = ""
		for index = 1, #names do
			local candidate = line == "" and names[index] or (line .. " " .. names[index])
			if #candidate > 44 then
				note(line)
				line = names[index]
			else
				line = candidate
			end
		end
		if line ~= "" then note(line) end
	elseif ch == "m" then
		if type(mod.os_msgbox) ~= "function" then
			note("os_msgbox missing")
		else
			local called, result = pcall(mod.os_msgbox, "Ki", "Does this return?")
			note("msgbox " .. tostring(called) .. " " .. tostring(result))
		end
	elseif ch == "c" then
		if type(mod.os_msgbox) ~= "function" then
			note("os_msgbox missing")
		else
			local called, result = pcall(mod.os_msgbox, "Ki", "Delete everything?", "OK", "Cancel")
			note("confirm " .. tostring(called) .. " " .. tostring(result))
		end
	elseif ch == "1" then
		if type(mod.os_msgbox) ~= "function" then
			note("os_msgbox missing")
		else
			local called, result = pcall(mod.os_msgbox, "Ki", "Clear?", "Cancel", "Clear History")
			note("reversed " .. tostring(called) .. " " .. tostring(result))
		end
	elseif ch == "2" then
		if type(mod.os_msgbox) ~= "function" then
			note("os_msgbox missing")
		else
			local called, result = pcall(mod.os_msgbox, "Ki", "Pick", "One", "Two", "Three")
			note("three " .. tostring(called) .. " " .. tostring(result))
		end
	elseif ch == "o" then
		if type(mod.os_menu) ~= "function" then
			note("os_menu missing")
		else
			local called, result = pcall(mod.os_menu, "Ki", {"Alpha", "Beta", "Gamma", "Delta"})
			note("menu " .. tostring(called) .. " " .. tostring(result))
		end
	elseif ch == "n" then
		if type(mod.os_number_input) ~= "function" then
			note("os_number_input missing")
		else
			local called, result = pcall(mod.os_number_input, "Ki", "sub", "How many?", 3, 0, 99)
			note("number " .. tostring(called) .. " " .. tostring(result))
		end
	end
	platform.window:invalidate()
end

-- All five register, so pressing one arrow says whether the OS dispatches to both the named handler
-- and the general one, which decides whether nps_v4 is double handling every press.
function on.arrowKey(key)
	arrows.arrowKey = arrows.arrowKey + 1
	note("arrowKey " .. tostring(key))
	platform.window:invalidate()
end

function on.arrowUp()
	arrows.arrowUp = arrows.arrowUp + 1
	platform.window:invalidate()
end

function on.arrowDown()
	arrows.arrowDown = arrows.arrowDown + 1
	platform.window:invalidate()
end

function on.arrowLeft()
	arrows.arrowLeft = arrows.arrowLeft + 1
	platform.window:invalidate()
end

function on.arrowRight()
	arrows.arrowRight = arrows.arrowRight + 1
	platform.window:invalidate()
end

function on.escapeKey()
	escapes = escapes + 1
	platform.window:invalidate()
end
