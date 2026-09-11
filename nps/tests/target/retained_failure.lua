assert((function()
    local bridge = nps_nspire
    local function arm(mode)
        assert(type(bridge.ui_menu_test_failure) == "function", "Load the retained failure diagnostic module")
        bridge.ui_menu_test_failure(mode)
        bridge.os_msgbox("Renderer failure armed", "Open Actions, then Browse templates.")
    end
    local function report()
        assert(type(bridge.ui_menu_test_status) == "function", "Load the retained failure diagnostic module")
        local status, attempts = bridge.ui_menu_test_status()
        local names = {[0] = "Ready", "Invalid input", "Unavailable", "Resource exhausted", "Assertion failed"}
        bridge.os_msgbox("Renderer recovery", (names[status] or tostring(status)) ..
            "\nAllocation attempts: " .. tostring(attempts) .. "\nGiac 1+1: " .. bridge.caseval("1+1"))
    end
    table.insert(menu, {"Failure probe",
        {"Fail next allocation", function() arm("resource") end},
        {"Fail next assertion", function() arm("assert") end},
        {"Read failure report", report},
    })
    toolpalette.register(menu)
    return true
end)())
