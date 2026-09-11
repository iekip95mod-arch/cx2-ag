assert((function()
    local paint = on.paint
    local nativeHeight
    local cachedResize = false
    local function refresh() platform.window:invalidate() end
    local function inset(extra)
        nativeHeight = fctEditor.needh + extra
        fctEditor.editor:resize(314, nativeHeight)
        fctEditor.editor:move(2, platform.window:height() - nativeHeight - 1)
        refresh()
    end
    table.insert(menu, {"Editor probe",
        {"Insert negative limit", function()
            fctEditor.editor:setText("")
            fctEditor:fixContent()
            template("limit(,x,-infinity)", 13)
        end},
        {"Restore focus", function() theView:setFocus(fctEditor) refresh() end},
        {"Repeat layout", function() reposME() refresh() end},
        {"Repeat expression", function()
            local expression, caret, selection = fctEditor.editor:getExpressionSelection()
            fctEditor.editor:setExpression(expression, caret, selection)
            refresh()
        end},
        {"Enlarge native editor", function() fctEditor.editor:resize(314, 100) refresh() end},
        {"Enable native border", function() fctEditor.editor:setBorder(1) refresh() end},
        {"Detach size callback", function() fctEditor.editor:setSizeChangeListener(function() end) refresh() end},
        {"Measured height plus one", function() inset(1) end},
        {"Measured height plus two", function() inset(2) end},
        {"Measured height plus four", function() inset(4) end},
        {"Measured height plus eight", function() inset(8) end},
        {"Cache native size", function()
            if cachedResize then return end
            cachedResize = true
            local resize = RichTextEditor.resize
            function RichTextEditor:resize(width, height)
                if self.probeWidth ~= width or self.probeHeight ~= height then
                    self.probeWidth, self.probeHeight = width, height
                    resize(self, width, height)
                end
            end
            refresh()
        end},
        {"Insert after empty expression reset", function()
            fctEditor.editor:setExpression("\\0el {}", 6)
            template("limit(,x,-infinity)", 13)
        end},
    })
    toolpalette.register(menu)
    function on.paint(gc)
        paint(gc)
        if not fctEditor or steps.active then return end
        local _, caret, selection = fctEditor.editor:getExpressionSelection()
        local rows = {
            "Box: " .. fctEditor.x .. "," .. fctEditor.y .. " " .. fctEditor.w .. "x" .. fctEditor.h,
            "Need: " .. tostring(fctEditor.needw) .. "x" .. tostring(fctEditor.needh),
            "Focus: " .. tostring(fctEditor.editor:hasFocus()) .. " caret " .. tostring(caret) .. "," .. tostring(selection),
            "Layout: " .. tostring(theView.historyLayout) .. " force " .. tostring(forcefocus),
            "Native height override: " .. tostring(nativeHeight),
        }
        gc:setColorRGB(255, 255, 255)
        gc:fillRect(0, 32, 315, 90)
        gc:setColorRGB(0, 0, 0)
        gc:setFont("sansserif", "r", 8)
        for index, row in ipairs(rows) do gc:drawString(row, 2, 22 + index * 15, "top") end
    end
    return true
end)())
