platform.apilevel = "2.0"

local samples, paints, complete, phase = {{}, {}}, 0, true, 1
local retained = {}
local started, problem = pcall(nrequire, "nps_nspire")
if not started then problem = tostring(problem)
elseif type(nps_nspire) ~= "table" or type(nps_nspire.ui_icon) ~= "function" or
       type(nps_nspire.ui_icon_image) ~= "function" then
    problem = "The installed module has no native symbol renderer."
elseif nps_nspire.integrity_status() ~= "verified" then
    problem = "The installed module did not pass its integrity check."
else
    problem = nil
    for icon = 0, 4 do
        local ok, decoded = pcall(function() return image.new(nps_nspire.ui_icon_image(icon)) end)
        if not ok then problem = "TI bitmap decoding failed." break end
        retained[icon] = decoded
    end
end

local function summary(index)
    if #samples[index] < 64 then return string.format("%d / 64 samples", #samples[index]) end
    local sorted = {}
    for i, sample in ipairs(samples[index]) do sorted[i] = sample end
    table.sort(sorted)
    return string.format("median %d ms, p95 %d ms", sorted[32], sorted[61])
end

function on.paint(gc)
    local w, h = platform.window:width(), platform.window:height()
    gc:setColorRGB(255, 255, 255)
    gc:fillRect(0, 0, w, h)
    gc:setColorRGB(37, 57, 87)
    gc:setFont("sansserif", "b", 11)
    gc:drawString("Native symbol rendering", 6, 4, "top")
    gc:setFont("sansserif", "r", 9)
    if problem then gc:drawString(problem, 6, 28, "top") return end
    local rows = math.max(1, math.min(8, math.floor((h - 100) / 18)))
    local start = timer.getMilliSecCounter()
    for row = 0, rows - 1 do
        for icon = 0, 4 do
            if phase == 1 then
                complete = nps_nspire.ui_icon(gc, icon, 14 + icon * 28, 32 + row * 18, w, h) and complete
            else gc:drawImage(retained[icon], 14 + icon * 28, 32 + row * 18) end
        end
    end
    local elapsed = timer.getMilliSecCounter() - start
    paints = paints + 1
    if paints > 4 and #samples[phase] < 64 then samples[phase][#samples[phase] + 1] = elapsed end
    gc:setColorRGB(37, 57, 87)
    gc:drawString(string.format("%d icons / paint", rows * 5), 6, h - 61, "top")
    gc:drawString("Callbacks: " .. summary(1), 6, h - 46, "top")
    gc:drawString("Images: " .. summary(2), 6, h - 31, "top")
    gc:drawString(complete and "All symbol draws completed" or "FAILED: incomplete symbol draw", 6, h - 16, "top")
end

function on.construction()
    timer.start(0.02)
end

function on.timer()
    if problem then timer.stop() return end
    if #samples[phase] >= 64 then
        if phase == 2 then timer.stop() return end
        phase, paints = 2, 0
    end
    platform.window:invalidate()
end

function on.save()
    return {samples = samples, paints = paints, complete = complete}
end
