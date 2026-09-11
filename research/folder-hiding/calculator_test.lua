local source = (arg[0]:match("^(.*[/\\])") or "") .. "test_app.lua"
local restored, loaded, invalidated, evaluated, drawn
local width, height = 320, 212
local arithmetic = {
    ["2+2"] = "4",
    ["2+3*4"] = "14",
    ["(2+3)*4"] = "20",
    ["1.5/0.5"] = "3",
    ["-2+5"] = "3",
    ["2+2+2"] = "6",
    ["27"] = "27",
    ["8+9+9+1"] = "27",
    ["1+9+8+9"] = "27",
    ["1+9+9+8.0"] = "27",
    ["(1+9+9+8)"] = "27",
    ["20+2"] = "22"
}

platform = { window = {
    invalidate = function() invalidated = invalidated + 1 end,
    width = function() return width end,
    height = function() return height end
} }

math.evalStr = function(input)
    evaluated[#evaluated + 1] = input
    if input == "1+" then return nil, 1 end
    assert(arithmetic[input], "unexpected evaluation: " .. input)
    return arithmetic[input]
end

local gc = {
    setColorRGB = function() end,
    fillRect = function() end,
    setFont = function() end,
    drawLine = function() end,
    getStringWidth = function(_, text) return #text * 6 end,
    drawString = function(_, text) drawn[#drawn + 1] = text end
}

local function reset(bridge, loader)
    restored, loaded, invalidated = 0, 0, 0
    evaluated, drawn, on = {}, {}, {}
    calc_helpers = bridge
    nrequire = loader or function()
        loaded = loaded + 1
        error("module unavailable")
    end
    dofile(source)
end

local function paint()
    drawn = {}
    on.paint(gc)
    return table.concat(drawn, "\n")
end

local function has(text)
    paint()
    for _, line in ipairs(drawn) do
        if line == text then return true end
    end
    return false
end

local function enter(input)
    on.charIn(input)
    on.enterKey()
end

local function restore()
    restored = restored + 1
    return true, "fixture restored"
end

reset({ restore = restore })
enter("1 + 9 + 9 + 8")
assert(restored == 1 and loaded == 0 and #evaluated == 0)
assert(has("27"))
assert(not paint():find("1 + 9 + 9 + 8", 1, true))
assert(not paint():find("fixture", 1, true))
on.enterKey()
assert(restored == 1)
enter("2+2")
assert(restored == 1 and has("4") and has("27"))
assert(not paint():find("1+9+9+8", 1, true))

for _, input in ipairs({ "2+2+2", "27", "8+9+9+1", "1+9+8+9", "1+9+9+8.0", "(1+9+9+8)" }) do
    reset({ restore = restore })
    enter(input)
    assert(restored == 0 and loaded == 0)
    assert(evaluated[1] == input and has(arithmetic[input]))
end

reset(nil, function(name)
    loaded = loaded + 1
    assert(name == "calc_helpers")
    calc_helpers = { restore = restore }
end)
enter("2+2")
assert(loaded == 0 and has("4"))
enter("1+9+9+8")
assert(loaded == 1 and restored == 1 and has("27"))

reset(nil)
enter("2+2")
assert(has("4") and loaded == 0)
enter("1+9+9+8")
assert(has("Unavailable") and loaded == 1)
assert(not paint():find("1+9+9+8", 1, true))

for _, bridge in ipairs({
    { restore = function() return false, "hidden files error" end },
    { restore = function() error("restore failed") end }
}) do
    reset(bridge)
    enter("1+9+9+8")
    assert(has("Unavailable"))
    assert(not paint():find("hidden", 1, true) and not paint():find("restore", 1, true))
end

for _, input in ipairs({ "2+3*4", "(2+3)*4", "1.5/0.5", "-2+5" }) do
    reset(nil)
    enter(input)
    assert(evaluated[1] == input and has(arithmetic[input]) and loaded == 0)
end

reset(nil)
enter("2+3×4")
assert(evaluated[1] == "2+3*4" and has("14"))
enter("1.5÷0.5")
assert(evaluated[2] == "1.5/0.5" and has("3"))
enter("−2+5")
assert(evaluated[3] == "-2+5")

reset(nil)
on.charIn("20+3")
on.backspaceKey()
on.charIn("2")
on.returnKey()
assert(evaluated[1] == "20+2" and has("22"))
on.charIn("2+2")
on.arrowLeft()
on.arrowLeft()
on.charIn("0")
on.arrowRight()
on.arrowRight()
on.enterKey()
assert(evaluated[2] == "20+2")
on.charIn("20+2")
on.arrowLeft()
on.arrowLeft()
on.arrowLeft()
on.deleteKey()
on.enterKey()
assert(evaluated[3] == "2+2")
on.charIn("1+9+9+8")
on.clearKey()
on.enterKey()
assert(loaded == 0 and #evaluated == 3)
on.charIn("1+9+9+8")
on.escapeKey()
assert(has("0"))
enter("1+")
assert(has("Error"))

reset({ restore = restore })
on.charIn("2")
on.charIn("+os.execute('anything')")
on.charIn("+2")
on.enterKey()
assert(evaluated[1] == "2+2" and restored == 0)
on.charIn(string.rep("1", 161))
on.enterKey()
assert(#evaluated == 1)
on.charIn("1+9+9+8\n")
on.enterKey()
assert(restored == 0 and #evaluated == 1)

reset(nil)
for _ = 1, 24 do enter("2+2") end
for _ = 1, 30 do on.arrowUp() end
assert(has("4"))
for _ = 1, 30 do on.arrowDown() end
assert(has("4"))
width, height = 240, 280
on.resize()
assert(has("Calculator") and has("4") and invalidated > 0)

print("Calculator: trigger, missing bridge, arithmetic delegation, editing, and history tests passed")
