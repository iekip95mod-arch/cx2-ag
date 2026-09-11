assert((function()
    local function show(expression)
        steps.result = {
            outcome = "rendering fixture", status = "rendering fixture",
            input = "UI fixture. No mathematical claim.",
            display_result = expression,
            assumptions = "The complete condition remains readable. CONDITION_END",
            solved = false, has_result = false, answer_only = false, steps = {},
        }
        steps.rows, steps.visibleSteps = {}, {}
        steps.walkthrough, steps.view, steps.stepScroll = "full", "result", 0
        openSteps()
        platform.window:invalidate()
    end
    table.insert(menu, {"Preview probe",
        {"Short fraction", function() show("1/2") end},
        {"Oversized expression", function()
            show(string.rep("(x+1)^(-1)+", 200) .. "FINAL_TERM")
        end},
    })
    toolpalette.register(menu)
    return true
end)())
