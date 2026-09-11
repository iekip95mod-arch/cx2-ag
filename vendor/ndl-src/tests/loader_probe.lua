platform.apilevel = "2.0"

local cases = {
    {"Header", "ld_diag_bad", "zehn.header.format (code 1, bytes 0)"},
    {"Decompression", "ld_diag_zlib", "zehn.decompress (code -3, bytes 16)"},
    {"Program entry", "ld_diag_entry", "program.entry (code 17, bytes 0)"}
}
local responses, selected = {}, 1

function on.enterKey()
    if #responses > 0 then return end
    for i, case in ipairs(cases) do
        local ok, response = pcall(nrequire, case[2])
        responses[i] = {text = tostring(response), passed = not ok and
            string.find(tostring(response), case[3], 1, true) ~= nil}
    end
    platform.window:invalidate()
end

function on.arrowKey(key)
    if key == "down" then selected = math.min(#cases, selected + 1) end
    if key == "up" then selected = math.max(1, selected - 1) end
    platform.window:invalidate()
end

function on.paint(gc)
    gc:setColorRGB(255, 255, 255)
    gc:fillRect(0, 0, platform.window:width(), platform.window:height())
    gc:setColorRGB(20, 35, 60)
    gc:setFont("sansserif", "b", 11)
    gc:drawString("Loader diagnostics", 6, 4, "top")
    gc:setFont("sansserif", "r", 10)
    for i, case in ipairs(cases) do
        local response = responses[i]
        local status = response and (response.passed and "PASS" or "FAIL") or "waiting"
        gc:drawString((selected == i and "> " or "  ") .. case[1] .. ": " .. status,
            6, 24 + (i - 1) * 17, "top")
    end
    local text = responses[selected] and responses[selected].text or "Enter runs once. Up and Down show each complete response."
    local line, y = "", 83
    for word in text:gmatch("%S+") do
        local nextLine = line == "" and word or line .. " " .. word
        if gc:getStringWidth(nextLine) > platform.window:width() - 12 then
            gc:drawString(line, 6, y, "top")
            y = y + 15
            line = word
        else line = nextLine end
    end
    gc:drawString(line, 6, y, "top")
end

function on.save() return {responses = responses, selected = selected} end
function on.restore(state)
    if type(state) == "table" then
        responses = state.responses or {}
        selected = state.selected or 1
    end
end
