-- What actually reaches a Lua document when a key is pressed. Deliberately does not nrequire our
-- module: these are OS questions, and loading 4 MB to ask them costs minutes on the emulator and
-- risks answering nothing when the load is the thing that goes wrong.
--
-- Three readings. Whether on.arrowKey fires alongside the four named handlers, whether Ctrl+Esc
-- arrives as on.escapeKey, and whether a focused editor keeps Ctrl+C to itself or lets it through as
-- on.copy.

local counts = {any = 0, up = 0, down = 0, left = 0, right = 0, escape = 0, copy = 0, cut = 0,
	paste = 0}
local last = "press a key"
local editor = nil

local function bump(name, detail)
	counts[name] = counts[name] + 1
	last = name .. (detail and (" " .. tostring(detail)) or "")
	platform.window:invalidate()
end

function on.construction()
	editor = D2Editor.newRichText()
	editor:move(4, 150):resize(200, 30)
	editor:setText("copy me")
	editor:setFocus(true)
end

function on.paint(gc)
	gc:setFont("sansserif", "r", 9)
	gc:drawString("any " .. counts.any .. "  up " .. counts.up .. "  dn " .. counts.down ..
		"  lf " .. counts.left .. "  rt " .. counts.right, 4, 4, "top")
	gc:drawString("esc " .. counts.escape .. "  copy " .. counts.copy .. "  cut " .. counts.cut ..
		"  paste " .. counts.paste, 4, 24, "top")
	gc:drawString("last: " .. last, 4, 44, "top")
	gc:drawString("f drops editor focus, g takes it back", 4, 64, "top")
	gc:drawString("editor focus: " .. tostring(editor and editor:hasFocus()), 4, 84, "top")
end

function on.arrowKey(key)
	bump("any", key)
end

function on.arrowUp()
	bump("up")
end

function on.arrowDown()
	bump("down")
end

function on.arrowLeft()
	bump("left")
end

function on.arrowRight()
	bump("right")
end

function on.escapeKey()
	bump("escape")
end

function on.copy()
	bump("copy")
end

function on.cut()
	bump("cut")
end

function on.paste()
	bump("paste")
end

-- The editor swallows charIn while it holds focus, so the two letters that move focus have to work
-- from either side and the document only sees them when focus is already off the editor.
function on.charIn(ch)
	if ch == "f" and editor then
		editor:setFocus(false)
		last = "focus off"
	elseif ch == "g" and editor then
		editor:setFocus(true)
		last = "focus on"
	elseif ch == "d" and editor then
		-- An editor on the page may take the arrows whatever hasFocus reports, so the only way to
		-- ask whether the document sees them is to have no editor at all.
		editor:setFocus(false)
		editor:resize(1, 1)
		editor = nil
		last = "editor gone"
	else
		last = "charIn " .. ch
	end
	platform.window:invalidate()
end
