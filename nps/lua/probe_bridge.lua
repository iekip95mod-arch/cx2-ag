-- Diagnostic document, not product code. ki_v2.lua reset the calculator with "attempt to index a
-- function value" and a reset leaves nothing to read, so this asks the same questions with every
-- call wrapped and draws the answers instead of dying.

local lines = {}

local function note(s)
    lines[#lines + 1] = tostring(s)
end

local ok, err = pcall(nrequire, "nps_split")
note("nrequire nps_split: ok=" .. tostring(ok) .. " err=" .. tostring(err))
note("type(nps_split) = " .. type(nps_split))

if type(nps_split) == "table" then
    local names = ""
    for k, v in pairs(nps_split) do
        names = names .. tostring(k) .. ":" .. type(v) .. " "
    end
    note("fields: " .. names)
end

local ok2, err2 = pcall(nrequire, "luagiac")
note("nrequire luagiac: ok=" .. tostring(ok2) .. " err=" .. tostring(err2))
note("type(luagiac) = " .. type(luagiac))

local ok3, r3 = pcall(function()
    return nps_split.differentiate("x^2*sin(x)", "x")
end)
note("differentiate: ok=" .. tostring(ok3))

if ok3 and type(r3) == "table" then
    note("outcome = " .. tostring(r3.outcome))
    note("result = " .. tostring(r3.result))
    note("canonical = " .. tostring(r3.canonical))
    note("giac_tag = " .. tostring(r3.giac_tag))
    note("giac = " .. tostring(r3.giac))
    note("agrees = " .. tostring(r3.agrees))
    note("steps = " .. tostring(r3.steps and #r3.steps))
else
    note("returned: " .. tostring(r3))
end

function on.paint(gc)
    gc:setFont("sansserif", "r", 7)
    gc:setColorRGB(0, 0, 0)
    local y = 0
    for i = 1, #lines do
        gc:drawString(lines[i], 2, y, "top")
        y = y + 11
    end
end
