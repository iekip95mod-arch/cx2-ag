platform.apiLevel = '2.0'
local editor, generation, phase, ticks = nil, 0, 0, 0
local states, pending, paints = {}, false, 0
local long = '(x^2+1)*(cos(x)-sin(x))+(2*x)*(sin(x)+cos(x))'
local cases = {
    { name = 'initial', font = 9, width = 250, expr = long },
    { name = 'same text, font 7', font = 7, width = 250, expr = long },
    { name = 'same text, width 180', font = 7, width = 180, expr = long },
    { name = 'short expression', font = 9, width = 250, expr = '0' },
    { name = 'long expression', font = 9, width = 250, expr = long },
    { name = 'burst short', font = 9, width = 180, expr = '0' },
    { name = 'burst long', font = 9, width = 180, expr = long }
}
local function configure(index)
    phase, ticks = index, 0
    generation = generation + 1
    local request, case = generation, cases[index]
    local state = { calls = 0, width = 0, height = 0, paints = paints }
    states[index] = state
    pending = true
    editor:setSizeChangeListener(nil)
    editor:setFontSize(case.font)
    editor:resize(case.width, 13)
    editor:setSizeChangeListener(function(_, width, height)
        if generation == request then
            state.calls, state.width, state.height = state.calls + 1, width, height
            state.sync = state.configuring == true
            pending = false
            platform.window:invalidate()
        end
    end)
    state.configuring = true
    editor:setExpression('\\0el {' .. case.expr .. '}', 0)
    state.configuring = false
    editor:move(-10000, -10000)
    editor:setVisible(false)
end
function on.paint(gc)
    paints = paints + 1
    if not editor then
        editor = D2Editor.newRichText()
        editor:setBorder(0)
        editor:setReadOnly(true)
        editor:setFocus(false)
        configure(1)
        timer.start(0.2)
    end
    gc:setColorRGB(255, 255, 255)
    gc:fillRect(0, 0, 320, 240)
    gc:setColorRGB(0, 0, 0)
    gc:setFont('sansserif', 'r', 9)
    gc:drawString('Callback timing probe B0809', 3, 3, 'top')
    gc:setFont('sansserif', 'r', 7)
    for index, case in ipairs(cases) do
        local state = states[index]
        local details = state and (state.calls .. ' calls, ' .. state.width .. 'x' .. state.height ..
            ', sync ' .. tostring(state.sync)) or 'waiting'
        gc:drawString(index .. '. ' .. case.name, 3, 23 + (index - 1) * 24, 'top')
        gc:drawString(details, 3, 34 + (index - 1) * 24, 'top')
    end
    gc:drawString(phase == #cases and ticks >= 10 and 'Finished' or 'Running', 3, 203, 'top')
end
function on.timer()
    ticks = ticks + 1
    if ticks >= 10 and phase < #cases then
        configure(phase + 1)
        if phase == 6 then configure(7) end
    end
    if ticks >= 10 and phase == #cases then timer.stop() end
    platform.window:invalidate()
end
