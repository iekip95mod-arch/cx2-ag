platform.apilevel = "2.0"
local rows = {"Enter: version. Tab: heap. P: UI timings. D: row comparison. C: containers."}
local scroll = 0
local load_start = timer.getMilliSecCounter()
local loaded, problem = pcall(nrequire, "nps_nspire")
local load_ms = timer.getMilliSecCounter() - load_start
local timings, completed, measuring = {}, false, false
local container_mode, container_storage = false, {}
local comparison_mode, current_descriptions = false, nil
local container_loaded = false
local stage, cycle, encoded, decoded = 1, 1, nil, nil
local labels = {}
for i = 1, 24 do labels[i] = "Template " .. i .. ", a complete wrapped description" end
local comparison_labels, descriptions = {}, {}
for i = 1, 24 do
    comparison_labels[i] = "Template " .. i
    descriptions[i] = "Enter the expression, then the variable and bounds. Every argument remains editable."
end

local function frame()
    local healthy
    encoded, healthy = nps_nspire.ui_menu_frame()
    assert(healthy and type(encoded) == "string", "retained frame unavailable")
end
local function decode()
    decoded = assert(image.new(encoded), "TI image unavailable")
    encoded = nil
end
local function solve(expression, expected)
    local reply = nps_nspire.walkthrough(expression, "x", "exact")
    assert(reply.solved and reply.agrees == true and reply.result == expected,
        expression .. ": unexpected answer or missing verification")
end
local calculus_workloads = {
    {"Menu create", function()
        assert(nps_nspire.ui_menu_open(platform.window:width(), platform.window:height(), "Templates",
            comparison_mode and comparison_labels or labels, current_descriptions),
            "retained menu unavailable")
    end},
    {"Initial render + encode", frame},
    {"Initial TI image decode", decode},
    {"Select final item", function() assert(nps_nspire.ui_menu_select(24), "selection failed") end},
    {"Changed render + encode", frame},
    {"Changed TI image decode", decode},
    {"Unchanged frame", function()
        local unchanged, healthy = nps_nspire.ui_menu_frame()
        assert(healthy and unchanged == nil, "unchanged frame rebuilt")
    end},
    {"Menu close", function() nps_nspire.ui_menu_close() decoded = nil end},
    {"Derivative + Giac check", function() solve("diff(x,x)", "1") end},
    {"Integral + Giac check", function() solve("int(x,x,0,2)", "2") end},
    {"Root limit + Giac check", function() solve("limit(sqrt(x),x,0,1)", "0") end},
    {"100 fraction formats", function()
        for i = 1, 100 do
            assert(nps_nspire.math_display("(1 * (2^(-1)))") == "(1 / 2)", "fraction formatting failed")
        end
    end},
}
local comparison_workloads = {}
for _, profile in ipairs({"Plain", "Described"}) do
    for index = 1, 8 do
        local workload = calculus_workloads[index]
        comparison_workloads[#comparison_workloads + 1] = {profile .. " " .. workload[1], workload[2], profile == "Described"}
    end
end
local container_workloads = {}
for index, label in ipairs({"ETL fresh", "EASTL fresh", "STL fresh", "ETL reused", "EASTL reused", "STL reused"}) do
    local kind, name = index, label
    container_workloads[index] = {label, function()
        local checksum, object_bytes, heap_bytes = nps_container_probe.run(kind, 10000, cycle * 17 % 200)
        assert(checksum == 221120000, "container commands differ")
        assert(type(object_bytes) == "number" and object_bytes > 0 and
            type(heap_bytes) == "number" and heap_bytes >= 0, "container storage unavailable")
        container_storage[name] = {object_bytes = object_bytes, heap_capacity_bytes = heap_bytes}
    end}
end
local workloads = calculus_workloads

local function stop(message)
    timer.stop()
    measuring = false
    if loaded then pcall(nps_nspire.ui_menu_close) end
    encoded, decoded = nil, nil
    rows[#rows + 1] = message
    platform.window:invalidate()
end

function on.charIn(ch)
    if (ch ~= "p" and ch ~= "P" and ch ~= "c" and ch ~= "C" and ch ~= "d" and ch ~= "D") or measuring then return end
    rows, scroll, timings, completed = {}, 0, {}, false
    container_mode, container_storage = ch == "c" or ch == "C", {}
    comparison_mode, current_descriptions = ch == "d" or ch == "D", nil
    workloads = container_mode and container_workloads or comparison_mode and comparison_workloads or calculus_workloads
    rows[1] = "nrequire call: " .. load_ms .. " ms"
    if not loaded then return stop("FAILED load: " .. tostring(problem)) end
    if container_mode then
        if not container_loaded then
            local ok, failure = pcall(nrequire, "nps_container_probe")
            if not ok then return stop("FAILED container load: " .. tostring(failure)) end
            container_loaded = true
        end
        rows[1] = "64 commands, 10000 frames per batch."
    end
    stage, cycle, measuring = 1, 1, true
    rows[2] = "One warmup, seven samples. Escape cancels."
    if comparison_mode then rows[3] = "24 equal titles. Descriptions only differ. Order alternates." end
    timer.start(0.05)
    platform.window:invalidate()
end

function on.timer()
    if not measuring then return end
    local index = container_mode and ((stage + cycle - 2) % #workloads + 1) or stage
    if comparison_mode and cycle % 2 == 0 then index = (stage + 7) % 16 + 1 end
    local workload = workloads[index]
    current_descriptions = comparison_mode and workload[3] and descriptions or nil
    local start = timer.getMilliSecCounter()
    local ok, failure = pcall(workload[2])
    local elapsed = timer.getMilliSecCounter() - start
    if not ok then return stop("FAILED " .. workload[1] .. ": " .. tostring(failure)) end
    if elapsed < 0 then return stop("FAILED clock moved backwards") end
    if cycle > 1 then
        local samples = timings[workload[1]] or {}
        samples[#samples + 1] = elapsed
        timings[workload[1]] = samples
    end
    stage = stage + 1
    if stage <= #workloads then return end
    stage, cycle = 1, cycle + 1
    if cycle <= 8 then return end
    for _, entry in ipairs(workloads) do
        local samples = timings[entry[1]]
        table.sort(samples)
        rows[#rows + 1] = entry[1] .. ": " .. samples[1] .. "/" .. samples[4] .. "/" .. samples[7] .. " ms"
        if container_mode then
            local storage = container_storage[entry[1]]
            rows[#rows + 1] = "Object " .. storage.object_bytes .. " B. Heap capacity " .. storage.heap_capacity_bytes .. " B."
        end
    end
    completed = true
    stop(container_mode and "Finished: min/median/max per batch. Includes construction, population and checksum. No display work." or
        "Finished: min/median/max. Display time excluded.")
end

function on.escapeKey()
    if measuring then stop("Cancelled") end
end
local function record(label, operation)
    local ok, reply = pcall(operation)
    rows[#rows + 1] = label .. ": " .. (ok and tostring(reply) or "ERROR " .. tostring(reply))
end
local function heap()
    local available = nps_nspire.heap_free()
    return tostring(available.total_kb) .. "/" .. tostring(available.largest_kb)
end
local function inspect(heap_first)
    if measuring then return end
    rows, scroll = {}, 0
    if not loaded then rows = {"LOAD FAILED: " .. tostring(problem)}
    else
        if heap_first then record("initial heap", heap) end
        record("version before heap", function() return nps_nspire.caseval("version()") end)
        record("heap", heap)
        record("version after heap", function() return nps_nspire.caseval("version()") end)
        record("arithmetic", function() return nps_nspire.caseval("1+1") end)
        record("TI limit", function() return nps_nspire.caseval("lim(1/x,x,-\226\136\158)") end)
        record("native limit", function()
            local result = nps_nspire.walkthrough("lim(1/x,x,-\226\136\158)", "x", "exact")
            return result.status .. ", " .. tostring(result.result) .. ", " .. tostring(result.agrees)
        end)
    end
    platform.window:invalidate()
end
function on.enterKey() inspect(false) end
function on.tabKey() inspect(true) end
function on.arrowDown() scroll = scroll + 1 platform.window:invalidate() end
function on.arrowUp() scroll = math.max(0, scroll - 1) platform.window:invalidate() end
function on.paint(gc)
    gc:setColorRGB(255, 255, 255)
    gc:fillRect(0, 0, platform.window:width(), platform.window:height())
    gc:setColorRGB(0, 0, 0)
    gc:setFont("sansserif", "r", 9)
    local lines = {}
    local width = platform.window:width() - 4
    for _, row in ipairs(rows) do
        local printable = row:gsub("[\128-\255]", "?"):gsub("%c", " ")
        local offset = 1
        while offset <= #printable do
            local low, high, count = 1, #printable - offset + 1, 1
            while low <= high do
                local middle = math.floor((low + high) / 2)
                if gc:getStringWidth(printable:sub(offset, offset + middle - 1)) <= width then
                    count, low = middle, middle + 1
                else high = middle - 1 end
            end
            lines[#lines + 1] = printable:sub(offset, offset + count - 1)
            offset = offset + count
        end
    end
    local visible = math.floor(platform.window:height() / 15)
    scroll = math.min(scroll, math.max(0, #lines - visible))
    for index = scroll + 1, math.min(#lines, scroll + visible) do
        gc:drawString(lines[index], 2, (index - scroll - 1) * 15, "top")
    end
end
function on.save()
    return {rows = rows, timings = timings, completed = completed, load_ms = load_ms,
        container_mode = container_mode, container_storage = container_storage, comparison_mode = comparison_mode}
end
