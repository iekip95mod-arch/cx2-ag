package.cpath = arg[1] .. ";" .. package.cpath
local nps = require("nps_split")
assert(type(nps.ui_menu_open) == "function", "retained application bridge is required")
local checks = 0
local function check(ok, message)
    checks = checks + 1
    assert(ok, message)
end

if arg[2] then
    local ffi = require("ffi")
    ffi.cdef[[
        void nps_retained_test_fail_after(size_t attempts);
        void nps_retained_test_assert_next(void);
        size_t nps_retained_test_attempts(void);
        int nps_retained_failure(void);
    ]]
    local native = ffi.load(arg[1])
    if arg[2] == "assert" then native.nps_retained_test_assert_next()
    else native.nps_retained_test_fail_after(0) end
    check(not nps.ui_menu_open(320, 240, "Templates", {"Fraction"}), "creation failure reaches Lua normally")
    local expected = arg[2] == "assert" and 4 or 3
    check(native.nps_retained_failure() == expected, "native failure retains its precise classification")
    local attempts = native.nps_retained_test_attempts()
    local encoded, healthy = nps.ui_menu_frame()
    check(encoded == nil and healthy == false, "failed native renderer publishes no image")
    check(not nps.ui_menu_select(1) and not nps.ui_menu_scroll(48), "failed renderer refuses navigation")
    nps.ui_menu_close()
    check(not nps.ui_menu_open(320, 240, "Templates", {"Fraction"}), "poisoned runtime refuses reopening")
    check(native.nps_retained_test_attempts() == attempts, "Lua recovery makes no further native allocations")
else
    check(nps.ui_menu_open(320, 212, "Templates", {"Definite integral"}), "plain row opens")
    local plain = nps.ui_menu_frame()
    check(nps.ui_menu_open(320, 212, "Templates", {"Definite integral"},
          {"Fill the integrand, variable and both bounds."}), "described row opens")
    local described = nps.ui_menu_frame()
    check(plain ~= described, "row descriptions are rendered rather than silently ignored")
    for _, invalid in ipairs({false, 12, {}, {false}, {"One", "Extra"}}) do
        check(not pcall(nps.ui_menu_open, 320, 212, "Templates", {"Definite integral"}, invalid),
              "invalid description arguments raise before replacing the active menu")
        local _, healthy = nps.ui_menu_frame()
        check(healthy, "description argument errors preserve the current scene")
    end
    local inherited_details = setmetatable({"Fill both bounds."}, {__index = function() error("inherited description") end})
    check(nps.ui_menu_open(320, 212, "Templates", {"Definite integral"}, inherited_details),
          "description admission uses raw entries")
    inherited_details[1] = nil
    collectgarbage("collect")
    check(nps.ui_menu_frame(), "descriptions remain owned after their Lua source is released")
    for _, invalid in ipairs({"line\nbreak", "nul\0byte", string.rep("a", 257), "\195\169"}) do
        check(not nps.ui_menu_open(320, 212, "Templates", {"Integral"}, {invalid}),
              "unsupported description content is refused")
    end
    check(nps.ui_menu_open(320, 212, "Templates", {"Integral"}, {""}), "empty descriptions preserve plain rows")
    check(nps.ui_menu_frame(), "plain rows render with an empty description")
    nps.ui_menu_close()
    local labels = {}
    for i = 1, 24 do labels[i] = "Template " .. i .. ", a complete wrapped description" end
    for _, width in ipairs({96, 180, 320}) do
        for iteration = 1, 8 do
            check(nps.ui_menu_open(width, 240, "Templates", labels), "full-capacity menu opens")
            local encoded, healthy = nps.ui_menu_frame()
            check(healthy and #encoded == 20 + width * 240 * 2, "frame has the bounded TI packet size")
            check(encoded:byte(1) + encoded:byte(2) * 256 == width and encoded:byte(5) == 240,
                  "frame records its actual viewport")
            encoded, healthy = nps.ui_menu_frame()
            check(encoded == nil and healthy, "unchanged frame requires no new Lua image")
            check(nps.ui_menu_select(24), "last menu entry remains selectable")
            encoded, healthy = nps.ui_menu_frame()
            check(encoded and healthy, "selection redraws the retained frame")
            for _, delta in ipairs({-240, 240}) do
                local accepted, selected = nps.ui_menu_scroll(delta)
                check(accepted and type(selected) == "number" and selected >= 1 and selected <= #labels and
                      selected == math.floor(selected), "scroll publishes the focused entry through Lua")
            end
            for _, invalid in ipairs({0, 25, 1.5, "1", math.huge, {}}) do
                check(not pcall(nps.ui_menu_select, invalid), "invalid selection is rejected at the boundary")
            end
            for _, invalid in ipairs({-241, 241, 0.5, "1", math.huge, {}}) do
                check(not pcall(nps.ui_menu_scroll, invalid), "invalid scroll is rejected at the boundary")
            end
            nps.ui_menu_close()
            encoded, healthy = nps.ui_menu_frame()
            check(encoded == nil and healthy == false, "closed menu has no published frame")
        end
    end
    check(nps.ui_menu_open(320, 240, "Templates", labels), "valid scene precedes argument refusal")
    nps.ui_menu_frame()
    for _, invalid in ipairs({0, 95, 321, 96.5, "320", math.huge, {}}) do
        check(not pcall(nps.ui_menu_open, invalid, 240, "Templates", labels), "invalid width raises before replacement")
        local _, healthy = nps.ui_menu_frame()
        check(healthy, "argument error preserves the active menu")
    end
    for _, invalid in ipairs({{}, {12}, {"Valid", false}}) do
        check(not pcall(nps.ui_menu_open, 320, 240, "Templates", invalid), "invalid label table raises before replacement")
        local _, healthy = nps.ui_menu_frame()
        check(healthy, "label type error preserves the active menu")
    end
    local inherited = setmetatable({"Fraction"}, {__index = function() error("unexpected inherited lookup") end})
    check(nps.ui_menu_open(320, 240, "Templates", inherited), "native admission reads only raw label entries")
    inherited[1] = nil
    collectgarbage("collect")
    local encoded, healthy = nps.ui_menu_frame()
    check(encoded and healthy, "menu owns its label text after the Lua source is released")
    for _, invalid in ipairs({"", "line\nbreak", "nul\0byte", string.rep("a", 257), "\195\169"}) do
        check(not nps.ui_menu_open(320, 240, "Templates", {invalid}), "unsupported label content is refused")
    end
    check(nps.ui_menu_open(96, 96, "Templates", {string.rep("W", 256)}), "long label is admitted at minimum viewport")
    check(nps.ui_menu_frame(), "oversized wrapped label renders")
    for _ = 1, 20 do
        local accepted, selected = nps.ui_menu_scroll(-240)
        check(accepted and selected == 1, "scrolling an oversized label preserves its selection")
    end
    nps.ui_menu_close()
end
print("retained Lua bridge: " .. checks .. " checks, 0 failed")
