-- Diagnostic document, not product code. The solve path froze the handheld where the differentiate
-- path did not, so this walks the difference one call at a time. Each enter press first paints what
-- it is about to try and returns, so a freeze leaves the name of the guilty call on screen.

platform.apilevel = '2.0'

local core_ok = pcall(nrequire, "nps_split")
local giac_ok = pcall(nrequire, "luagiac")

local steps = {
    {
        name = "our solver alone, no Giac",
        run = function()
            local r = nps_split.solve("2x + 5 = 13", "x", false)
            return "outcome=" .. tostring(r.outcome) .. " result=" .. tostring(r.result)
        end,
    },
    {
        name = "Giac alone, simplify",
        run = function()
            return tostring(luagiac.caseval("simplify(((2*x)+5)-13)"))
        end,
    },
    {
        name = "Giac alone, the solve command the adapter builds",
        run = function()
            return tostring(luagiac.caseval("solve((((2*x)+5))=(13),x)"))
        end,
    },
    {
        name = "our solver with the cross-check, three arguments",
        run = function()
            local r = nps_split.solve("2x + 5 = 13", "x", true)
            return "outcome=" .. tostring(r.outcome) .. " tag=" .. tostring(r.giac_tag) ..
                   " raw=" .. tostring(r.giac_raw)
        end,
    },
    {
        name = "our solver, two arguments",
        run = function()
            local r = nps_split.solve("2x + 5 = 13", "x")
            return "tag=" .. tostring(r.giac_tag) .. " raw=" .. tostring(r.giac_raw)
        end,
    },
    {
        name = "differentiate, two arguments",
        run = function()
            local r = nps_split.differentiate("x^2*sin(x)", "x")
            return "tag=" .. tostring(r.giac_tag) .. " agrees=" .. tostring(r.agrees)
        end,
    },
    {
        name = "differentiate, three arguments",
        run = function()
            local r = nps_split.differentiate("x^2*sin(x)", "x", true)
            return "tag=" .. tostring(r.giac_tag) .. " agrees=" .. tostring(r.agrees)
        end,
    },
}

local log = {}
local index = 0
local armed = false

local function paint(gc)
    gc:setFont("sansserif", "r", 7)
    gc:setColorRGB(0, 0, 0)
    gc:drawString("solve probe   core=" .. tostring(core_ok) .. " giac=" .. tostring(giac_ok) ..
                  "   enter twice per step", 2, 0, "top")
    local y = 12
    for i = 1, #log do
        gc:drawString(log[i], 2, y, "top")
        y = y + 11
    end
    if armed then
        gc:setColorRGB(180, 0, 0)
        gc:drawString("about to run " .. index .. ": " .. steps[index].name, 2, y, "top")
    elseif index >= #steps then
        gc:setColorRGB(0, 120, 0)
        gc:drawString("all steps done", 2, y, "top")
    else
        gc:setColorRGB(90, 90, 90)
        gc:drawString("press enter to arm step " .. (index + 1), 2, y, "top")
    end
end

function on.paint(gc)
    local ok, err = pcall(paint, gc)
    if not ok then
        gc:setColorRGB(180, 0, 0)
        gc:drawString("paint failed: " .. tostring(err), 2, 2, "top")
    end
end

function on.enterKey()
    if armed then
        local ok, out = pcall(steps[index].run)
        log[#log + 1] = index .. ": " .. (ok and tostring(out) or ("ERROR " .. tostring(out)))
        armed = false
    elseif index < #steps then
        index = index + 1
        armed = true
    end
    platform.window:invalidate()
end

on.returnKey = on.enterKey
