-- Task 27: whether the OS ever asks a Lua document to serialize itself, and whether it asks when
-- Ctrl+Esc closes the document. The ui_smoke harness calls on.save directly, which proves the table
-- round-trips and says nothing about the OS calling it, so this measures the call rather than the
-- table.
--
-- Every lifecycle handler appends its name to a trace list, and on.save carries that list into the
-- saved state. Reopening therefore reports what ran before the save as well as whether the save
-- happened at all. If on.save never fires the trace dies with the document, and that absence is the
-- answer to 27 rather than a failed measurement.

local ok, nps = pcall(function() return nrequire("nps_nspire") end)
local mod = ok and nps or nil

local history = {}
local input = ""
local trace = {}
local restored = "no"
local saves = 0
local marks = 0
local writes = "none"

local function ran(name)
	trace[#trace + 1] = name
	if #trace > 8 then table.remove(trace, 1) end
end

-- Whether deactivate fires on a real close, which decides whether a checkpoint can be written on the
-- way out or has to be written continuously. The trace list dies with the document, so the only way
-- to see a handler run during a teardown is a side effect that outlives it: 3 arms this, and the
-- marker file it writes is fetched off the calculator afterwards. Ctrl+Esc is the control, because
-- deactivate is already known to fire there.
local armed = false

function on.create() ran("create") end
function on.activate() ran("activate") end

function on.deactivate()
	ran("deactivate")
	if armed and mod and type(mod.typed_check) == "function" then
		armed = false
		pcall(mod.typed_check)
	end
end
function on.getFocus() ran("getFocus") end
function on.loseFocus() ran("loseFocus") end
function on.destroy() ran("destroy") end
function on.closing() ran("closing") end
function on.timer() ran("timer") end

function on.save()
	saves = saves + 1
	ran("save")
	return {history = history, input = input, trace = trace, saves = saves}
end

function on.restore(state)
	restored = "no state"
	if type(state) ~= "table" then return end
	history = state.history or {}
	input = state.input or ""
	saves = state.saves or 0
	restored = "yes, " .. #history .. " entries, trace " .. table.concat(state.trace or {}, ",")
end

-- 1 marks the document changed, because Ki never calls markChanged anywhere and a dirty flag the
-- document never sets is one way the OS could decide there is nothing to serialize. 2 writes a file
-- from inside a document callback, which is the fallback path if nothing serializes.
function on.charIn(ch)
	if ch == "1" then
		local called, err = pcall(function() document.markChanged() end)
		marks = marks + 1
		if not called then restored = "markChanged failed: " .. tostring(err) end
	elseif ch == "2" then
		if mod and type(mod.typed_check) == "function" then
			local called, count = pcall(mod.typed_check)
			writes = tostring(called) .. " " .. tostring(count)
		else
			writes = "no typed_check"
		end
	elseif ch == "3" then
		armed = true
		writes = "armed"
	else
		input = input .. ch
	end
	platform.window:invalidate()
end

function on.enterKey()
	history[#history + 1] = input
	input = ""
	platform.window:invalidate()
end

function on.backspaceKey()
	input = string.sub(input, 1, -2)
	platform.window:invalidate()
end

function on.paint(gc)
	gc:setFont("sansserif", "r", 9)
	gc:drawString("module " .. (mod and "yes" or "no") .. "  saves " .. saves ..
		"  marks " .. marks .. "  write " .. writes, 4, 2, "top")
	gc:drawString("restored: " .. restored, 4, 18, "top")
	gc:drawString("input [" .. input .. "]", 4, 34, "top")
	gc:drawString("ran: " .. table.concat(trace, ","), 4, 50, "top")
	for index = 1, #history do
		gc:drawString(index .. " " .. history[index], 4, 50 + index * 16, "top")
	end
end
