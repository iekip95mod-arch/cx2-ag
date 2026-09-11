-- What the OS hands a document at apilevel 2.0 on this boot, and whether loading luagiac is what
-- takes the document down. ki_v2.lua died on open with "attempt to index a function value" even
-- though every index in it is under pcall. gcprobe.lua, which never loads luagiac, opens fine. So
-- the load-time facts are gathered first and shown, and the luagiac load waits for enter, so the
-- screen says which half killed it.

platform.apilevel = '2.0'

local shown = {}

local function t(s) shown[#shown + 1] = s end

local function ask(label, f)
    local ok, v = pcall(f)
    t(label .. ": " .. (ok and tostring(v) or ("raised " .. tostring(v))))
end

ask("apilevel", function() return platform.apilevel end)
ask("type(platform.window)", function() return type(platform.window) end)
ask("window:width()", function() return platform.window:width() end)
ask("window:invalidate()", function() platform.window:invalidate() return "ok" end)
ask("type(on)", function() return type(on) end)
ask("nrequire nps_split", function() return select(2, pcall(nrequire, "nps_split")) end)
ask("type(nps_split)", function() return type(nps_split) end)
ask("type(luagiac) before", function() return type(luagiac) end)

function on.paint(gc)
    gc:setFont("sansserif", "r", 9)
    gc:setColorRGB(0, 0, 0)
    gc:drawString("apiprobe: enter loads luagiac", 3, 0, "top")
    local y = 15
    for i = math.max(1, #shown - 13), #shown do
        gc:drawString(shown[i], 3, y, "top")
        y = y + 15
    end
end

-- Enter asks for a module that does not exist anywhere, tab asks for luagiac by name. If enter
-- alone resets the calculator, the missing-module path in Ndl's nrequire is the fault and
-- luagiac has nothing to do with it.
function on.enterKey()
    ask("nrequire nosuchmod", function() return select(2, pcall(nrequire, "nosuchmod")) end)
    ask("type(on) after", function() return type(on) end)
    platform.window:invalidate()
end

function on.tabKey()
    ask("nrequire luagiac", function() return select(2, pcall(nrequire, "luagiac")) end)
    ask("type(luagiac) after", function() return type(luagiac) end)
    platform.window:invalidate()
end

on.returnKey = on.enterKey
