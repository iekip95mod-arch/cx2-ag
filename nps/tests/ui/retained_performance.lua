local checks = 0
local function check(ok, message)
    checks = checks + 1
    assert(ok, message)
end

local function document(fault, containers, comparison)
    local clock, running, menu, dirty = 0, false, false, false
    local viewport_width, viewport_height = 320, 212
    local opens, closes, solves, decodes = 0, 0, 0, 0
    local formats = 0
    local menu_profiles = {}
    local batches, container_loads = {}, 0
    local env = setmetatable({on = {}, platform = {window = {}}}, {__index = _G})
    env.platform.window.width = function() return viewport_width end
    env.platform.window.height = function() return viewport_height end
    env.platform.window.invalidate = function() end
    env.timer = {
        getMilliSecCounter = function() clock = clock + 2 return clock end,
        start = function() running = true end,
        stop = function() running = false end,
    }
    env.image = {new = function(encoded)
        assert(encoded == "frame")
        decodes = decodes + 1
        if fault == "decode" then error("decoder failed " .. string.rep("W", 80)) end
        return {}
    end}
    env.nps_nspire = {
        math_display = function(expression)
            assert(expression == "(1 * (2^(-1)))")
            formats = formats + 1
            return fault == "format" and "wrong fraction" or "(1 / 2)"
        end,
        ui_menu_open = function(width, height, title, labels, descriptions)
            assert(width == 320 and height == 212 and #labels == 24)
            assert(not menu)
            menu_profiles[#menu_profiles + 1] = {labels = table.concat(labels, "|"), described = descriptions ~= nil}
            if descriptions then
                assert(#descriptions == 24)
                for _, description in ipairs(descriptions) do assert(#description > 0 and #description <= 256) end
                if fault == "descriptions" then return false end
            end
            opens, menu, dirty = opens + 1, true, true
            return true
        end,
        ui_menu_frame = function()
            assert(menu)
            if fault == "frame" then return nil, false end
            if dirty then dirty = false return "frame", true end
            return nil, true
        end,
        ui_menu_select = function(index)
            assert(menu and index == 24)
            dirty = true
            return true
        end,
        ui_menu_close = function()
            if menu then closes = closes + 1 end
            menu = false
        end,
        walkthrough = function(expression, variable, mode)
            assert(variable == "x" and mode == "exact")
            solves = solves + 1
            if fault == "clock" then clock = clock - 100 end
            return {solved = true, agrees = fault ~= "agreement", result = fault == "answer" and "99" or
                ({["diff(x,x)"] = "1", ["int(x,x,0,2)"] = "2", ["limit(sqrt(x),x,0,1)"] = "0"})[expression]}
        end,
    }
    env.nps_container_probe = {run = function(kind, frames, seed)
        assert(kind >= 1 and kind <= 6 and frames == 10000)
        batches[#batches + 1] = {kind = kind, seed = seed}
        if fault == "clock" then clock = clock - 100 end
        if fault == "run" then error("container failed") end
        return fault == "checksum" and 0 or 221120000, fault == "storage" and 0 or 1280, kind % 3 == 0 and 1280 or 0
    end}
    env.nrequire = function(name)
        if name == "nps_container_probe" then container_loads = container_loads + 1 end
        if fault == "load" or (fault == "container_load" and name == "nps_container_probe") then error("loader failed") end
    end
    setfenv(assert(loadfile("tests/target/retained_app_probe.lua")), env)()
    check(type(env.on.charIn) == "function", "P starts the bounded timing mode")
    for attempt = 1, fault == "repeat" and 2 or 1 do
        env.on.charIn(containers and "c" or comparison and "d" or "p")
        local ticks = 0
        while running do
            ticks = ticks + 1
            check(ticks <= (comparison and 128 or 100), "timing mode terminates within its declared sample count")
            env.on.timer()
            if fault == "cancel" then env.on.escapeKey() end
        end
    end
    local painted, fits = {}, true
    local function width(text)
        local wide = select(2, text:gsub("W", ""))
        return #text * 6 + wide * 5
    end
    local gc = {
        setColorRGB = function() end, fillRect = function() end, setFont = function() end,
        getStringWidth = function(_, text) return width(text) end,
        drawString = function(_, text, x, y)
            fits = fits and x >= 0 and x + width(text) <= viewport_width and y + 12 <= viewport_height
            painted[#painted + 1] = text
        end,
    }
    for _, requested in ipairs({96, 180, 320}) do
        viewport_width = requested
        for i = 1, 60 do env.on.arrowUp() end
        env.on.paint(gc)
        for i = 1, 60 do env.on.arrowDown() env.on.paint(gc) end
        check(fits and #painted > 0, "scrolled diagnostic text fits each viewport")
        painted, viewport_height = {}, 10000
        env.on.paint(gc)
        local expected = table.concat(env.on.save().rows):gsub("[\128-\255]", "?"):gsub("%c", " ")
        check(table.concat(painted) == expected, "measured wrapping preserves every diagnostic character")
        viewport_height = 212
    end
    return env.on.save(), menu, opens, closes, solves, decodes, batches, container_loads, formats, menu_profiles
end

local saved, menu, opens, closes, solves, decodes, batches, container_loads, formats = document()
check(saved.completed and not menu, "successful run closes the retained menu")
check(opens == 8 and closes == 8 and solves == 24 and decodes == 16 and formats == 800,
      "one warmup and seven measured cycles execute each real operation")
local workloads = 0
for _, samples in pairs(saved.timings) do
    workloads = workloads + 1
    check(#samples == 7, "warmup is excluded from every reported workload")
    for _, elapsed in ipairs(samples) do check(elapsed == 2, "elapsed milliseconds are measured") end
end
check(workloads == 12 and saved.load_ms == 2, "load time stays separate from the twelve workloads")
for _, fault in ipairs({"load", "frame", "decode", "answer", "agreement", "clock", "cancel", "format"}) do
    saved, menu = document(fault)
    check(not saved.completed and not menu, fault .. " cannot publish completed timings or retain a menu")
    local text = table.concat(saved.rows, " ")
    check(text:find(fault == "cancel" and "Cancelled" or "FAILED", 1, true), fault .. " is visible")
end
saved, menu, opens, closes, solves, decodes, batches, container_loads, formats = document(nil, true)
check(saved.completed and saved.container_mode and #batches == 48, "six containers complete one warmup and seven samples")
check(opens == 0 and closes == 0 and solves == 0 and decodes == 0 and formats == 0,
      "container mode excludes calculus and rendering work")
for cycle = 1, 8 do
    for stage = 1, 6 do
        local batch = batches[(cycle - 1) * 6 + stage]
        check(batch.kind == (stage + cycle - 2) % 6 + 1 and batch.seed == cycle * 17 % 200,
            "container order rotates and every batch receives its cycle seed")
    end
end
local container_count = 0
for label, samples in pairs(saved.timings) do
    container_count = container_count + 1
    check(#samples == 7 and saved.container_storage[label].object_bytes == 1280, "container samples exclude warmup and retain storage")
    for _, elapsed in ipairs(samples) do check(elapsed == 2, "container measurements use elapsed milliseconds") end
end
check(container_count == 6, "every container retains separate timings")
saved, menu, opens, closes, solves, decodes, batches, container_loads = document("repeat", true)
check(saved.completed and #batches == 96 and container_loads == 1, "repeated timings reuse the loaded module")
for _, fault in ipairs({"container_load", "run", "checksum", "storage", "clock", "cancel"}) do
    saved = document(fault, true)
    check(not saved.completed, fault .. " refuses completed container timings")
    check(table.concat(saved.rows, " "):find(fault == "cancel" and "Cancelled" or "FAILED", 1, true), fault .. " is visible")
end
local profiles
saved, menu, opens, closes, solves, decodes, batches, container_loads, formats, profiles = document(nil, false, true)
check(saved.completed and saved.comparison_mode and not menu, "matched menu comparison completes and releases its menu")
check(opens == 16 and closes == 16 and decodes == 32 and solves == 0 and formats == 0,
      "each menu profile runs one warmup and seven measured cycles without calculus work")
local compared = 0
for name, samples in pairs(saved.timings) do
    compared = compared + 1
    check(name:find("Plain ", 1, true) == 1 or name:find("Described ", 1, true) == 1,
          "menu profiles retain distinct timing labels")
    check(#samples == 7, "each matched comparison excludes its warmup")
end
check(compared == 16, "both menu profiles measure all eight retained operations")
for cycle = 1, 8 do
    local first, second = profiles[cycle * 2 - 1], profiles[cycle * 2]
    check(first.described == (cycle % 2 == 0) and second.described ~= first.described,
          "menu creation order alternates each cycle")
    check(first.labels == second.labels, "both menu profiles receive identical primary labels")
end
for _, fault in ipairs({"load", "frame", "decode", "descriptions", "cancel"}) do
    saved, menu = document(fault, false, true)
    check(not saved.completed and not menu, fault .. " cannot publish completed comparison timings")
    check(table.concat(saved.rows, " "):find(fault == "cancel" and "Cancelled" or "FAILED", 1, true),
          fault .. " remains visible during row comparison")
end
print("retained performance probe: " .. checks .. " checks, 0 failed")
