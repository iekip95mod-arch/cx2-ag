platform.apilevel = "2.0"
local loaded, problem = pcall(nrequire, "nps_retained_probe")
local retained, pool, revision, selected
local frame_ms, decode_ms, stats = 0, 0, false
if loaded then problem = nil end

function on.paint(gc)
    if problem then
        gc:setColorRGB(255, 255, 255)
        gc:fillRect(0, 0, platform.window:width(), platform.window:height())
        gc:setColorRGB(0, 0, 0)
        gc:setFont("sansserif", "r", 9)
        gc:drawString(tostring(problem), 4, 4, "top")
        return
    end
    local start = timer.getMilliSecCounter()
    local ok, encoded, used, version = pcall(nps_retained_probe.frame,
        platform.window:width(), platform.window:height())
    local elapsed = timer.getMilliSecCounter() - start
    if not ok then problem = encoded platform.window:invalidate() return end
    pool, revision = used, version
    if encoded then
        frame_ms = elapsed
        start = timer.getMilliSecCounter()
        local decoded, icon = pcall(image.new, encoded)
        decode_ms = timer.getMilliSecCounter() - start
        if not decoded then problem = icon platform.window:invalidate() return end
        retained = icon
    end
    if retained then gc:drawImage(retained, 0, 0) end
    if selected then
        gc:setColorRGB(255, 255, 255)
        gc:fillRect(0, 0, platform.window:width(), 20)
        gc:setColorRGB(37, 57, 87)
        gc:setFont("sansserif", "r", 9)
        gc:drawString(selected, 6, 2, "top")
    end
    if stats then
        gc:setColorRGB(255, 255, 255)
        gc:fillRect(0, 0, platform.window:width(), platform.window:height())
        gc:setColorRGB(37, 57, 87)
        gc:setFont("sansserif", "r", 10)
        local lines = {"LVGL qualification", "Render + encode: " .. frame_ms .. " ms",
            "TI image decode: " .. decode_ms .. " ms", "LVGL pool used: " .. pool .. " bytes",
            "Frame revision: " .. revision, "P returns to the controls"}
        for i, line in ipairs(lines) do gc:drawString(line, 6, 6 + (i - 1) * 24, "top") end
    end
end

function on.arrowDown()
    if not problem then nps_retained_probe.move(1) selected = nil platform.window:invalidate() end
end
function on.arrowUp()
    if not problem then nps_retained_probe.move(-1) selected = nil platform.window:invalidate() end
end
function on.enterKey()
    if not problem then selected = nps_retained_probe.choose() platform.window:invalidate() end
end
function on.escapeKey()
    selected, stats = nil, false
    platform.window:invalidate()
end
function on.charIn(ch)
    if ch == "p" or ch == "P" then stats = not stats platform.window:invalidate() end
end
function on.save()
    return {pool = pool, revision = revision, frame_ms = frame_ms, decode_ms = decode_ms, selected = selected}
end
