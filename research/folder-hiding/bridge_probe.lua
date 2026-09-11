local lines = { "Native bridge probe", "Press Enter to restore" }

function on.enterKey()
    local loaded, detail = pcall(nrequire, "calc_helpers")
    lines = { "load: " .. tostring(loaded), tostring(detail), "table: " .. type(calc_helpers) }
    if type(calc_helpers) == "table" and type(calc_helpers.restore) == "function" then
        local called, restored, message = pcall(calc_helpers.restore)
        lines[#lines + 1] = "call: " .. tostring(called)
        lines[#lines + 1] = "restored: " .. tostring(restored)
        lines[#lines + 1] = tostring(message)
    end
    platform.window:invalidate()
end

function on.paint(gc)
    gc:setColorRGB(0, 0, 0)
    gc:setFont("sansserif", "r", 8)
    for i, line in ipairs(lines) do
        gc:drawString(line, 4, 10 + i * 22, "top")
    end
end
