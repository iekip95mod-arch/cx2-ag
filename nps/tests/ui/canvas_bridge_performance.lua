local nps = assert(package.loadlib(assert(arg[1]), "luaopen_nps_split"))()
if jit then jit.off() end
local checksum = 0
local rectangles = 0
local gc = {
    setColorRGB = function(_, red, green, blue) checksum = checksum + red + green + blue end,
    fillRect = function(_, x, y, width, height)
        checksum = checksum + x + y + width + height
        rectangles = rectangles + 1
    end,
}
local function direct_panel()
    gc:setColorRGB(255, 255, 255)
    gc:fillRect(0, 0, 320, 240)
    gc:setColorRGB(37, 57, 87)
    gc:fillRect(0, 0, 320, 18)
    gc:setColorRGB(242, 244, 247)
    gc:fillRect(0, 222, 320, 18)
    gc:setColorRGB(190, 196, 205)
    gc:fillRect(0, 222, 320, 1)
end
local function native_panel()
    assert(nps.ui_panel(gc, 320, 240, 18, 18))
end
local function measure(label, paint)
    local samples = {}
    for sample = 1, 7 do
        checksum, rectangles = 0, 0
        local started = os.clock()
        for frame = 1, 2000 do paint() end
        samples[sample] = (os.clock() - started) * 1000000 / 2000
        assert(rectangles == 8000, "panel lost a rectangle")
        assert(checksum == 8542000, "panel changed geometry or colors")
    end
    table.sort(samples)
    print(string.format("%s: host interpreted Lua, us/frame min=%.3f median=%.3f max=%.3f",
                        label, samples[1], samples[4], samples[7]))
end
measure("Lua panel", direct_panel)
measure("Native panel through Lua callbacks", native_panel)
