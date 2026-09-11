-- Diagnostic document, not product code. ki_v2.lua dies with "attempt to index a function value"
-- before its first paint and the message carries no line number, so every construct it uses at
-- chunk level is tried here on its own and the answer is drawn rather than thrown.

platform.apilevel = '2.0'

local lines = {}

local function note(s)
    lines[#lines + 1] = tostring(s)
end

local function try(what, f)
    local ok, r = pcall(f)
    note(what .. ": ok=" .. tostring(ok) .. " -> " .. tostring(r))
end

note("type(platform) = " .. type(platform))
note("type(platform.window) = " .. type(platform.window))
note("type(on) = " .. type(on))
note("type(nrequire) = " .. type(nrequire))
note("apilevel = " .. tostring(platform.apilevel))

try("window:width", function() return platform.window:width() end)
try("window:height", function() return platform.window:height() end)
try("window.width", function() return platform.window.width end)
try("read on.paint", function() return type(on.paint) end)

try("assign and read back", function()
    function on.backspaceKey() end
    on.deleteKey = on.backspaceKey
    return type(on.deleteKey)
end)

try("nrequire nps_split", function()
    nrequire("nps_split")
    return type(nps_split)
end)

function on.paint(gc)
    gc:setFont("sansserif", "r", 7)
    gc:setColorRGB(0, 0, 0)
    local y = 0
    for i = 1, #lines do
        gc:drawString(lines[i], 2, y, "top")
        y = y + 11
    end
end
