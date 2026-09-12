---------------------
-- "Ki V3" : the Ki V1 shell (KhiCAS on our giac 1.9 build) plus step-by-step modes.
-- Everything up to the "Ki V3" section at the end is the Ki V1 document as it shipped; the step
-- modes are appended there and hook in at three places marked "Ki V3" (enterHandler, addME,
-- the menu). See .Internal/ki-v3.md.
---------------------
-- "KhiCAS" : TI-Nspire Giac UI in Nspire-Lua
-- Version 1.05 - 18/06/2014
-- GPL v3 License
-- http://tiplanet.org/forum/viewtopic.php?f=43&t=14800
---------------------
-- Giac CAS engine by Bernard Parisse
-- http://www-fourier.ujf-grenoble.fr/~parisse/
---------------------
-- The UI is highly based on Xavier 'critor' Andréani's "SuperSpire" 
-- See http://tiplanet.org/forum/viewtopic.php?t=13851&p=157015
-------------------
-- ETK GUI Lib by Jim Bauwens and Adrien "Adriweb" Bertrand
-- Additions/mods by Xavier
-- some more little changes by Adrien
-- (borders, horiz. lines, readonly, copy/paste, errHandler etc.)
-------------------

platform.apilevel = '2.0'

hasGiac = pcall(nrequire, "luagiac")
if not hasGiac then
	luagiac = { caseval = function(str) return math.evalStr(str) end }
	print("Giac module not loaded ! Fallback on Nspire's math engine.")
end

-- Host channel probe. The USB link stays up while this Lua UI runs but dies under the native
-- KhiCAS shell, so this is the layer a host conversation has to live in. The Nspire Lua sandbox
-- exposes no file I/O, so Giac has to do it. Which spellings actually work is unknown, hence a
-- probe that records what happened rather than a channel written on assumptions.
-- Off while the startup restart is being bisected. pcall catches a Lua error but not a native fault
-- inside Giac, so this block can take the calculator down and did not survive as a suspect.
local CHAN_PROBE = false

-- Read from Giac on first paint rather than hardcoded, so the label cannot drift from the binary
-- that is actually loaded. Not read at load time on purpose: a caseval before the UI is up is what
-- the channel probe was doing when it took the calculator down.
giacVer = nil

function giacLabel()
	if giacVer == nil then
		giacVer = ""
		if hasGiac then
			local ok, s = pcall(luagiac.caseval, "version()")
			if ok and type(s) == "string" then
				-- "giac for TI Nspire CX 1.9.0, (c) ...", so the version is the last word before the
				-- first comma. Scanned rather than pattern matched.
				local stop = string.find(s, ",", 1, true) or (string.len(s) + 1)
				local start = 1
				for i = stop - 1, 1, -1 do
					if string.sub(s, i, i) == " " then
						start = i + 1
						break
					end
				end
				giacVer = string.sub(s, start, stop - 1)
			end
		end
	end
	if giacVer == "" then return "Giac :" end
	return "Giac " .. giacVer .. " :"
end

chanProbe = {}

local function chanTry(label, cmd)
	local ok, res = pcall(luagiac.caseval, cmd)
	chanProbe[#chanProbe + 1] = label .. " => " .. (ok and tostring(res) or "PCALL FAIL")
end

-- The corner is right aligned, so an unbounded status walks off the left edge rather than clipping.
local function paintableFailure(message)
	if #message > 50 then return message:sub(1, 47) .. "..." end
	return message
end

if hasGiac and CHAN_PROBE then
	for _, ext in ipairs({"", ".tns"}) do
		local out = "/documents/ndl/chan-out" .. ext
		local inp = "/documents/ndl/chan-in.txt" .. ext
		chanTry("write" .. ext,
			'f:=fopen("' .. out .. '");fprint(f,Unquoted,"write ok");fclose(f)')
		chanTry("read" .. ext,
			'csv2gen("' .. inp .. '",",",char(10),".")')
	end
	chanTry("eval", "normal(diff(x^2*sin(x),x))")

	-- Report through every write spelling, since the point is to find out which one lands.
	local body = table.concat(chanProbe, " | "):gsub('"', "'")
	for _, ext in ipairs({"", ".tns"}) do
		pcall(luagiac.caseval,
			'f:=fopen("/documents/ndl/chan-probe' .. ext .. '");' ..
			'fprint(f,Unquoted,"' .. body .. '");fclose(f)')
	end
end

-- localized useful functions

local mathmin = math.min
local mathmax = math.max
local mathrandom = math.random

ts = 0.0


-- ETK Stuff

--------------------------------------------------------------------- View: Widgets & Events manager

defaultFocus = nil

View = class()

function View:init(window)
	self.window = window
	self.widgetList = {}
	self.focusList = {}
	self.currentFocus = 0
	self.currentCursor = "default"

	-- Previous location of mouse pointer
	self.prev_mousex = 0
	self.prev_mousey = 0
end


function View:invalidate()
	self.window:invalidate()
end

function View:setCursor(cursor)
	if cursor ~= self.currentCursor then
		self.currentCursor = cursor
		self:invalidate()
	end
end


function View:add(o)
	table.insert(self.widgetList, o)
	self:repos(o)
	if o.acceptsFocus then
		table.insert(self.focusList, 1, o)
		if self.currentFocus > 0 then
			self.currentFocus = self.currentFocus + 1
		end
	end
	return o
end

function View:remove(o)
	if self:getFocus() == o then
		o:releaseFocus()
	end
	local i = 1
	local f = 0
	local oldf
	while i <= #self.focusList do
		if self.focusList[i] == o then
			f = i
		end
		i = i + 1
	end
	if f > 0 then
		if self:getFocus() == o then
			self:tabForward()
		end
		table.remove(self.focusList, f)
		if self.currentFocus > f then
			self.currentFocus = self.currentFocus - 1
		end
	end
	f = 0
	i = 1
	while i <= #self.widgetList do
		if self.widgetList[i] == o then
			f = i
		end
		i = i + 1
	end
	if f > 0 then
		table.remove(self.widgetList, f)
	end
	--    table.remove(self.widgetList, o)
	--    table.remove(self.focusList, o)
end

function View:repos(o)
	local x = o.x
	local y = o.y
	local w = o.w
	local h = o.h
	if o.hConstraint == "right" then
		x = scrWidth - o.w - o.dx1
	elseif o.hConstraint == "center" then
		x = (scrWidth - o.w + o.dx1) / 2
	elseif o.hConstraint == "justify" then
		w = scrWidth - o.x - o.dx1
	end
	if o.vConstraint == "bottom" then
		y = scrHeight - o.h - o.dy1
	elseif o.vConstraint == "middle" then
		y = (scrHeight - o.h + o.dy1) / 2
	elseif o.vConstraint == "justify" then
		h = scrHeight - o.y - o.dy1
	end
	o:repos(x, y)
	o:resize(w, h)
end

function View:resize()
	for _, o in ipairs(self.widgetList) do
		self:repos(o)
	end
end

function View:hide(o)
	if o.visible then
		o.visible = false
		self:releaseFocus(o)
		if o:contains(self.prev_mousex, self.prev_mousey) then
			o:onMouseLeave(o.x - 1, o.y - 1)
		end
		self:invalidate()
	end
end

function View:show(o)
	if not o.visible then
		o.visible = true
		if o:contains(self.prev_mousex, self.prev_mousey) then
			o:onMouseEnter(self.prev_mousex, self.prev_mousey)
		end
		self:invalidate()
	end
end

function View:getFocus()
	if self.currentFocus == 0 then
		return nil
	end
	return self.focusList[self.currentFocus]
end

function View:setFocus(obj)
	if self.currentFocus ~= 0 then
		if self.focusList[self.currentFocus] == obj then
			return
		end
		self.focusList[self.currentFocus]:releaseFocus()
	end
	self.currentFocus = 0
	for i = 1, #self.focusList do
		if self.focusList[i] == obj then
			self.currentFocus = i
			obj:setFocus()
			self:invalidate()
			break
		end
	end
end

function View:releaseFocus(obj)
	if self.currentFocus ~= 0 then
		if self.focusList[self.currentFocus] == obj then
			self.currentFocus = 0
			obj:releaseFocus()
			self:invalidate()
		end
	end
end

function View:sendStringToFocus(str)
	local o = self:getFocus()
	if not o then
		o = defaultFocus
		self:setFocus(o)
	end
	if o then
		if o.visible then
			if o:addString(str) then
				self:invalidate()
			else
				o = nil
			end
		end
	end

	if not o then -- look for a default handler
		for _, o in ipairs(self.focusList) do
			if o.visible then
				if o:addString(str) then
					self:setFocus(o)
					self:invalidate()
					break
				end
			end
		end
	end
end


function View:backSpaceHandler()
	-- Does the focused widget accept BackSpace?
	local o = self:getFocus()
	if o then
		if o.visible and o.acceptsBackSpace then
			o:backSpaceHandler()
			self:setFocus(o)
			self:invalidate()
		else
			o = nil
		end
	end
	if not o then -- look for a default handler
		for _, o in ipairs(self.focusList) do
			if o.visible and o.acceptsBackSpace then
				o:backSpaceHandler()
				self:setFocus(o)
				self:invalidate()
				break;
			end
		end
	end
end


-- One traversal for both directions, bounded by the list rather than by a visible widget existing.
-- The two recursions this replaces had no way out when View:hide had emptied the visible set, and a
-- stack overflow in a key handler resets the calculator. Same shape and same fix as V4.
function View:stepFocus(delta)
	local count = #self.focusList
	if count == 0 then
		return
	end
	local index = self.currentFocus
	for _ = 1, count do
		index = index + delta
		if index > count then
			index = 1
		end
		if index < 1 then
			index = count
		end
		if self.focusList[index].visible then
			self:setFocus(self.focusList[index])
			break
		end
	end
	self:invalidate()
end


function View:tabForward()
	self:stepFocus(1)
end


function View:tabBackward()
	self:stepFocus(-1)
end


function View:onMouseDown(x, y)
	-- Find a widget that has a mouse down handler and bounds the click point
	for _, o in ipairs(self.widgetList) do
		if o.visible and o.acceptsFocus and o:contains(x, y) then
			self.mouseCaptured = o
			o:onMouseDown(o, window, x - o.x, y - o.y)
			self:setFocus(o)
			self:invalidate()
			return
		end
	end
	if self:getFocus() then
		self:setFocus(nil)
		self:invalidate()
	end
end


function View:onMouseMove(x, y)
	local prev_mousex = self.prev_mousex
	local prev_mousey = self.prev_mousey
	for _, o in ipairs(self.widgetList) do
		local xyin = o:contains(x, y)
		local prev_xyin = o:contains(prev_mousex, prev_mousey)
		if xyin and not prev_xyin and o.visible then
			-- Mouse entered widget
			o:onMouseEnter(x, y)
			self:invalidate()
		elseif prev_xyin and (not xyin or not o.visible) then
			-- Mouse left widget
			o:onMouseLeave(x, y)
			self:invalidate()
		end
	end
	self.prev_mousex = x
	self.prev_mousey = y
end


function View:onMouseUp(x, y)
	local mc = self.mouseCaptured
	if mc then
		self.mouseCaptured = nil
		if mc:contains(x, y) then
			mc:onMouseUp(x - mc.x, y - mc.y)
		else
			--            mc:cancelClick()
		end
	end
end


function View:enterHandler()
	-- Does the focused widget accept Enter?
	local o = self:getFocus()
	if o then
		if o.visible and o.acceptsEnter then
			o:enterHandler()
			self:setFocus(o)
			self:invalidate()
		else
			o = nil
		end
	end
	if not o then -- look for a default handler
		for _, o in ipairs(self.focusList) do
			if o.visible and o.acceptsEnter then
				o:enterHandler()
				self:setFocus(o)
				self:invalidate()
				break;
			end
		end
	end
end

function View:arrowLeftHandler()
	-- Does the focused widget accept ArrowLeft?
	local o = self:getFocus()
	if o then
		if o.visible and o.acceptsArrowLeft then
			o:arrowLeftHandler()
			self:setFocus(o)
			self:invalidate()
		else
			o = nil
		end
	end
	if not o then -- look for a default handler
		for _, o in ipairs(self.focusList) do
			if o.visible and o.acceptsArrowLeft then
				o:arrowLeftHandler()
				self:setFocus(o)
				self:invalidate()
				break;
			end
		end
	end
end

function View:arrowRightHandler()
	-- Does the focused widget accept ArrowRight?
	local o = self:getFocus()
	if o then
		if o.visible and o.acceptsArrowRight then
			o:arrowRightHandler()
			self:setFocus(o)
			self:invalidate()
		else
			o = nil
		end
	end
	if not o then -- look for a default handler
		for _, o in ipairs(self.focusList) do
			if o.visible and o.acceptsArrowRight then
				o:arrowRightHandler()
				self:setFocus(o)
				self:invalidate()
				break;
			end
		end
	end
end

function View:arrowUpHandler()
	-- Does the focused widget accept ArrowUp?
	local o = self:getFocus()
	if o then
		if o.visible and o.acceptsArrowUp then
			o:arrowUpHandler()
			self:setFocus(o)
			self:invalidate()
		else
			o = nil
		end
	end
	if not o then -- look for a default handler
		for _, o in ipairs(self.focusList) do
			if o.visible and o.acceptsArrowUp then
				o:arrowUpHandler()
				self:setFocus(o)
				self:invalidate()
				break;
			end
		end
	end
end

function View:arrowDownHandler()
	-- Does the focused widget accept ArrowDown?
	local o = self:getFocus()
	if o then
		if o.visible and o.acceptsArrowDown then
			o:arrowDownHandler()
			self:setFocus(o)
			self:invalidate()
		else
			o = nil
		end
	end
	if not o then -- look for a default handler
		for _, o in ipairs(self.focusList) do
			if o.visible and o.acceptsArrowDown then
				o:arrowDownHandler()
				self:setFocus(o)
				self:invalidate()
				break;
			end
		end
	end
end

function View:paint(gc)
	local fo = self:getFocus()
	for _, o in ipairs(self.widgetList) do
		if o.visible then
			o:paint(gc, fo == o)
			if fo == o then
				gc:setColorRGB(100, 150, 255)
				gc:drawRect(o.x - 1, o.y - 1, o.w + 1, o.h + 1)
				gc:setPen("thin", "smooth")
				gc:setColorRGB(0)
			end
		end
	end
	cursor.set(self.currentCursor)
end

theView = nil

--------------------------------------------------------------------- Widget

Widget = class()

function Widget:setHConstraints(hConstraint, dx1)
	self.hConstraint = hConstraint
	self.dx1 = dx1
end

function Widget:setVConstraints(vConstraint, dy1)
	self.vConstraint = vConstraint
	self.dy1 = dy1
end

function Widget:init(view, x, y, w, h)
	self.xOrig = x
	self.yOrig = y
	self.view = view
	self.x = x
	self.y = y
	self.w = w
	self.h = h
	self.acceptsFocus = false
	self.visible = true
	self.acceptsEnter = false
	self.acceptsEscape = false
	self.acceptsTab = false
	self.acceptsDelete = false
	self.acceptsBackSpace = false
	self.acceptsReturn = false
	self.acceptsArrowUp = false
	self.acceptsArrowDown = false
	self.acceptsArrowLeft = false
	self.acceptsArrowRight = false
	self.hConstraint = "left"
	self.vConstraint = "top"
end

function Widget:repos(x, y)
	self.x = x
	self.y = y
end

function Widget:resize(w, h)
	self.w = w
	self.h = h
end

function Widget:setFocus()
end

function Widget:releaseFocus()
end

function Widget:contains(x, y)
	return x >= self.x and x <= self.x + self.w
			and y >= self.y and y <= self.y + self.h
end


function Widget:onMouseEnter(x, y)
	-- Implemented in subclasses
end


function Widget:onMouseLeave(x, y)
	-- Implemented in subclasses
end

function Widget:paint(gc, focused)
	-- Implemented in subclasses
end

function Widget:enterHandler()
end

function Widget:escapeHandler()
end

function Widget:tabHandler()
end

function Widget:deleteHandler()
end

function Widget:backSpaceHandler()
end

function Widget:returnHandler()
end

function Widget:arrowUpHandler()
end

function Widget:arrowDownHandler()
end

function Widget:arrowLeftHandler()
end

function Widget:arrowRightHandler()
end

function Widget:onMouseDown(x, y)
end

function Widget:onMouseUp(x, y)
end

--------------------------------------------------------------------- Button widget

Button = class(Widget)

function Button:init(view, x, y, w, h, default, command, shortcut)
	Widget.init(self, view, x, y, w, h)
	-- Button configuration
	self.acceptsFocus = true
	self.acceptsBackspace = false
	self.command = command or function() end -- what to do when pressed
	self.default = default -- is default button when ENTER is pressed
	self.shortcut = shortcut
	-- Current button state
	self.clicked = false
	self.highlighted = false
	self.acceptsEnter = true
end

-- Act on key press on button
function Button:enterHandler()
	if self.acceptsEnter then
		self:command()
	end
end

function Button:escapeHandler()
	if self.acceptsEscape then
		self:command()
	end
end

function Button:tabHandler()
	if self.acceptsTab then
		self:command()
	end
end

function Button:deleteHandler()
	if self.acceptsDelete then
		self:command()
	end
end

function Button:backSpaceHandler()
	if self.acceptsBackSpace then
		self:command()
	end
end

function Button:returnHandler()
	if self.acceptsReturn then
		self:command()
	end
end

function Button:arrowUpHandler()
	if self.acceptsArrowUp then
		self:command()
	end
end

function Button:arrowDownHandler()
	if self.acceptsArrowDown then
		self:command()
	end
end

function Button:arrowLeftHandler()
	if self.acceptsArrowLeft then
		self:command()
	end
end

function Button:arrowRightHandler()
	if self.acceptsArrowRight then
		self:command()
	end
end

function Button:arrowUpHandler()
	if self.acceptsArrowUp then
		self:command()
	end
end

function Button:arrowDownHandler()
	if self.acceptsArrowDown then
		self:command()
	end
end

function Button:onMouseDown(x, y)
	self.clicked = true
	self.highlighted = true
end

function Button:onMouseEnter(x, y)
	theView:setCursor("hand pointer")
	if self.clicked and not self.highlighted then
		self.highlighted = true
	end
end

function Button:onMouseLeave(x, y)
	theView:setCursor("default")
	if self.clicked and self.highlighted then
		self.highlighted = false
	end
end

function Button:cancelClick()
	if self.clicked then
		self.highlighted = false
		self.clicked = false
	end
end

function Button:onMouseUp(x, y)
	self:cancelClick()
	self:command()
end

function Button:addString(str)
	if str == " " or str == self.shortcut then
		self:command()
		return true
	end
	return false
end

--------------------------------------------------------------------- ImgLabel widget

ImgLabel = class(Widget)

function ImgLabel:init(view, x, y, img)
	self.img = image.new(img)
	self.w = image.width(self.img)
	self.h = image.height(self.img)
	Widget.init(self, view, x, y, self.w, self.h, false, command, shortcut)
end

function ImgLabel:paint(gc, focused)
	gc:drawImage(self.img, self.x, self.y)
end

--------------------------------------------------------------------- ImgButton widget

ImgButton = class(Button)

function ImgButton:init(view, x, y, img, command, shortcut)
	self.img = image.new(img)
	self.w = image.width(self.img)
	self.h = image.height(self.img)
	Button.init(self, view, x, y, self.w, self.h, false, command, shortcut)
end

function ImgButton:paint(gc, focused)
	gc:drawImage(self.img, self.x, self.y)
end

--------------------------------------------------------------------- TextButton widget

TextButton = class(Button)

function TextButton:init(view, x, y, text, command, shortcut)
	self.textid = text
	self.text = getLocaleText(text)
	self:resize(0, 0)
	Button.init(self, view, x, y, self.w, self.h, false, command, shortcut)
end

function TextButton:resize(w, h)
	self.text = getLocaleText(self.textid)
	self.w = getStringWidth(self.text) + 5
	self.h = getStringHeight(self.text) + 5
end


function TextButton:paint(gc, focused)
	gc:setColorRGB(223, 223, 223)
	gc:drawRect(self.x + 1, self.y + 1, self.w - 2, self.h - 2)
	gc:setColorRGB(191, 191, 191)
	gc:fillRect(self.x + 1, self.y + 1, self.w - 3, self.h - 3)
	gc:setColorRGB(223, 223, 223)
	gc:drawString(self.text, self.x + 3, self.y + 3, "top")
	gc:setColorRGB(0)
	gc:drawString(self.text, self.x + 2, self.y + 2, "top")
	gc:drawRect(self.x, self.y, self.w - 2, self.h - 2)
end

--------------------------------------------------------------------- vertical scroll bar
VScrollBar = class(Widget)

function VScrollBar:init(view, x, y, w, h)
	self.pos = 10
	self.siz = 10
	Widget.init(self, view, x, y, w, h, false)
end

function VScrollBar:paint(gc, focused)
	gc:setColorRGB(0)
	gc:drawRect(self.x, self.y, self.w, self.h)
	gc:fillRect(self.x + 2, self.y + self.h - (self.h - 4) * (self.pos + self.siz) / 100 - 2, self.w - 3, mathmax(1, (self.h - 4) * self.siz / 100 + 1))
end

--------------------------------------------------------------------- Text widget

TextLabel = class(Widget)

function TextLabel:init(view, x, y, text)
	self:setText(text)
	Widget.init(self, view, x, y, self.w, self.h, false)
end

function TextLabel:resize(w, h)
	self.text = getLocaleText(self.textid)
	self.w = getStringWidth(self.text)
	self.h = getStringHeight(self.text)
end

function TextLabel:setText(text)
	self.textid = text
	self.text = getLocaleText(text)
	self:resize(0, 0)
end

function TextLabel:getText()
	return self.text
end

function TextLabel:paint(gc, focused)
	gc:setColorRGB(0)
	gc:drawString(self.text, self.x, self.y, "top")
end

--------------------------------------------------------------------- editable RichText widget 

RichTextEditor = class(Widget)

function RichTextEditor:init(view, x, y, w, h, text)
	self.editor = D2Editor.newRichText()
	self.readOnly = false
	self:repos(x, y)
	self.editor:setFontSize(fsize)
	self.editor:setFocus(false)
	self.text = text
	self:resize(w, h)
	Widget.init(self, view, x, y, self.w, self.h, true)
	self.acceptsFocus = true
	self.editor:setExpression(text)
	self.editor:setBorder(1)
end

function RichTextEditor:onMouseEnter(x, y)
	theView:setCursor("text")
end

function RichTextEditor:onMouseLeave(x, y)
	theView:setCursor("default")
end

function RichTextEditor:repos(x, y)
	if not self.editor then self = nil; return; end
	self.editor:setBorderColor((showEditorsBorders and 0) or 0xffffff)
	self.editor:move(x+1, y+1)
	Widget.repos(self, x, y)
end

function RichTextEditor:resize(w, h)
	if not self.editor then self = nil; return; end
	self.editor:resize(w-1, h-1)
	Widget.resize(self, w, h)
end

function RichTextEditor:setFocus()
	self.editor:setFocus(true)
end

function RichTextEditor:releaseFocus()
	self.editor:setFocus(false)
end

function RichTextEditor:addString(str)
	local currentText = self.editor:getText() or ""
	self.editor:setText(currentText .. str)
	return true
end


function RichTextEditor:paint(gc, focused)
	--    self.editor:paint(gc)
end

--------------------------------------------------------------------- editable Math widget 

MathEditor = class(RichTextEditor)

-- pretty printed square root characters from MathBoxes take two 'special' characters in returned expression string
-- cursor position in expression returned by getExpression() then does not match cursor position in string after the square root
function string.ulen(s)
	if not s then return 0 else return select(2, s:gsub("[^\128-\193]", "")) end
end
ulen = string.ulen

function MathEditor:init(view, x, y, w, h, text)
	RichTextEditor.init(self, view, x, y, w, h, text)
	self.editor:setBorder(1)
	self.acceptsEnter = true
	self.acceptsBackSpace = true
	self.result = false
	-- add editor focus listener which does: setFocus(getME(editor))
	self.editor:registerFilter({
		arrowLeft = function()
			local _, curpos = self.editor:getExpressionSelection()
			if curpos < 7 then
				on.arrowLeft()
				return true
			end
			return false
		end,
		arrowRight = function()
			local currentText, curpos = self.editor:getExpressionSelection()
			if curpos > ulen(currentText) - 2 then
				on.arrowRight()
				return true
			end
			return false
		end,
		tabKey = function()
			theView:tabForward()
			return true
		end,
		mouseDown = function(x, y)
			theView:onMouseDown(x, y)
			return false
		end,
		backspaceKey = function()
			if (self == fctEditor) then
				self:fixCursor()
				local _, curpos = self.editor:getExpressionSelection()
				if curpos <= 6 then return true end
				return false
			else
				self:backSpaceHandler()
				return true
			end
		end,
		deleteKey = function()
			if (self == fctEditor) then
				self:fixCursor()
				local currentText, curpos = self.editor:getExpressionSelection()
				if curpos >= ulen(currentText) - 1 then return true end
				return false
			else
				self:backSpaceHandler()
				return true
			end
		end,
		enterKey = function()
			self:enterHandler()
			return true
		end,
		returnKey = function()
			theView:enterHandler()
			return true
		end,
		escapeKey = function()
			on.escapeKey()
			return true
		end,
		clearKey = function()
			if self == fctEditor then
				self.editor:setExpression("")
				self:fixContent()
			else
				self:backSpaceHandler()
			end
			return true
		end,
		charIn = function(c)
			if (self == fctEditor) then
				if self.editor:getExpression() then
					self:fixCursor()
				end
				return false
			else
				return self.readOnly
			end
		end
	})
end

function MathEditor:fixContent()
	local currentText = self.editor:getExpressionSelection()
	if not currentText or currentText == "" then
		self.editor:createMathBox()
	end
end

function MathEditor:fixCursor()
	local currentText, curpos, selstart = self.editor:getExpressionSelection()
	local l = ulen(currentText)
	if curpos < 6 or selstart < 6 or curpos > l - 1 or selstart > l - 1 then
		if curpos < 6 then curpos = 6 end
		if selstart < 6 then selstart = 6 end
		if curpos > l - 1 then curpos = l - 1 end
		if selstart > l - 1 then selstart = l - 1 end
		self.editor:setExpression(currentText, curpos, selstart)
	end
end

function MathEditor:getExpression()
	if not self.editor then self = nil; return ""; end
	local rawexpr = self.editor:getExpression()
	local expr = ""
	local n = rawexpr:len()
	local b = 0
	local bs = 0
	local bi = 0
	local status = 0
	local i = 1
	local c
	while i <= n do
		c = rawexpr:sub(i, i)
		if c == "{" then
			b = b + 1
		elseif c == "}" then
			b = b - 1
		end
		if status == 0 then
			if rawexpr:sub(i, i + 5) == "\\0el {" then
				bs = i + 6
				i = i + 5
				status = 1
				bi = b
				b = b + 1
			end
		else
			if b == bi then
				status = 0
				expr = expr .. rawexpr:sub(bs, i - 1)
			end
		end
		i = i + 1
	end
	return expr
end

function MathEditor:setFocus()
	if not self.editor then self = nil; return; end
	self.editor:setFocus(true)
end

function MathEditor:releaseFocus()
	if not self.editor then self = nil; return; end
	self.editor:setFocus(false)
end

function MathEditor:addString(str)
	if not self.editor then self = nil; return; end
	self:fixCursor()
	local currentText, curpos, selstart = self.editor:getExpressionSelection()
	currentText = currentText:usub(1, mathmin(curpos, selstart)) .. str .. currentText:usub(mathmax(curpos, selstart) + 1, ulen(currentText))
	self.editor:setExpression(currentText, mathmin(curpos, selstart) + ulen(str))
	return true
end

function MathEditor:backSpaceHandler()
	backSpaceHandler(self)
end

function MathEditor:enterHandler()
	enterHandler(self)
end

function MathEditor:paint(gc)
	if not self.editor then self = nil; return; end
	if showHLines and not self.result then
		gc:setColorRGB(100, 100, 100)
		local ycoord = self.y - (showEditorsBorders and 0 or 2)
		gc:drawLine(1, ycoord, platform.window:width() - sbv.w - 2, ycoord)
		gc:setColorRGB(0)
	end
end

--------------------------------------------------------------------- events handling
function on.arrowUp()
	if theView:getFocus() == fctEditor then
		on.tabKey()
	else
		on.tabKey()
		if theView:getFocus() ~= fctEditor then on.tabKey() end
	end
	reposView()
end

function on.arrowDown()
	if theView:getFocus() == fctEditor then return end
	on.backtabKey()
	if theView:getFocus() ~= fctEditor then on.backtabKey() end
	reposView()
end

function on.arrowLeft()
	if theView:getFocus() == fctEditor then return end
	on.tabKey()
	reposView()
end

function on.arrowRight()
	if theView:getFocus() == fctEditor then return end
	on.backtabKey()
	reposView()
end

function on.charIn(ch)
	theView:sendStringToFocus(ch)
end

function on.tabKey()
	theView:tabForward()
	reposView()
end

function on.backtabKey()
	theView:tabBackward()
	reposView()
end

function on.escapeKey()
	-- nothing to do ?
end

function on.enterKey()
	theView:enterHandler()
end
on.returnKey = on.enterKey

function on.mouseMove(x, y)
	theView:onMouseMove(x, y)
end

function on.mouseDown(x, y)
	theView:onMouseDown(x, y)
	--		theView:invalidate()
end

function on.mouseUp(x, y)
	theView:onMouseUp(x, y)
end

function initFontGC(gc)
	gc:setFont(font, style, fsize)
end

function getStringHeightGC(text, gc)
	initFontGC(gc)
	return gc:getStringHeight(text)
end

function getStringHeight(text)
	return platform.withGC(getStringHeightGC, text)
end

function getStringWidthGC(text, gc)
	initFontGC(gc)
	return gc:getStringWidth(text)
end

function getStringWidth(text)
	return platform.withGC(getStringWidthGC, text)
end

function initGUI()
	showEditorsBorders = false
	showHLines = true
	scrWidth = platform.window:width()
	scrHeight = platform.window:height()
	if (scrWidth > 0 or scrHeight > 0) then
		theView = View(platform.window)
		sbv = VScrollBar(theView, 0, -1, 5, scrHeight + 1)
		sbv:setHConstraints("right", 0)
		theView:add(sbv)
		fctEditor = MathEditor(theView, 2, border, 50, 30, "")
		-- fctEditor = MathEditor(theView, border, border, 50, 30, "")
		fctEditor:setHConstraints("justify", 1)
		-- fctEditor:setHConstraints("justify", border + scrWidth - sbv.x)
		fctEditor:setVConstraints("bottom", 1)
		fctEditor.editor:setSizeChangeListener(function(editor, w, h)
			return resizeME(editor, w, h)
		end)
		theView:add(fctEditor)
		fctEditor.editor:setText("")
		fctEditor.editor:setBorder(0)
		fctEditor:fixContent()
		sbv:setVConstraints("justify", scrHeight - fctEditor.y + border)
		theView:setFocus(fctEditor)
		inited = true
	end
	toolpalette.enableCopy(true)
	toolpalette.enablePaste(true)
end

function resizeGC(gc)
	scrWidth = platform.window:width()
	scrHeight = platform.window:height()
	if not inited then
		initGUI()
	end
	if inited then
		initFontGC(gc)
		strFullHeight = gc:getStringHeight("H")
		strHeight = strFullHeight - 3
		theView:resize()
		reposME()
		theView:invalidate()
	end
end

function on.resize()
	platform.withGC(resizeGC)
end

forcefocus = true
function on.activate()
	forcefocus = true
end

dispinfos = true
function on.paint(gc)
	if not inited then
		initGUI()
		initFontGC(gc)
		strFullHeight = gc:getStringHeight("H")
		strHeight = strFullHeight - 3
	end
	if inited then
		local obj
		obj = theView:getFocus()
		initFontGC(gc)
		if not obj then theView:setFocus(fctEditor) end
		if (forcefocus) then
			if obj == fctEditor then
				fctEditor.editor:setFocus(true)
				if fctEditor.editor:hasFocus() then forcefocus = false end
			else
				forcefocus = false
			end
		end
		if dispinfos then
			gc:setColorRGB(0)
			gc:setFont("sansserif", "r", 10)
			local glabel = giacLabel()
			gc:drawString(glabel, 2, 0, "top")
			local line = strHeight
			if hasGiac then
				gc:setColorRGB(0, 127, 0)
				gc:drawString("OK.", gc:getStringWidth(glabel) + 6, 0, "top")
			else
				gc:setColorRGB(255, 0, 0)
				gc:drawString("NO.", gc:getStringWidth(glabel) + 6, 0, "top")
				gc:setColorRGB(0)
				gc:drawString("Make sure to have the .luax file and Ndl installed!", 2, line, "top")
				gc:drawString("Hint: run ndl_installer again", 2, line + strHeight, "top")
				gc:setFont("sansserif", "i", 10)
				gc:drawString("Fallback on the Nspire's math engine.", 2, line + 2 * strHeight + 8, "top")
				gc:setFont("sansserif", "r", 10)
				line = line + 3 * strHeight + 8
			end
			-- The step surface has its own seven refusal paths and none of them are Giac's, so a
			-- module that loaded but cannot be called has to say so here rather than at first use.
			if stepSurfaceError then
				gc:setColorRGB(180, 0, 0)
				gc:drawString(paintableFailure(stepSurfaceError), 2, line, "top")
			end
			gc:setColorRGB(0)
			gc:setFont("sansserif", "r", fsize)
		end
		theView:paint(gc)
		-- gc:drawString(ts, 2, 0, "top")
		gc:drawRect(0, fctEditor.y - 2, scrWidth, fctEditor.y - 2)
	end
end

--------------------------------------------------------------------- global variables

font = "sansserif"
style = "r"
fsize = 12

scrWidth = 0
scrHeight = 0
inited = false
delim = " ≟ "
border = 3

strHeight = 0
strFullHeight = 0

--------------------------------------------------------------------- global functions

evalstr = false

histME1 = {}
histME2 = {}

function addME(expr, res)
	local mee = MathEditor(theView, border, border, 50, 30, "")
	mee.readOnly = true
	table.insert(histME1, mee)
	mee:setHConstraints("left", border)
	mee.editor:setSizeChangeListener(function(editor, w, h)
		return resizeME(editor, w + 3, h)
	end)
	mee.editor:setExpression("\\0el {" .. expr .. "}", 0)
	mee:fixCursor()
	mee.editor:setReadOnly(true)
	theView:add(mee)

	local mer = MathEditor(theView, border, border, 50, 30, "")
	mer.result = true
	mer.readOnly = true
	table.insert(histME2, mer)
	mer:setHConstraints("right", scrWidth - sbv.x + border)
	mer.editor:setSizeChangeListener(function(editor, w, h)
		return resizeMEpar(editor, w + border, h)
	end)
	mer.editor:setExpression("\\0el {" .. res .. "}", 0)
	mer:fixCursor()
	mer.editor:setReadOnly(true)
	theView:add(mer)
	-- Ki V3: the text behind the editors, so a save can carry the history.
	table.insert(steps.histText, { expr, res })
	reposME()
end

limpsuff = "(+)"
limmsuff = "(-)"
function cleanAns1(expr)
	local a, b = expr:find(delim, 1, true)
	local c = 0
	local i = 1
	local l = string.len(expr)
	if a then
		c = expr:find(".", 1, true)
		if not c then c = 0 end
		if c == 0 or c > b then
			expr = expr:sub(1, a - 1)
		else
			expr = expr:sub(b + 1, l)
		end
		return cleanAns1(expr)
	end
	l = string.len(expr)
	a, b = expr:find(limpsuff, 1, true)
	if (not (a)) then
		a, b = expr:find(limmsuff, 1, true)
	else
		a = a - 2
		local texpr = ""
		if (a > 1) then texpr = expr:sub(1, a - 1) end
		if (b < l) then texpr = texpr .. expr:sub(b + 1, l) end
		expr = texpr
	end
	return expr
end

function escapeStr(expr)
	local expr2 = ""
	local l = string.len(expr)
	local c
	for i = 1, l do
		c = expr:sub(i, i)
		if c == "\"" then
			c = "\\\""
		end
		expr2 = expr2 .. c
	end
	return expr2
end

function backSpaceHandler(widget)
	local i = 1
	local f = 0
	local n = mathmax(#histME1, #histME2)
	if (widget ~= fctEditor) then
		while (f == 0 and i <= n) do
			if histME1[i] == widget or histME2[i] == widget then
				f = i
			end
			i = i + 1
		end
	end
	if f > 0 then
		destroyD2Editor(histME1[f].editor)
		destroyD2Editor(histME2[f].editor)
		theView:remove(histME1[f])
		theView:remove(histME2[f])
		table.remove(histME1, f)
		table.remove(histME2, f)
		table.remove(steps.histText, f)
		reposME()
	end
end

function enterHandler(widget)
	local expr, exprkeep
	local svar
	local incerr = "incompatible data type"
	if (widget ~= fctEditor) then
		expr = cleanAns1(widget:getExpression())
		theView:setFocus(fctEditor)
		fctEditor:addString(expr)
	else
		if (fctEditor.editor:getExpression()) then
			expr = fctEditor.editor:getExpression()
			expr = fctEditor:getExpression()
			if (expr and expr ~= "") then
				dispinfos = false
				t1 = timer.getMilliSecCounter()
				-- Ki V3: a step request goes to nps_split, anything else to Giac as before.
				local request = stepRequest(expr)
				if request then
					res = runSteps(request.mode, request.text)
				else
					res = luagiac.caseval(expr) or "Error"
				end
				t2 = timer.getMilliSecCounter()
				ts  = string.format("Time :  %f" , ( t2 - t1 ) / 1000. )
				fctEditor.editor:setText("")
				fctEditor:fixContent()
				ioffset = 0

				res = " " .. res
				expr = " " .. expr
				expr = expr:gsub("^%s+", " ")
				addME(expr, res)
			end
		end
	end
end

function getParME(editor)
	for i = 1, #histME2 do
		if histME2[i].editor == editor then
			return histME1[i]
		end
	end
	return nil
end

function getME(editor)
	if (fctEditor.editor == editor) then
		return fctEditor
	else
		for i = 1, #histME1 do
			if histME1[i].editor == editor then
				return histME1[i]
			end
		end
		for i = 1, #histME2 do
			if histME2[i].editor == editor then
				return histME2[i]
			end
		end
	end
	return nil
end

function getMEindex(me)
	local ti
	if (fctEditor.editor == me) then
		return 0
	else
		ti = 0
		for i = #histME1, 1, -1 do
			if histME1[i] == me then
				return ti
			end
			ti = ti + 1
		end
		ti = 0
		for i = #histME2, 1, -1 do
			if histME2[i] == me then
				return ti
			end
			ti = ti + 1
		end
	end
	return 0
end

function resizeMEpar(editor, w, h)
	local pare = getParME(editor)
	if pare then
		resizeMElim(editor, w, h, pare.w + pare.dx1 * 2)
	else
		resizeME(editor, w, h)
	end
end

function resizeME(editor, w, h)
	if not editor then return end
	resizeMElim(editor, w, h, scrWidth / 2)
end

function resizeMElim(editor, w, h, lim)
	if not editor then return end
	local met = getME(editor)
	if met then
		met.needw = w
		met.needh = h
		w = mathmax(w, 0)
		w = mathmin(w, scrWidth - met.dx1 * 2)
		h = mathmax(h, strFullHeight + 8)
		if (me ~= fctEditor) then
			w = mathmin(w, (scrWidth - lim) - 2 * met.dx1 + 1)
		end
		met:resize(w, h)
		needcenter = true
		reposME()
		theView:invalidate()
	end
	return editor
end

ioffset = 0
function reposView()
	local focusedME = theView:getFocus()
	if focusedME and focusedME ~= fctEditor then
		local y = focusedME.y
		local h = focusedME.h
		local y0 = fctEditor.y
		local index = getMEindex(focusedME)
		if y < 0 and ioffset < index then
			ioffset = ioffset + 1
			reposME()
			reposView()
		end
		if y + h > y0 and ioffset > index then
			ioffset = ioffset - 1
			reposME()
			reposView()
		end
	end
end

function reposME()
	local h, y, ry, res, i0, beforeh, totalh, visih, h1, h2
	totalh = 0
	beforeh = 0
	visih = 0
	fctEditor.y = scrHeight - fctEditor.h
	theView:repos(fctEditor)
	sbv:setVConstraints("justify", scrHeight - fctEditor.y + border)
	theView:repos(sbv)
	y = fctEditor.y
	i0 = mathmax(#histME1, #histME2)
	for i = i0, 1, -1 do
		h = 0
		h1 = 0
		h2 = 0
		if i <= #histME1 then h1 = mathmax(h1, histME1[i].h) end
		if i <= #histME2 then h2 = mathmax(h2, histME2[i].h) end
		h = mathmax(h1, h2)
		if i0 - i >= ioffset then
			if y >= 0 then
				if y >= h + border then
					visih = visih + h + border
				else
					visih = visih + y
				end
			end
			y = y - h - border
			ry = y
			totalh = totalh + h + border
		else
			ry = scrHeight
			beforeh = beforeh + h + border
			totalh = totalh + h + border
		end
		if i <= #histME1 then
			histME1[i].y = ry
			theView:repos(histME1[i])
			if histME1[i].focus then res = histME1[i] end
		end
		if i <= #histME2 then
			histME2[i].y = ry + mathmax(0, h1 - h2)
			theView:repos(histME2[i])
			if histME2[i].focus then res = histME2[i] end
		end
	end
	if totalh == 0 then
		sbv.pos = 0
		sbv.siz = 100
	else
		sbv.pos = beforeh * 100 / totalh
		sbv.siz = visih * 100 / totalh
	end
	theView:invalidate()
end

function destroyD2Editor(editor)
	if not editor then return end
	editor:setVisible(false)
	editor:move(-10000, -10000)
	editor:resize(1, 1)
	editor = nil
end

-- The OS draws these, so they cannot drift from the native dialogs, and they are absent when the module is.
local function osMsgbox(message, first, second, third)
	if type(nps_split) ~= "table" or type(nps_split.os_msgbox) ~= "function" then return nil end
	local shown, pressed = pcall(nps_split.os_msgbox, "Ki", message, first, second, third)
	if not shown then return nil end
	return pressed
end

-- The framework draws this one, the same list widget Giac uses for its own menus. Measured on the
-- emulator: escape closes it in a single press and returns nil, so a dismissal is not a selection.
local function osMenu(title, items)
	if type(nps_split) ~= "table" or type(nps_split.os_menu) ~= "function" then return nil end
	local shown, chosen = pcall(nps_split.os_menu, title, items)
	if not shown then return nil end
	return chosen
end

function reset()
	for _, v in pairs(theView.widgetList) do
		theView:remove(v)
	end
	for _, e in pairs(histME1) do
		destroyD2Editor(e.editor)
	end
	for _, e in pairs(histME2) do
		destroyD2Editor(e.editor)
	end
	histME1 = {}
	histME2 = {}
	steps.histText = {}
	fsize = 10
	applyFontSizeChange()
	platform.window:invalidate()
	myErrorHandler()
end

-- Focus opens on the first button and escape returns the last, so the destructive answer sits between them.
local function clearHistory()
	local pressed = osMsgbox("Clear the whole history?", "Keep History", "Clear History", "Cancel")
	if pressed == nil then
		addME(" Clear History", " no way to ask first, so nothing was cleared")
		return
	end
	if pressed ~= 2 then return end
	reset()
end

function myErrorHandler(line, errMsg, callStack, locals)
	if errMsg then print(errMsg) end
	defaultFocus = nil
	theView = nil
	collectgarbage()
	initGUI()
	if errMsg then addME("Script error", errMsg) end
	collectgarbage()
	return true -- let the script continue
end

platform.registerErrorHandler(myErrorHandler)


--------------------------------------------------------------------- toolpalette stuff

function toggleBorders()
	showEditorsBorders = not showEditorsBorders
	on.resize()
end

function set1d()
	 fctEditor.editor:setDisable2DinRT(true)
	on.resize()
end

function set2d()
	 fctEditor.editor:setDisable2DinRT(false)
	on.resize()
end

function toggleHLines()
	showHLines = not showHLines
	on.resize()
end

function applyFontSizeChange()
	fctEditor.editor:setFontSize(fsize)
	for _, e in pairs(histME1) do
		e.editor:setFontSize(fsize)
	end
	for _, e in pairs(histME2) do
		e.editor:setFontSize(fsize)
	end
end

do
	-- TI restricts handheld sizes to "7, 9, 10, 11, 12, 16, or 24": https://education.ti.com/html/eguides/nspire/EG_Nspire/EN/content/eg_lua/m_libraries/2deditorlib/setfontsize.HTML
	local SHELL_FONTS = { 7, 9, 10, 11, 12, 16, 24 }

	function fontDown()
		local chosen = SHELL_FONTS[1]
		for _, size in ipairs(SHELL_FONTS) do
			if size < fsize then chosen = size end
		end
		fsize = chosen
		applyFontSizeChange()
	end

	function fontUp()
		local chosen = SHELL_FONTS[#SHELL_FONTS]
		for index = #SHELL_FONTS, 1, -1 do
			if SHELL_FONTS[index] > fsize then chosen = SHELL_FONTS[index] end
		end
		fsize = chosen
		applyFontSizeChange()
	end
end

function menustring( ch )
--	 theView:sendStringToFocus( ch )
	 if fctEditor then fctEditor:addString( ch ) end
end

menu = {
       -- Ki V3: the physics slice. The label says what the entry finds in a beginner's words and
       -- choosing it drops the command into the input editor, where the syntax is theirs to edit.
       -- Which equation applies is still the solver's verdict from what is given.
       { "Physics",
       	 { "Find final speed from acceleration and time",	function() menustring( "!k find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s" ) end },
       	 { "Find starting speed from final speed",	function() menustring( "!k find v0; v = 17 m/s; a = 3 m/s^2; t = 4 s" ) end },
       	 { "Find acceleration from a change in speed",	function() menustring( "!k find a; v0 = 5 m/s; v = 17 m/s; t = 4 s" ) end },
       	 { "Find the time taken to reach a new speed",	function() menustring( "!k find t; v0 = 5 m/s; v = 17 m/s; a = 3 m/s^2" ) end },
       	 { "Find distance travelled from average speed",	function() menustring( "!k find x; v0 = 5 m/s; v = 17 m/s; t = 4 s" ) end },
       	 { "Find distance when the units are mixed",	function() menustring( "!k find x; v0 = 18 km/h; a = 3 m/s^2; t = 4 s" ) end },
       	 { "Find final speed from distance and time",	function() menustring( "!k find v; x = 20 m; t = 4 s; a = 3 m/s^2" ) end },
       	 { "Find the speed of a falling object",	function() menustring( "!k find v; v0 = 0 m/s; a = 9.8 m/s^2; t = 2.5 s" ) end },
       	 { "Blank problem, fill in your own numbers",	function() menustring( "!k find v; v0 = ; a = ; t = " ) end },
       },
       -- Native wording first, the callable form after it. A tool palette has no submenu, second
       -- line or tooltip, so where the pair runs past 44 characters the argument spelling gives way
       -- and never the native name. 44 comes from budgets' emulator measurement: English is complete
       -- at 47 and cut at 50, all-W is cut at 25, and the cut is silent with no ellipsis.
       { "Actions",
       	 { "Open Shell  *",	function() menustring( "*" ) end },
       	 { "Open Script Editor",	function() menustring( "+\"\"" ) end },
       	 "-",
       	 { "Save Variables  write(\"a.tns\",0",	function() menustring( "write(\"a.tns\",0" ) end },
       	 { "Read Variables  eval(read(\"a.tns\"))",	function() menustring( "eval(read(\"a.tns\"))" ) end },
       	 "-",
       	 { "Show/Hide Editor Borders", toggleBorders },
       	 { "Show/Hide Horizontal Lines", toggleHLines },
       	 "-",
       	 { "Increase Font Size", fontUp },
       	 { "Decrease Font Size", fontDown },
       	 "-",
       	 { "Restart CAS  restart;",	function() menustring( "restart;" ) end },
       	 { "Clear History", clearHistory },
       },
       { "Number",
       	 { "Convert to Decimal  evalf(x[,prec])",	function() menustring( "evalf(" ) end },
       	 { "Factor Integer  ifactor(n)",	function() menustring( "ifactor(" ) end },
       	 { "Euclidean Quotient  iquo(a,b)",	function() menustring( "iquo(" ) end },
       	 { "Euclidean Remainder  irem(a,b)",	function() menustring( "irem(" ) end },
       	 { "Prime Test  is_prime(p)",	function() menustring( "is_prime" ) end },
       	 { "Next Prime  nextprime(n)",	function() menustring( "nextprime(" ) end },
       	 { "Divisor List  idivis(n)",	function() menustring( "idivis(" ) end },
       	 { "Extended GCD  iegcd(a,b)",	function() menustring( "iegcd(" ) end },
       	 { "Bezout Coefficients  iabcuv(a,b,c)",	function() menustring( "iabcuv(" ) end },
       	 { "Euler Indicatrix  euler(n)",	function() menustring( "euler(" ) end },
       	 { "Chinese Remainder  ichrem([a,n],[b,m])",	function() menustring( "ichrem(" ) end },
       	 { "Power Modulo  powmod(a,m,n)",	function() menustring( "powmod(" ) end },
       	 "-",
       	 { "Absolute Value  abs(x)",	function() menustring( "abs(" ) end },
       	 { "Floor  floor(x)",	function() menustring( "floor(" ) end },
       	 { "Ceiling  ceil(x)",	function() menustring( "ceil(" ) end },
       	 { "Sign  sign(x)",	function() menustring( "sign(" ) end },
       	 { "Round  round(x[,n])",	function() menustring( "round(" ) end },
       	 { "Maximum  max(x)",	function() menustring( "max(" ) end },
       	 { "Minimum  min(x)",	function() menustring( "min(" ) end },
       	 { "Interval  [inf..sup]",	function() menustring( "[..]") end },
       	 { "Convert to Interval  convert(expr,interval)",	function() menustring( "convert(,interval)" ) end },
       	 "-",
       	 { "Real Part  re(z)",	function() menustring( "re(" ) end },
       	 { "Imaginary Part  im(z)",	function() menustring( "im(" ) end },
       	 { "Complex Conjugate  conj(z)",	function() menustring( "conj(" ) end },
       	 { "Argument  arg(z)",	function() menustring( "arg(" ) end },
       },
       { "Algebra",
       	 { "Solve  solve(expr,var)",	function() menustring( "solve(" ) end },
       	 { "Factor  factor(expr)",	function() menustring( "factor(" ) end },
       	 { "Normal Form  normal(expr)",	function() menustring( "normal(" ) end },
       	 { "Simplify  simplify(expr)",	function() menustring( "simplify(" ) end },
       	 { "Substitute  subst(expr,var,value)",	function() menustring( "subst(" ) end },
       	 { "Convert  convert(expr,...)",	function() menustring( "convert(" ) end },
       	 { "Numerical Solve  fsolve(expr,var,guess)",	function() menustring( "fsolve(" ) end },
       	 { "Recurrence Solve  rsolve(eq,un)",	function() menustring( "rsolve(" ) end },
       	 { "Partial Fractions  partfrac(expr)",	function() menustring( "partfrac(" ) end },
       	 { "Collect Trig  tcollect(expr)",	function() menustring( "tcollect(" ) end },
       	 { "Expand Trig  texpand(expr)",	function() menustring( "texpand(" ) end },
       	 "-",
       	 { "Complex Factor  cfactor(expr)",	function() menustring( "cfactor(" ) end },
       	 { "Complex Partial Fractions  cpartfrac(expr)",	function() menustring( "cpartfrac(" ) end },
       	 { "Complex Solve  csolve(expr,var)",	function() menustring( "csolve(" ) end },
       },
       -- Native nests these under Algebra as Polynomial Tools. The palette has no submenu and
       -- Algebra plus Polynomials is 36 entries against a cap of 30, so they stay a box of their own.
       { "Polynomials",
       	 { "Factor  factor(P)",	function() menustring( "factor(" ) end },
       	 { "Complex Factor  cfactor(P)",	function() menustring( "cfactor(" ) end },
       	 { "Polynomial Roots  proot(P)",	function() menustring( "proot(" ) end },
       	 { "Degree  degree(P,var)",	function() menustring( "degree(" ) end },
       	 { "Coefficient  coeff(P,var,n)",	function() menustring( "coeff(" ) end },
       	 { "Horner Form  horner(P,x)",	function() menustring( "horner(" ) end },
       	 { "Canonical Form  canonical_form(P,var)",	function() menustring( "canonical_form(" ) end },
       	 { "Coefficients to Polynomial  pcoeff(list)",	function() menustring( "pcoeff(" ) end },
       	 { "Interpolation  lagrange(X,Y)",	function() menustring( "lagrange(" ) end },
       	 { "Euclidean Division  quorem(A,B)",	function() menustring( "quorem(" ) end },
       	 { "Greatest Common Divisor  gcd(A,B)",	function() menustring( "gcd(" ) end },
       	 { "Extended GCD  egcd(A,B,var)",	function() menustring( "egcd(" ) end },
       	 { "Bezout Coefficients  abcuv(A,B,C,var)",	function() menustring( "abcuv(" ) end },
       	 { "Polynomial to List  symb2poly(P,var)",	function() menustring( "symb2poly(" ) end },
       	 { "List to Polynomial  poly2symb(list,var)",	function() menustring( "poly2symb(" ) end },
       	 { "Resultant  resultant(A,B,var)",	function() menustring( "resultant(" ) end },
       	 { "Groebner Basis  gbasis(polys,vars)",	function() menustring( "gbasis(" ) end },
       	 "-",
       	 { "Cyclotomic  cyclotomic(n)",	function() menustring( "cyclotomic(" ) end },
       	 { "Hermite  hermite(n)",	function() menustring( "hermite(" ) end },
       	 { "Chebyshev First  tchebyshev1(n)",	function() menustring( "tchebyshev1(" ) end },
       	 { "Chebyshev Second  tchebyshev2(n)",	function() menustring( "tchebyshev2(" ) end },
       	 { "Random Polynomial  randpoly(n)",	function() menustring( "randpoly(" ) end },
       },
       { "Calculus",
       	 { "Derivative  diff(expr,var)",	function() menustring( "diff(" ) end },
       	 { "Integral  int(expr,var)",	function() menustring( "int(" ) end },
       	 { "Limit  limit(expr,var,value)",	function() menustring( "limit(" ) end },
       	 { "Sum  sum(expr,var,min,max)",	function() menustring( "sum(" ) end },
       	 { "Series  series(expr,var=value,order)",	function() menustring( "series(" ) end },
       	 { "Differential Equation  desolve(eq,x,y)",	function() menustring( "desolve(" ) end },
       },
       { "Probability",
       	 { "Factorial  factorial(n)",	function() menustring( "factorial(" ) end },
       	 { "Permutations  perm(n,p)",	function() menustring( "perm(" ) end },
       	 { "Combinations  comb(n,p)",	function() menustring( "comb(" ) end },
       	 { "Random  rand()",	function() menustring( "rand(" ) end },
       	 "-",
       	 { "Binomial  binomial(n,p,k)",	function() menustring( "binomial(" ) end },
       	 { "Binomial Cdf  binomial_cdf(n,p,x)",	function() menustring( "binomial_cdf(" ) end },
       	 { "Binomial Inverse Cdf  binomial_icdf(n,p,y)",	function() menustring( "binomial_icdf(" ) end },
       	 { "Normal  normald(m,sigma,x)",	function() menustring( "normald(" ) end },
       	 { "Normal Cdf  normald_cdf(m,sigma,x)",	function() menustring( "normald_cdf(" ) end },
       	 { "Normal Inverse Cdf  normald_icdf(m,sigma,y)",	function() menustring( "normald_icdf(" ) end },
       	 { "Poisson  poisson(mu,k)",	function() menustring( "poisson(" ) end },
       	 { "Exponential  exponentiald(mu,x)",	function() menustring( "exponentiald(" ) end },
       	 { "Geometric  geometric(p,k)",	function() menustring( "geometric(" ) end },
       	 { "Chi Squared  chisquared(n,x)",	function() menustring( "chisquared(" ) end },
       	 { "Uniform  uniformd(a,b,x)",	function() menustring( "uniformd(" ) end },
       	 "-",
       	 { "Chi Squared Test  chisquaret(l1,l2)",	function() menustring( "chisquaret(" ) end },
       	 { "Z Test  normalt",	function() menustring( "normalt(" ) end },
       	 { "Student Test  studentt",	function() menustring( "studentt(" ) end },
       },
       { "Statistics",
       	 { "Sequence  seq(expr,var,inf,sup)",	function() menustring( "seq(" ) end },
       	 { "Size  size(l)",	function() menustring( "size(" ) end },
       	 { "List to Sequence  op(l)",	function() menustring( "op(" ) end },
       	 { "Apply Function  apply(f,l)",	function() menustring( "apply(" ) end },
       	 { "Append  append(l,expr)",	function() menustring( "append(" ) end },
       	 { "Concatenate  concat(l1,l2)",	function() menustring( "concat(" ) end },
       	 { "First Element  head(l)",	function() menustring( "head(" ) end },
       	 { "Tail  tail(l)",	function() menustring( "tail(" ) end },
       	 { "Sort  sort(l[,function])",	function() menustring( "sort(" ) end },
       	 { "Reverse List  revlist(l)",	function() menustring( "revlist(" ) end },
       	 { "Contains  contains(l,expr)",	function() menustring( "contains(" ) end },
       	 { "Suppress  suppress(l,n)",	function() menustring( "suppress(" ) end },
       	 { "Remove  remove(function,l)",	function() menustring( "remove(" ) end },
       	 "-",
       	 { "Bar Plot  bar_plot(data_list)",	function() menustring( "bar_plot(" ) end },
       	 { "Histogram  histogram(data,xmin,size)",	function() menustring( "histogram(" ) end },
       	 { "Scatter Plot  scatterplot(Xlist,Ylist)",	function() menustring( "scatterplot(" ) end },
       	 { "Regression Plot  linear_regression_plot(X,Y)",	function() menustring( "linear_regression_plot(" ) end },
       },
       -- 28 of the 30 a tool box may hold. Two more entries and this one has to split.
       { "Matrix & Vector",
       	 { "Solve Linear System  linsolve",	function() menustring( "linsolve(" ) end },
       	 { "Determinant  det(M)",	function() menustring( "det(" ) end },
       	 { "Inverse  inv(M)",	function() menustring( "inv(" ) end },
       	 { "Row Reduce  rref(M)",	function() menustring( "rref(" ) end },
       	 { "Row Echelon  ref(M)",	function() menustring( "ref(" ) end },
       	 { "Kernel  ker(M)",	function() menustring( "ker(" ) end },
       	 { "Image  image(M)",	function() menustring( "image(" ) end },
       	 { "Bug  bug(M)",	function() menustring( "bug(" ) end },
       	 { "Eigenvalues  eigenvalues(M)",	function() menustring( "eigenvalues(" ) end },
       	 { "Eigenvectors  eigenvects(M)",	function() menustring( "eigenvects(" ) end },
       	 { "Jordan Form  jordan(M)",	function() menustring( "jordan(" ) end },
       	 { "Matrix Power  matpow(M,n)",	function() menustring( "matpow(" ) end },
       	 "-",
       	 { "Dot Product  dot(v1,v2)",	function() menustring( "dot(" ) end },
       	 { "Cross Product  cross(v1,v2)",	function() menustring( "cross(" ) end },
       	 { "Identity  identity(n)",	function() menustring( "identity(" ) end },
       	 { "Build Matrix  matrix(n,m,function)",	function() menustring( "matrix(" ) end },
       	 { "Random Matrix  randmatrix(n,m,law)",	function() menustring( "randmatrix(" ) end },
       	 { "Hilbert Matrix  hilbert(n)",	function() menustring( "hilbert(" ) end },
       	 { "Vandermonde  vandermonde(list)",	function() menustring( "vandermonde(" ) end },
       	 "-",
       	 { "L1 Norm  l1norm(M)",	function() menustring( "l1norm(" ) end },
       	 { "L2 Norm  l2norm(M)",	function() menustring( "l2norm(" ) end },
       	 { "Infinity Norm  linfnorm(M)",	function() menustring( "linfnorm(" ) end },
       	 { "Condition Number  cond(M,1|2|inf)",	function() menustring( "cond(" ) end },
       	 { "LU Decomposition  lu(M)",	function() menustring( "lu(" ) end },
       	 { "QR Decomposition  qr(M)",	function() menustring( "qr(" ) end },
       	 { "Schur Form  schur(M)",	function() menustring( "schur(" ) end },
       	 { "Singular Value Decomposition  svd(M)",	function() menustring( "svd(" ) end },
       	 { "Singular Values  svl(M)",	function() menustring( "svl(" ) end },
       },
       -- Native's Calculator has no Plots category at all, because graphing lives in the Graphs
       -- application. These nine have nowhere native to go and every KhiCAS feature stays.
       { "Plots",
       	 { "Function Plot  plot(f(x),x=a..b)",	function() menustring( "plot(" ) end },
       	 { "Area Plot  plotarea(f(x),x=a..b)",	function() menustring( "plotarea(" ) end },
       	 { "Parametric Plot  plotparam([x,y],t=a..b)",	function() menustring( "plotparam(" ) end },
       	 { "Polar Plot  plotpolar(r(theta),theta=a..b)",	function() menustring( "plotpolar(" ) end },
       	 { "Slope Field  plotfield(f(t,y),t=t1..t2)",	function() menustring( "plotfield(" ) end },
       	 { "Differential Equation Plot  plotode(f,t,y)",	function() menustring( "plotode(" ) end },
       	 { "Density Plot  plotdensity(f(x,y),x,y)",	function() menustring( "plotdensity(" ) end },
       	 { "Contour Plot  plotcontour(f(x,y),x,y)",	function() menustring( "plotcontour(" ) end },
       	 { "Implicit Plot  plotimplicit(f(x,y),x,y)",	function() menustring( "plotimplicit(" ) end },
       },
}
toolpalette.register(menu)

--------------------------------------------------------------------- Ki V3: step-by-step modes
-- The shell above is Ki V1. From here down, nps_split.luax.tns supplies a derivation for the three
-- families it covers, and a viewer shows it one step at a time: a focused step in the list, the
-- step on its own with its fuller explanation, its restrictions and what checked it, and a detail
-- level that decides how much of that appears (PRD sections 9 and 17, UI-003, STEP-009, STEP-010,
-- MATH-005). No expression is parsed here, which is section 12.3's boundary: the shell hands the
-- typed text to the module as it is, and the only text inspected is the prefix that names a mode.
--
-- A request is "!d expr", "!i expr" or "!s equation", or any line at all once a mode has been
-- chosen from the Steps menu. "!v name" changes the variable, "!?" lists these.

-- The module verifies its own package against a sidecar before it registers anything, and on a
-- failure it registers only its diagnostics. So loading it says nothing about whether it can be
-- called: what it says about itself has to be read first (PLAT-012).
STEP_EXPORTS = { "differentiate", "integrate", "solve", "kinematics" }

stepSurfaceError = nil
hasSteps = false
local moduleLoaded = pcall(nrequire, "nps_split")
if not moduleLoaded then
	stepSurfaceError = "nps_split.luax.tns is not in the ndl folder"
elseif type(nps_split) ~= "table" then
	stepSurfaceError = "StepCAS module is outdated or incomplete (invalid module table)"
elseif type(nps_split.integrity_status) ~= "function" then
	stepSurfaceError = "StepCAS module incompatible (missing integrity status)"
else
	local integrityOk, integrity = pcall(nps_split.integrity_status)
	if not integrityOk or type(integrity) ~= "string" then
		stepSurfaceError = "StepCAS integrity status malformed"
	elseif integrity ~= "verified" then
		stepSurfaceError = "StepCAS unavailable (integrity: " .. integrity .. ")"
	else
		local missing = nil
		for _, name in ipairs(STEP_EXPORTS) do
			if not missing and type(nps_split[name]) ~= "function" then missing = name end
		end
		if not missing and type(nps_split.capability_manifest) ~= "function" then
			missing = "capability_manifest"
		end
		if missing then
			stepSurfaceError = "StepCAS module is outdated or incomplete (missing " .. missing .. ")"
		else
			local manifestOk, manifest = pcall(nps_split.capability_manifest)
			if not manifestOk or type(manifest) ~= "table" then
				stepSurfaceError = "StepCAS manifest malformed (expected table)"
			elseif manifest.artifact ~= "split" then
				stepSurfaceError = "StepCAS module incompatible (artifact must be split)"
			elseif manifest.schema_version ~= 2 then
				stepSurfaceError = "StepCAS module incompatible (capability schema must be 2)"
			elseif type(manifest.symbolic_backend) ~= "table" or
			       manifest.symbolic_backend.interface_id ~= "lua5.1.luagiac.caseval-v1" then
				stepSurfaceError = "StepCAS module incompatible (backend interface)"
			else
				hasSteps = true
			end
		end
	end
end

steps = {
	mode = nil,
	variable = "x",
	detail = 1,
	result = nil,
	rows = nil,
	focus = 1,
	view = "list",
	scroll = 0,
	active = false,
	status = nil,
	histText = {},
	pendingHistory = nil,
	pendingExpression = nil,
}

STEP_MODES = {
	d = { key = "differentiate", label = "d/dx" },
	i = { key = "integrate", label = "integral" },
	s = { key = "solve", label = "solve" },
	k = { key = "kinematics", label = "kinematics" },
}
STEP_DETAILS = { "standard", "beginner" }
STEP_LINE = 15
STEP_MARGIN = 3

local function stepModeLabel(key)
	for _, m in pairs(STEP_MODES) do
		if m.key == key then return m.label end
	end
	return key or "giac"
end

local function trimLeft(s)
	while s:sub(1, 1) == " " do s = s:sub(2) end
	return s
end

function stepRequest(expr)
	local s = trimLeft(expr)
	if s:sub(1, 1) == "!" then
		local letter = s:sub(2, 2)
		local rest = trimLeft(s:sub(3))
		if letter == "v" then return { mode = "variable", text = rest } end
		if letter == "g" then return { mode = "plain", text = rest } end
		if letter == "!" then return { mode = "reopen", text = rest } end
		local m = STEP_MODES[letter]
		-- A prefix with nothing after it sets the mode. The step controls live on this line rather
		-- than in the tool palette, so every one of them has to be reachable by typing.
		if m and rest == "" then return { mode = "setmode", text = m.key } end
		if m then return { mode = m.key, text = rest } end
		return { mode = "help", text = rest }
	end
	if steps.mode then return { mode = steps.mode, text = s } end
	return nil
end

-- The answer line carries its own trust, because section 17 wants an unverified result labelled
-- rather than presented like a checked one. A cross-check that is not Giac answering the same
-- question is named for what it did, so an integral does not read as two agreeing antiderivatives.
-- An unlisted method falls back to plain Giac rather than claiming a check it did not run.
local GIAC_METHOD_NAMES = {
	["differentiated the answer"] = "Giac's derivative",
	["solved the substituted equation"] = "Giac's own solve",
}

local function stepVerdict(r)
	local who = (r.giac_method and GIAC_METHOD_NAMES[r.giac_method]) or "Giac"
	if r.agrees == true then return who .. " agrees" end
	if r.agrees == false then return string.upper(who) .. " DISAGREES" end
	if r.giac_tag == "unavailable" then return "not cross-checked" end
	if r.giac_tag and r.giac_raw and r.giac_raw ~= "" then
		return "Giac: " .. r.giac_tag .. " <" .. r.giac_raw .. ">"
	end
	if r.giac_tag then return "Giac: " .. r.giac_tag end
	return "not cross-checked"
end

-- A step contributes more than one line: its rule and goal, then the expression it produced, and
-- at the beginner level its short explanation too. Each row remembers which step it belongs to, so
-- the focus can move by step while the screen scrolls by row. Only the first step's before is
-- shown, since every later one repeats the previous step's after.
local function buildRows(r)
	local rows = {}
	local started = false
	for i, s in ipairs(r.steps or {}) do
		rows[#rows + 1] = { step = i, kind = "text", text = s.name .. ": " .. s.goal, depth = s.depth,
		                    verified = s.verified, failed = s.failed }
		if STEP_DETAILS[steps.detail] == "beginner" and s.short and s.short ~= "" then
			rows[#rows + 1] = { step = i, kind = "why", text = s.short, depth = s.depth + 1,
			                    verified = s.verified, failed = s.failed }
		end
		if not started and s.before and s.before ~= "" then
			rows[#rows + 1] = { step = i, kind = "math", text = s.before, depth = s.depth + 1,
			                    verified = s.verified, failed = s.failed }
			started = true
		end
		if s.after and s.after ~= "" then
			rows[#rows + 1] = { step = i, kind = "math", text = s.after, depth = s.depth + 1,
			                    verified = s.verified, failed = s.failed }
			started = true
		end
	end
	return rows
end

-- The history editors are OS widgets drawn over the canvas, and setVisible(false) alone left them
-- painted over the viewer on the emulator, so they are moved off screen the way destroyD2Editor
-- parks one. The shell's size-change listeners call reposME after a new entry lands and would
-- move them straight back, so reposME itself parks them while the viewer is up.
local function parkEditors()
	for _, e in ipairs(histME1) do e.editor:move(-10000, -10000) end
	for _, e in ipairs(histME2) do e.editor:move(-10000, -10000) end
	if fctEditor then fctEditor.editor:move(-10000, -10000) end
end

local baseReposME = reposME
function reposME()
	if steps.active then
		parkEditors()
		return
	end
	baseReposME()
end

local function setEditorsVisible(flag)
	for _, e in ipairs(histME1) do e.editor:setVisible(flag) end
	for _, e in ipairs(histME2) do e.editor:setVisible(flag) end
	if fctEditor then fctEditor.editor:setVisible(flag) end
	-- Native greys out an Edit command it cannot carry out, so Copy and Paste follow the editors.
	toolpalette.enableCopy(flag)
	toolpalette.enablePaste(flag)
	if inited then reposME() end
end

-- The viewer hides the editors while it is up and gives the input line its focus back when it
-- closes.
function openSteps()
	if not steps.result then return end
	steps.active = true
	if fctEditor then fctEditor.editor:setFocus(false) end
	setEditorsVisible(false)
	platform.window:invalidate()
end

function closeSteps()
	steps.active = false
	setEditorsVisible(true)
	if theView and fctEditor then theView:setFocus(fctEditor) end
	forcefocus = true
	platform.window:invalidate()
end

function stepsSetMode(key)
	steps.mode = key
	steps.status = key and ("every enter: " .. stepModeLabel(key) .. " steps in " .. steps.variable)
	                    or nil
	platform.window:invalidate()
end

function stepsToggleDetail()
	steps.detail = steps.detail % #STEP_DETAILS + 1
	if steps.result then steps.rows = buildRows(steps.result) end
	platform.window:invalidate()
end

-- What enterHandler calls for a request. The text that comes back goes into the history's answer
-- box, the way a Giac answer does, and the derivation opens in the viewer.
function runSteps(mode, text)
	if mode == "variable" then
		if text ~= "" then steps.variable = text end
		steps.status = "steps in " .. steps.variable
		return "variable " .. steps.variable
	end
	if mode == "setmode" then
		stepsSetMode(text)
		return "every enter: " .. stepModeLabel(text)
	end
	if mode == "plain" then
		stepsSetMode(nil)
		steps.status = "plain Giac"
		return steps.status
	end
	if mode == "reopen" then
		if not steps.result then return "no steps yet" end
		openSteps()
		return steps.status or "steps"
	end
	if mode == "help" then
		return "!d !i !s expr, !k find v; v0 = 5 m/s; ..., bare !d !i !s !k sets the mode, " ..
		       "!g plain Giac, !v name, !! last steps. " ..
		       "In a kinematics line v0 is the starting speed, v the final speed, " ..
		       "a the acceleration, t the time and x the distance travelled."
	end
	if not hasSteps then
		steps.status = stepSurfaceError or "nps_split.luax.tns is not in the ndl folder"
		return steps.status
	end
	-- The startup gate checks the four named modes. A mode reaching here that is not one of them
	-- would still be a nil call, so the request is checked against what was actually registered.
	if type(nps_split[mode]) ~= "function" then
		steps.status = "StepCAS module is outdated or incomplete (missing " .. mode .. ")"
		return steps.status
	end
	if text == "" then return "nothing to work on" end

	-- PERF-006 wants total latency on target hardware with everything resident, which is this call.
	local t0 = timer.getMilliSecCounter()
	local r, why = nps_split[mode](text, steps.variable)
	local t1 = timer.getMilliSecCounter()
	if not r then
		steps.status = "steps: " .. tostring(why)
		return steps.status
	end
	r.total_ms = t1 - t0
	r.heap_kb = math.floor(collectgarbage("count"))
	r.mode = mode
	r.input = text
	steps.result = r
	steps.rows = buildRows(r)
	steps.focus = 1
	steps.scroll = 0
	steps.view = "list"
	steps.status = stepVerdict(r)
	openSteps()
	return r.canonical or r.result or r.outcome
end

-- Greedy word wrap against the real string width, so a long explanation reads as lines rather
-- than running off the right edge.
local function wrapText(gc, text, width)
	local lines = {}
	local line = ""
	for word in string.gmatch(text, "%S+") do
		local candidate = line == "" and word or (line .. " " .. word)
		if line ~= "" and gc:getStringWidth(candidate) > width then
			lines[#lines + 1] = line
			line = word
		else
			line = candidate
		end
	end
	if line ~= "" then lines[#lines + 1] = line end
	return lines
end

local function headerLineCount()
	local n = 5
	if steps.result and steps.result.assumptions then n = n + 1 end
	return n
end

local function visibleRows(h)
	return mathmax(1, math.floor((h - (headerLineCount() + 1) * STEP_LINE) / STEP_LINE))
end

local function followFocus(h)
	local rows = steps.rows or {}
	local first, last = nil, nil
	for i, row in ipairs(rows) do
		if row.step == steps.focus then
			first = first or i
			last = i
		end
	end
	if not first then return end
	local visible = visibleRows(h)
	if first - 1 < steps.scroll then steps.scroll = first - 1 end
	if last > steps.scroll + visible then steps.scroll = last - visible end
end

local function paintStepsHeader(gc, w)
	local r = steps.result
	gc:setColorRGB(0, 0, 0)
	gc:drawString("Ki V3 steps  " .. stepModeLabel(r.mode) .. " in " .. steps.variable .. "  " ..
	              STEP_DETAILS[steps.detail] .. "  (enter opens, esc back, shift+tab detail)",
	              STEP_MARGIN, 0, "top")
	local y = STEP_LINE
	gc:setColorRGB(90, 90, 90)
	gc:drawString(r.input or "", STEP_MARGIN, y, "top")
	y = y + STEP_LINE

	gc:setColorRGB(0, 0, 0)
	gc:drawString(r.canonical or r.result or r.outcome, STEP_MARGIN, y, "top")
	y = y + STEP_LINE

	if r.agrees == false then gc:setColorRGB(180, 0, 0) else gc:setColorRGB(90, 90, 90) end
	local note = r.outcome .. "  |  " .. stepVerdict(r)
	if not r.solved and r.detail and r.detail ~= "" then note = note .. "  |  " .. r.detail end
	gc:drawString(note, STEP_MARGIN, y, "top")
	y = y + STEP_LINE

	if r.assumptions then
		gc:setColorRGB(140, 80, 0)
		gc:drawString("assumes: " .. r.assumptions, STEP_MARGIN, y, "top")
		y = y + STEP_LINE
	end

	-- The measurement line: the whole call including the cross-check, and the counts PERF-010
	-- freezes budgets against.
	gc:setColorRGB(60, 60, 140)
	gc:drawString(string.format("%d ms  nodes %d  steps %d  giac %d  heap %dk", r.total_ms or -1,
	                            r.nodes or -1, r.step_count or -1, r.giac_calls or -1, r.heap_kb or -1),
	              STEP_MARGIN, y, "top")
	y = y + STEP_LINE
	gc:setColorRGB(200, 200, 200)
	gc:drawLine(STEP_MARGIN, y, w - STEP_MARGIN, y)
	return y + 2
end

local function rowColor(gc, row)
	if row.failed then
		gc:setColorRGB(180, 0, 0)
	elseif row.kind == "math" then
		gc:setColorRGB(0, 0, 140)
	elseif row.kind == "why" then
		gc:setColorRGB(90, 90, 90)
	elseif row.verified then
		gc:setColorRGB(0, 0, 0)
	else
		gc:setColorRGB(120, 120, 120)
	end
end

-- The native list shows a scrollbar when it overflows, so a list that scrolls silently reads as one
-- that ends where the screen does.
local function paintScrollbar(gc, w, top, height, total, visible, scroll)
	if total <= visible or height <= 0 then return end
	local x = w - 3
	gc:setColorRGB(200, 200, 200)
	gc:fillRect(x, top, 3, height)
	local thumb = mathmax(6, math.floor(height * visible / total))
	local y = top + math.floor((height - thumb) * scroll / (total - visible))
	gc:setColorRGB(90, 90, 90)
	gc:fillRect(x, y, 3, thumb)
end

local function paintStepsList(gc, w, h, y)
	local rows = steps.rows or {}
	local top = y
	followFocus(h)
	local visible = visibleRows(h)
	for i = 1, visible do
		local row = rows[i + steps.scroll]
		if not row then break end
		if row.step == steps.focus then
			gc:setColorRGB(220, 230, 250)
			gc:fillRect(STEP_MARGIN, y, w - 2 * STEP_MARGIN, STEP_LINE)
		end
		rowColor(gc, row)
		local mark = (row.kind == "text" and not row.verified) and "  (unverified)" or ""
		gc:drawString(row.text .. mark, STEP_MARGIN + row.depth * 10, y, "top")
		y = y + STEP_LINE
	end
	paintScrollbar(gc, w, top, visible * STEP_LINE, #rows, visible, steps.scroll)
	if steps.result.steps_truncated then
		gc:setColorRGB(180, 0, 0)
		gc:drawString("step list cut short: the record was too deep to hand over", STEP_MARGIN, y, "top")
	end
	if #rows > visible then
		gc:setColorRGB(120, 120, 120)
		gc:drawString(string.format("%d-%d of %d", steps.scroll + 1,
		                            mathmin(steps.scroll + visible, #rows), #rows),
		              w - 70, h - STEP_LINE, "top")
	end
end

-- One step on its own. Standard is the rule, the goal, the reason and the algebra; beginner adds
-- the fuller explanation, the restrictions the rule relies on, what checked it and which rule.
local function stepLines(gc, w)
	local all = steps.result.steps or {}
	local s = all[steps.focus]
	if not s then return {} end
	local width = w - 2 * STEP_MARGIN
	local out = {}
	local function add(text, color)
		for _, ln in ipairs(wrapText(gc, text, width)) do
			out[#out + 1] = { text = ln, color = color }
		end
	end
	local black, grey, blue, red, brown = {0, 0, 0}, {90, 90, 90}, {0, 0, 140}, {180, 0, 0}, {140, 80, 0}
	local trust = s.failed and "  FAILED ITS CHECK" or (s.verified and "" or "  (unverified)")
	add(string.format("Step %d of %d: %s%s", steps.focus, #all, s.name, trust), s.failed and red or black)
	add("Goal: " .. s.goal, black)
	if s.short and s.short ~= "" then add("Why: " .. s.short, grey) end
	if s.before and s.before ~= "" then add("From: " .. s.before, blue) end
	if s.after and s.after ~= "" then add("To: " .. s.after, blue) end
	if STEP_DETAILS[steps.detail] == "beginner" then
		if s.detail and s.detail ~= "" then add(s.detail, black) end
		if s.domain and s.domain ~= "" then add("Assumes: " .. s.domain, brown) end
		if s.checks and s.checks ~= "" then add("Checked by: " .. s.checks, grey) end
		if s.rule then add("Rule " .. s.rule .. ", claim: " .. tostring(s.claim), grey) end
	end
	return out
end

-- An open step scrolls by line with up and down, since a beginner explanation can run past the
-- screen, and left and right move between steps.
local function paintStep(gc, w, h, y)
	local lines = stepLines(gc, w)
	local room = math.floor((h - y - STEP_LINE) / STEP_LINE)
	steps.stepScroll = mathmax(0, mathmin(steps.stepScroll or 0, #lines - room))
	for i = 1, room do
		local ln = lines[i + steps.stepScroll]
		if not ln then break end
		gc:setColorRGB(ln.color[1], ln.color[2], ln.color[3])
		gc:drawString(ln.text, STEP_MARGIN, y, "top")
		y = y + STEP_LINE
	end
	gc:setColorRGB(120, 120, 120)
	local more = #lines > room and string.format("%d-%d of %d lines, ", steps.stepScroll + 1,
	                                             mathmin(steps.stepScroll + room, #lines), #lines) or ""
	gc:drawString(more .. "left/right: other steps  up/down: scroll  esc: list", STEP_MARGIN,
	              h - STEP_LINE, "top")
end

local function paintSteps(gc)
	local w = platform.window:width()
	local h = platform.window:height()
	gc:setColorRGB(255, 255, 255)
	gc:fillRect(0, 0, w, h)
	gc:setFont("sansserif", "r", 9)
	local y = paintStepsHeader(gc, w)
	if steps.view == "step" then
		paintStep(gc, w, h, y)
	else
		paintStepsList(gc, w, h, y)
	end
end

local function moveFocus(delta)
	local n = #(steps.result.steps or {})
	if n == 0 then return end
	steps.focus = mathmax(1, mathmin(n, steps.focus + delta))
	steps.stepScroll = 0
end

-- Up and down: the focus in the list, the text in an open step.
local function moveDown(delta)
	if steps.view == "step" then
		steps.stepScroll = mathmax(0, (steps.stepScroll or 0) + delta)
	else
		moveFocus(delta)
	end
end

-- Viewer keys run under pcall: an error inside a handler unwinds into the OS and resets the
-- calculator, and a reset leaves nothing on screen to read. Trapped, it closes the viewer and puts
-- the message in the status line.
local function guarded(f)
	return function(...)
		local ok, err = pcall(f, ...)
		if not ok then
			steps.status = "steps: " .. tostring(err)
			closeSteps()
		end
		platform.window:invalidate()
	end
end

-- The shell's handlers, kept so the viewer can hand every key back to them when it is closed.
local baseOn = {
	paint = on.paint, enterKey = on.enterKey, escapeKey = on.escapeKey, tabKey = on.tabKey,
	backtabKey = on.backtabKey, arrowUp = on.arrowUp, arrowDown = on.arrowDown,
	arrowLeft = on.arrowLeft, arrowRight = on.arrowRight, charIn = on.charIn,
}

-- UI-010: a saved document reopens with its history and with the line left unentered. addME and the
-- input editor both need the view, which the first paint builds, so both wait here for it.
local function replayHistory()
	if not inited then return end
	if steps.pendingHistory then
		local pending = steps.pendingHistory
		steps.pendingHistory = nil
		dispinfos = false
		for _, pair in ipairs(pending) do addME(pair[1], pair[2]) end
	end
	if steps.pendingExpression and fctEditor then
		local text = steps.pendingExpression
		steps.pendingExpression = nil
		fctEditor:addString(text)
	end
end

function on.paint(gc)
	if steps.active then
		local ok, err = pcall(paintSteps, gc)
		if not ok then
			gc:setFont("sansserif", "r", 7)
			gc:setColorRGB(180, 0, 0)
			gc:drawString("steps paint failed: " .. tostring(err), 2, 2, "top")
		end
		return
	end
	baseOn.paint(gc)
	replayHistory()
	if steps.status then
		local status = paintableFailure(steps.status)
		gc:setFont("sansserif", "r", 9)
		gc:setColorRGB(60, 60, 140)
		gc:drawString(status, scrWidth - gc:getStringWidth(status) - 8, 0, "top")
	end
end

-- Enter opens the focused step and esc closes it again, so the two keys walk in and out of the
-- record; with the viewer closed every key is the shell's.
on.enterKey = guarded(function()
	if not steps.active then return baseOn.enterKey() end
	if steps.view == "list" then
		if #(steps.result.steps or {}) > 0 then
			steps.view = "step"
			steps.stepScroll = 0
		end
	else
		steps.view = "list"
	end
end)
on.returnKey = on.enterKey

on.escapeKey = guarded(function()
	if not steps.active then return baseOn.escapeKey() end
	if steps.view == "step" then
		steps.view = "list"
	else
		closeSteps()
	end
end)

-- Measured on the emulator: an arrow reaches the four named handlers and never on.arrowKey, so one
-- press is one call and the viewer has no double dispatch to defend against.
local function viewerArrow(key)
	if key == "down" then moveDown(1)
	elseif key == "up" then moveDown(-1)
	elseif key == "right" then moveFocus(1)
	elseif key == "left" then moveFocus(-1)
	end
end

on.arrowDown = guarded(function()
	if not steps.active then return baseOn.arrowDown() end
	viewerArrow("down")
end)
on.arrowUp = guarded(function()
	if not steps.active then return baseOn.arrowUp() end
	viewerArrow("up")
end)
on.arrowRight = guarded(function()
	if not steps.active then return baseOn.arrowRight() end
	viewerArrow("right")
end)
on.arrowLeft = guarded(function()
	if not steps.active then return baseOn.arrowLeft() end
	viewerArrow("left")
end)

on.tabKey = guarded(function()
	if not steps.active then return baseOn.tabKey() end
end)
on.backtabKey = guarded(function()
	if not steps.active then return baseOn.backtabKey() end
	stepsToggleDetail()
end)
on.help = guarded(function() stepsToggleDetail() end)

-- Focus opens on item 1, so the entry a student can undo comes first and delete cannot be reached by
-- a reflex enter. The menu consumes escape itself, so on.escapeKey never sees the dismissal.
on.contextMenu = guarded(function()
	if not inited or steps.active then return end
	local focused = theView and theView:getFocus()
	if not focused or focused == fctEditor or not focused.editor then return end
	local chosen = osMenu("History Entry", { "Copy to Entry Line", "Delete Entry" })
	if chosen == 1 then
		enterHandler(focused)
	elseif chosen == 2 then
		backSpaceHandler(focused)
	end
end)

on.charIn = guarded(function(ch)
	if not steps.active then return baseOn.charIn(ch) end
end)

-- The document keeps the history, the variable, the mode, the detail level and the line typed but not
-- yet entered, which is the work a student notices missing first. The derivation is
-- recomputed on the next request rather than saved, since the core is deterministic and a saved
-- record would be a copy of what it produces. A restored table is data (section 17): every field
-- is type checked and ranged before it is used.
function on.save()
	local expression
	if fctEditor and fctEditor.editor and fctEditor.editor:getExpression() then
		local typed = fctEditor:getExpression()
		if type(typed) == "string" and typed ~= "" then expression = typed end
	end
	return { variable = steps.variable, mode = steps.mode, detail = steps.detail,
	         history = steps.histText, expression = expression }
end

function on.restore(saved)
	if type(saved) ~= "table" then return end
	if type(saved.variable) == "string" and saved.variable ~= "" then
		steps.variable = saved.variable
	end
	if saved.mode == nil or saved.mode == "differentiate" or saved.mode == "integrate"
		or saved.mode == "solve" or saved.mode == "kinematics" then
		steps.mode = saved.mode
	end
	if type(saved.detail) == "number" and saved.detail >= 1 and saved.detail <= #STEP_DETAILS
		and saved.detail == math.floor(saved.detail) then
		steps.detail = saved.detail
	end
	if type(saved.history) == "table" then
		local pending = {}
		for _, pair in ipairs(saved.history) do
			if type(pair) == "table" and type(pair[1]) == "string" and type(pair[2]) == "string" then
				pending[#pending + 1] = { pair[1], pair[2] }
			end
		end
		if #pending > 0 then steps.pendingHistory = pending end
	end
	if type(saved.expression) == "string" and saved.expression ~= "" then
		steps.pendingExpression = saved.expression
	end
	if steps.mode then steps.status = "every enter: " .. stepModeLabel(steps.mode) .. " steps in " .. steps.variable end
end
