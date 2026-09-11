platform.apilevel = "2.0"

local editor
local mode = "Select an insertion method from Menu"
local skeleton = "limit(,x,-infinity)"
local raw, caret = "", 0
local sizing = "fixed"
local resizing = false
local pendingHeight

local function resize(height)
    if resizing then return end
    resizing = true
    height = math.min(math.floor(platform.window:height() / 2), math.max(22, height + 1))
    editor:resize(314, height - 1)
    editor:move(2, platform.window:height() - height)
    resizing = false
end

local function inspect()
    raw, caret = editor:getExpressionSelection()
    platform.window:invalidate()
end

local function insert(single, filled, layout, restoreFocus)
    if not editor then return end
    sizing = layout or "fixed"
    pendingHeight = nil
    editor:move(2, 100)
    editor:resize(314, 95)
    editor:setExpression("\\0el {}", 6)
    local expression = filled and "limit(x,x,-infinity)" or skeleton
    local position = 6 + #expression
    if single then position = 12 end
    editor:setExpression("\\0el {" .. expression .. "}", position)
    if not single then
        local current, ending = editor:getExpressionSelection()
        editor:setExpression(current, math.max(0, ending - 13))
    end
    mode = filled and "Filled control" or (single and "Single expression reset" or "Two expression resets")
    if layout then mode = layout .. " resize" end
    if restoreFocus ~= false then editor:setFocus(true) end
    inspect()
end

toolpalette.register({{"Insertion",
    {"Two resets", function() insert(false, false) end},
    {"Single reset", function() insert(true, false) end},
    {"Filled control", function() insert(true, true) end},
    {"Inspect expression", function() if editor then inspect() end end},
    {"Callback resize", function() insert(false, false, "callback") end},
    {"Deferred resize", function() insert(false, false, "deferred") end},
    {"Insert without focus reset", function() insert(false, false, "callback", false) end},
    {"Restore editor focus", function() editor:setFocus(true) inspect() end},
}})

function on.timer()
    timer.stop()
    if pendingHeight then
        local height = pendingHeight
        pendingHeight = nil
        resize(height)
        platform.window:invalidate()
    end
end

function on.paint(gc)
    if not editor then
        editor = D2Editor.newRichText()
        editor:move(2, 100)
        editor:resize(314, 95)
        editor:setFontSize(12)
        editor:setBorder(1)
        editor:createMathBox()
        editor:setFocus(true)
        editor:setSizeChangeListener(function(_, width, height)
            if resizing then return end
            if sizing == "callback" then resize(height)
            elseif sizing == "deferred" then
                pendingHeight = height
                timer.start(0.01)
            end
        end)
    end
    gc:setColorRGB(255, 255, 255)
    gc:fillRect(0, 0, platform.window:width(), platform.window:height())
    gc:setColorRGB(0, 0, 0)
    gc:setFont("sansserif", "r", 9)
    gc:drawString(mode, 2, 2, "top")
    gc:drawString("Caret: " .. tostring(caret), 2, 20, "top")
    local printable = raw:gsub("%c", " ")
    for line = 1, 3 do
        gc:drawString(printable:sub((line - 1) * 43 + 1, line * 43), 2, 20 + line * 17, "top")
    end
end
