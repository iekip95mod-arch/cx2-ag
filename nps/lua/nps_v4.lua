---------------------
-- "Ki V4" : Ki V3 against one unified module, nps_nspire.luax.tns, with Giac linked into it.
-- The shell and the step modes are Ki V3's. What differs is where the maths lives: V3 loads
-- luagiac and nps_split as two images and a backend call inside a derivation goes back out through
-- Lua to reach Giac, while V4 loads one image whose table carries both, so that call is direct.
-- See .Internal/ki-v4.md.
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

-- Ki V4: one module. nrequire("nps_nspire") loads nps_nspire.luax.tns, whose main registers a single table holding
-- both the step entries and Giac's own caseval, because Giac is linked into that image rather than
-- living in a second one. There is no luagiac here and nothing to load twice, so a backend call
-- inside a derivation is a direct call rather than a trip back out through the interpreter.
-- A successful basename lookup can still find an older module, so both surfaces are checked here.
local failureDetails
local fitHeaderText
local function conciseFailure(value)
	local message = tostring(value or "unknown error")
	failureDetails = message
	local lf = message:find("\n", 1, true)
	local cr = message:find("\r", 1, true)
	local lineEnd = lf
	if cr and (not lineEnd or cr < lineEnd) then lineEnd = cr end
	if lineEnd then message = message:sub(1, lineEnd - 1) end
	if message == "" then message = "unknown error" end
	if #message > 96 then message = message:sub(1, 93) .. "..." end
	return message
end

local requiredSolvers = {
	{ "differentiate", "calculus.derivative.single-variable" },
	{ "integrate", "calculus.integral.indefinite.single-variable" },
	{ "solve", "algebra.linear-equation.one-unknown" },
	{ "solve", "algebra.quadratic.pure-square.one-unknown" },
	{ "walkthrough", "algebra.formula-rearrangement.single-occurrence" },
	{ "walkthrough", "algebra.polynomial-rewrite.single-expression" },
	{ "walkthrough", "number.integer-method.literal" },
	{ "kinematics", "physics.kinematics.constant-acceleration.one-dimension" },
	{ "unit_conversion", "units.chain-link-conversion" },
	{ "density", "physics.density.mass-volume" },
	{ "vector_addition", "physics.vectors.cartesian-addition.two-dimension" },
	{ "relative_motion", "physics.kinematics.relative-motion.components.two-dimension" },
	{ "work", "physics.work.constant-force-dot-product" },
	{ "magnitude_angle_to_components", "physics.vectors.magnitude-components.two-dimension" },
	{ "catch_up", "physics.kinematics.catch-up.equal-position" },
}

local function manifestCompatibility(manifest)
	if type(manifest) ~= "table" then return "StepCAS manifest malformed (expected table)" end
	if manifest.artifact ~= "unified" then return "StepCAS module incompatible (artifact must be unified)" end
	if manifest.schema_version ~= 2 then return "StepCAS module incompatible (capability schema must be 2)" end
	local backend = manifest.symbolic_backend
	if type(backend) ~= "table" then return "StepCAS manifest malformed (symbolic_backend)" end
	if backend.interface_id ~= "nps.giac.typed-v1" then
		return "StepCAS module incompatible (backend interface)"
	end
	if type(backend.version) ~= "string" or backend.version == "" then
		return "StepCAS manifest malformed (backend version)"
	end

	if type(manifest.schema_versions) ~= "table" then
		return "StepCAS manifest malformed (schema_versions)"
	end
	if #manifest.schema_versions > 16 then return "StepCAS manifest malformed (too many schemas)" end
	local hasCapabilitySchema = false
	local hasContextSchema = false
	for index, schema in ipairs(manifest.schema_versions) do
		if type(schema) ~= "table" or type(schema.id) ~= "string" or type(schema.version) ~= "number" then
			return "StepCAS manifest malformed (schema entry " .. tostring(index) .. ")"
		end
		if schema.id == "capability-manifest" and schema.version == 2 then hasCapabilitySchema = true end
		if schema.id == "solution-context" and schema.version == 3 then hasContextSchema = true end
	end
	if not hasCapabilitySchema then
		return "StepCAS module incompatible (missing capability-manifest v2 schema)"
	end
	if not hasContextSchema then
		return "StepCAS module incompatible (missing solution-context v3 schema)"
	end

	if type(manifest.installed_modules) ~= "table" then
		return "StepCAS manifest malformed (installed_modules)"
	end
	if #manifest.installed_modules > 32 then return "StepCAS manifest malformed (too many modules)" end
	local installed = {}
	for index, entry in ipairs(manifest.installed_modules) do
		if type(entry) ~= "table" or type(entry.id) ~= "string" then
			return "StepCAS manifest malformed (module entry " .. tostring(index) .. ")"
		end
		installed[entry.id] = true
	end
	for _, solver in ipairs(requiredSolvers) do
		if not installed[solver[2]] then
			return "StepCAS module incompatible (missing " .. solver[2] .. ")"
		end
	end
	if not installed["units.si"] then return "StepCAS module incompatible (missing units.si)" end
	return nil
end

local ndlResident = type(nrequire) == "function"
local moduleLoaded = false
local moduleLoadError = nil
if ndlResident then
	moduleLoaded, moduleLoadError = pcall(nrequire, "nps_nspire")
end
local moduleTable = moduleLoaded and type(nps_nspire) == "table"
hasGiac = false

local hasStepSurface = false
local stepSurfaceError = nil
local stepSurfaceRemedy = "Install the matching module and digest."
local stepManifest = nil
if not ndlResident then
	stepSurfaceError = "Ndl is not loaded on this calculator."
	stepSurfaceRemedy = "Open the ndl folder and run the installer."
elseif not moduleLoaded then
	stepSurfaceError = "StepCAS module load failed: " .. conciseFailure(moduleLoadError)
	if tostring(moduleLoadError):find("was found but would not load", 1, true) then
		stepSurfaceRemedy = "Restart the calculator. If it still fails, check the module and digest."
	end
elseif not moduleTable then
	stepSurfaceError = "StepCAS module is outdated or incomplete (invalid module table)"
else
	if type(nps_nspire.integrity_status) ~= "function" then
		stepSurfaceError = "StepCAS module incompatible (missing integrity status)"
	else
		local integrityOk, integrity = pcall(nps_nspire.integrity_status)
		if not integrityOk or type(integrity) ~= "string" then
			stepSurfaceError = "StepCAS integrity status malformed"
		elseif integrity ~= "verified" then
			stepSurfaceError = "StepCAS unavailable (integrity: " .. conciseFailure(integrity) .. ")"
		else
			hasGiac = type(nps_nspire.caseval) == "function"
			local missing = nil
			if not hasGiac then missing = "caseval" end
			if not missing and type(nps_nspire.walkthrough) ~= "function" then missing = "walkthrough" end
			for _, solver in ipairs(requiredSolvers) do
				if not missing and type(nps_nspire[solver[1]]) ~= "function" then missing = solver[1] end
			end
			if not missing and type(nps_nspire.capability_manifest) ~= "function" then
				missing = "capability_manifest"
			end
			if missing then
				stepSurfaceError = "StepCAS module is outdated or incomplete (missing " .. missing .. ")"
			else
				local manifestOk, manifest = pcall(nps_nspire.capability_manifest)
				if not manifestOk then
					stepSurfaceError = "StepCAS manifest failed: " .. conciseFailure(manifest)
				else
					stepSurfaceError = manifestCompatibility(manifest)
					if not stepSurfaceError then
						hasStepSurface = true
						stepManifest = manifest
					end
				end
			end
		end
	end
end

-- PERF-010's launch-time free memory, taken here because this is the last moment before anything
-- else runs. An outside probe cannot see this state on the handheld, which has no exec.
local function readHeapFree()
	if not hasStepSurface or type(nps_nspire.heap_free) ~= "function" then return nil end
	local ok, heap = pcall(nps_nspire.heap_free)
	if not ok or type(heap) ~= "table" then return nil end
	return heap
end

local function heapText(heap)
	if not heap then return "" end
	return string.format("free %d/%dk%s", heap.total_kb or -1, heap.largest_kb or -1,
	                     heap.truncated and " floor" or "")
end

local launchHeap = readHeapFree()

local resourceProfileEnabled = moduleTable and
	type(nps_nspire.resource_profile_begin) == "function" and
	type(nps_nspire.resource_profile_finish) == "function"

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
-- A local, so the memo belongs to the document that scanned it rather than to whichever ran first.
local giacVer, giacVersionDetails

local function giacRuntimeVersion()
	if giacVer == nil then
		giacVer = ""
		if hasGiac then
			local ok, s = pcall(nps_nspire.caseval, "version()")
			if type(s) == "string" then giacVersionDetails = ok and s or "Query failed: " .. s
			else giacVersionDetails = "Unexpected " .. type(s) .. " response" end
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
	return giacVer
end

local function versionShaped(s)
	local digit = false
	for i = 1, string.len(s) do
		local c = string.sub(s, i, i)
		if c >= "0" and c <= "9" then
			digit = true
		elseif c ~= "." then
			return false
		end
	end
	return digit
end

function giacLabel()
	local running = giacRuntimeVersion()
	if not versionShaped(running) then return "Giac :" end
	return "Giac " .. running .. " :"
end

-- PLAT-010. The manifest carries the version this artefact was built around, and this is the one
-- that answered. Anything unreadable fails the comparison too, and is named rather than quoted back.
local function backendVersionRefusal()
	if not hasStepSurface then return nil end
	local declared = stepManifest.symbolic_backend.version
	local running = giacRuntimeVersion()
	-- Both numbers fit the launch screen's 50 character paint budget, which the longer
	-- "StepCAS module incompatible (...)" wording the load-time refusals use would not.
	if not versionShaped(running) then
		return "StepCAS needs Giac " .. declared .. ", found no version"
	end
	if running ~= declared then
		return "StepCAS needs Giac " .. declared .. ", found " .. running
	end
	return nil
end

-- One answer for every surface that solves. The load-time refusals are decided before the UI is up
-- and the version cannot be, so a caller asking whether it may solve has to ask about both. The
-- default remedy fits this one: the artefact carries both the manifest and the backend that answered.
function stepRefusal()
	return stepSurfaceError or backendVersionRefusal()
end

-- PLAT-001. The module reads the model, the build and the Ndl revision off the calculator it is
-- running on, and nothing drew them, so the one requirement about which handheld this is could not
-- be read from the handheld. Memoised: none of the three can change while the document is open.
local deviceLine, deviceMismatch

local function readDeviceIdentity()
	if deviceLine ~= nil then return end
	deviceLine, deviceMismatch = "", false
	if type(nps_nspire) ~= "table" or type(nps_nspire.device_identity) ~= "function" then return end
	local ok, d = pcall(nps_nspire.device_identity)
	if not ok or type(d) ~= "table" then return end
	deviceLine = "Nspire " .. tostring(d.model) .. " " .. tostring(d.cas) .. ", OS " ..
	             tostring(d.os) .. ", Ndl r" .. tostring(d.ndl_revision)
	deviceMismatch = d.model_agrees_with_os == false
end

function deviceIdentityLine()
	readDeviceIdentity()
	if deviceLine == "" then return nil end
	return deviceLine
end

-- Drawn apart and in red rather than folded into the line above, because a model the OS disagrees
-- with is the reading being wrong rather than one more field in it.
function deviceIdentityMismatch()
	readDeviceIdentity()
	return deviceMismatch
end

chanProbe = {}

local function chanTry(label, cmd)
	local ok, res = pcall(nps_nspire.caseval, cmd)
	chanProbe[#chanProbe + 1] = label .. " => " .. (ok and tostring(res) or "PCALL FAIL")
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
		pcall(nps_nspire.caseval,
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
		elseif self.currentFocus == f then
			-- tabForward wrapped back onto the widget being removed, so nothing is left to focus.
			self.currentFocus = 0
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
			obj:setFocus()
			self:invalidate()
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
-- stack overflow in a key handler resets the calculator.
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
local routeOverlayEvent

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
	local filters = {
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
		-- Through the handler, like escape below, so the viewer's guard covers a key the OS sends here.
		enterKey = function()
			on.enterKey()
			return true
		end,
		returnKey = function()
			on.returnKey()
			return true
		end,
		escapeKey = function()
			on.escapeKey()
			return true
		end,
		help = function()
			on.help()
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
	}
	for event, handler in pairs(filters) do
		filters[event] = function(...)
			if routeOverlayEvent(event, ...) then return true end
			return handler(...)
		end
	end
	self.editor:registerFilter(filters)
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
	if self.historyExpression then return self.historyExpression end
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

function MathEditor:resize(w, h)
	w, h = mathmax(2, w), mathmax(2, h)
	RichTextEditor.resize(self, w, mathmax(2, h - (self.fullTextCueHeight or 0)))
	Widget.resize(self, w, h)
end

function MathEditor:fitInput()
	local width = mathmax(2, scrWidth - self.x - (self.dx1 or 0))
	-- Keep history reachable while the native input owns the caret.
	local height = mathmax(2, math.floor(scrHeight / 2))
	local fits = (self.needw or 0) <= width - 1 and (self.needh or self.h) <= height - 1
	self.fullTextCueHeight = fits and 0 or mathmin(strFullHeight + 2, mathmax(0, height - 2))
	local h = mathmax(strFullHeight + 8, (self.needh or strFullHeight) + 1 + self.fullTextCueHeight)
	self:resize(width, mathmin(height, h))
end

function MathEditor:fitHistory(width, height)
	if self.historyWidth ~= width or self.historyHeight ~= height or self.historyFont ~= fsize then
		self.historyWidth, self.historyHeight, self.historyFont = width, height, fsize
		self.historyMode, self.fullTextCueHeight = "math", 0
		self:resize(width, height)
		self.editor:setFontSize(fsize)
		self.needw, self.needh = nil, nil
		self.editor:setExpression("\\0el {" .. self.historyExpression .. "}", 0)
	end
	if self.historyMode == "math" and self.needw and self.needh
	   and (self.needw > width - 1 or self.needh > height - 1) then
		self.historyMode = "text"
		self.editor:setWordWrapWidth(0)
		self.needw, self.needh = nil, nil
		self.editor:setText(self.historyExpression)
	end
	local measured = self.needw and self.needh
	local fits = measured and self.needw <= width - 1 and self.needh <= height - 1
	self.fullTextCueHeight = fits and 0 or strFullHeight + 2
	local h = mathmax(strFullHeight + 8, (self.needh or strFullHeight) + 1 + self.fullTextCueHeight)
	-- Shrinking the measured width can reflow native mathematics and clip it again.
	self:resize(width, mathmin(height, h))
	self.editor:setVisible(measured ~= nil)
end

function MathEditor:paint(gc)
	if not self.editor then self = nil; return; end
	if showHLines and not self.result then
		gc:setColorRGB(100, 100, 100)
		local ycoord = self.y - (showEditorsBorders and 0 or 2)
		gc:drawLine(1, ycoord, platform.window:width() - sbv.w - 2, ycoord)
		gc:setColorRGB(0)
	end
	if self.fullTextCueHeight and self.fullTextCueHeight > 0 then
		gc:setFont("sansserif", "r", 8)
		local cue = "HELP: full text"
		if gc:getStringWidth(cue) > self.w then cue = "HELP: text" end
		if gc:getStringWidth(cue) > self.w then cue = "HELP" end
		local y = self.y + self.h - self.fullTextCueHeight
		if y >= 0 and y + gc:getStringHeight(cue) <= scrHeight then
			gc:drawString(cue, self.x, y, "top")
		end
		initFontGC(gc)
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
	inited = false
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
			glabel = fitHeaderText(gc, glabel, scrWidth - 35)
			gc:drawString(glabel, 2, 0, "top")
			if hasGiac and versionShaped(giacRuntimeVersion()) and not backendVersionRefusal() then
				gc:setColorRGB(0, 127, 0)
				gc:drawString("OK.", gc:getStringWidth(glabel) + 6, 0, "top")
			else
				gc:setColorRGB(255, 0, 0)
				gc:drawString("NO.", gc:getStringWidth(glabel) + 6, 0, "top")
			end
			local line = strHeight
			if launchHeap then
				gc:setColorRGB(0)
				gc:drawString(fitHeaderText(gc, heapText(launchHeap), scrWidth - 4), 2, line, "top")
				line = line + strHeight
			end
			local identity = deviceIdentityLine()
			if identity then
				gc:setColorRGB(0)
				gc:drawString(fitHeaderText(gc, identity, scrWidth - 4), 2, line, "top")
				line = line + strHeight
				if deviceIdentityMismatch() then
					gc:setColorRGB(180, 0, 0)
					gc:drawString(fitHeaderText(gc, "This model and this OS are not a pair StepCAS knows",
					                          scrWidth - 4), 2, line, "top")
					line = line + strHeight
				end
			end
			-- A refusal can fire after integrity passes, with Giac itself up, so the reason is drawn
			-- whenever there is one rather than only when Giac is absent.
			local refusal = stepRefusal()
			if refusal then
				gc:setColorRGB(180, 0, 0)
				gc:drawString(fitHeaderText(gc, refusal, scrWidth - 4), 2, line, "top")
				gc:drawString(fitHeaderText(gc, stepSurfaceRemedy, scrWidth - 4), 2, line + strHeight, "top")
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
local HISTORY_MAX_ENTRIES = 50

local function removeHistoryAt(index)
	local first, second = histME1[index], histME2[index]
	if not first or not second or not steps.histText[index] then return false end
	destroyD2Editor(first.editor)
	destroyD2Editor(second.editor)
	theView:remove(first)
	theView:remove(second)
	table.remove(histME1, index)
	table.remove(histME2, index)
	table.remove(steps.histText, index)
	return true
end

function addME(expr, res)
	local mee = MathEditor(theView, border, border, 50, 30, "")
	mee.readOnly = true
	mee.historyExpression = expr
	mee:setHConstraints("left", border)
	mee.editor:setSizeChangeListener(function(editor, w, h)
		return resizeME(editor, w, h)
	end)
	mee.editor:setReadOnly(true)

	local allocated, mer = pcall(MathEditor, theView, border, border, 50, 30, "")
	if not allocated then
		destroyD2Editor(mee.editor)
		error(mer, 0)
	end
	mer.result = true
	mer.readOnly = true
	mer.historyExpression = res
	mer:setHConstraints("right", scrWidth - sbv.x + border)
	mer.editor:setSizeChangeListener(function(editor, w, h)
		return resizeMEpar(editor, w, h)
	end)
	mer.editor:setReadOnly(true)

	table.insert(histME1, mee)
	table.insert(histME2, mer)
	table.insert(steps.histText, { expr, res })
	theView:add(mee)
	theView:add(mer)
	if #histME1 > HISTORY_MAX_ENTRIES then removeHistoryAt(1) end
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
	end
	if a then
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
		removeHistoryAt(f)
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
				-- Ki V4: a step request goes to the module's step entries, anything else to its caseval.
				local request = stepRequest(expr)
				if request then
					res = runSteps(request.mode, request.text)
				else
					-- The first Enter clears the launch banner, so its refusal has to be restated.
					local refusal = stepRefusal()
					steps.status = refusal
					res = refusal or nps_nspire.caseval(expr) or "Error"
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
		theView.historyReflow = true
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
	-- Native size callbacks can arrive while these editors are being measured.
	if theView.historyLayout then return end
	theView.historyLayout = true
	local h, y, ry, res, i0, beforeh, totalh, visih, h1, h2
	totalh = 0
	beforeh = 0
	visih = 0
	fctEditor:fitInput()
	fctEditor.y = scrHeight - fctEditor.h
	theView:repos(fctEditor)
	local width = mathmax(1, math.floor((scrWidth - sbv.w - 3 * border) / 2))
	local height = mathmax(1, fctEditor.y - border - 2)
	for _, editor in ipairs(histME1) do editor:fitHistory(width, height) end
	for _, editor in ipairs(histME2) do editor:fitHistory(width, height) end
	sbv:setVConstraints("justify", scrHeight - fctEditor.y + border)
	theView:repos(sbv)
	y = fctEditor.y
	i0 = mathmax(#histME1, #histME2)
	-- Reflow can move a focused row without a navigation key being pressed.
	if theView.historyReflow then
		theView.historyReflow = false
		local focused = theView:getFocus()
		if focused and focused ~= fctEditor then
			local index = getMEindex(focused)
			ioffset = mathmin(ioffset, index)
			local room = fctEditor.y
			for i = i0 - ioffset, i0 - index, -1 do
				local first, second = histME1[i], histME2[i]
				room = room - mathmax(first and first.h or 0, second and second.h or 0) - border
			end
			while room < 0 and ioffset < index do
				local i = i0 - ioffset
				local first, second = histME1[i], histME2[i]
				room = room + mathmax(first and first.h or 0, second and second.h or 0) + border
				ioffset = ioffset + 1
			end
		end
	end
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
	theView.historyLayout = false
	theView:invalidate()
end

function destroyD2Editor(editor)
	if not editor then return end
	editor:setVisible(false)
	editor:move(-10000, -10000)
	editor:resize(1, 1)
	editor = nil
end

local function releaseHistory()
	for _, history in ipairs({histME1, histME2}) do
		for _, row in ipairs(history) do
			if theView then theView:remove(row) end
			destroyD2Editor(row.editor)
		end
	end
	histME1, histME2, steps.histText = {}, {}, {}
end

-- The OS draws these, so they cannot drift from the native dialogs, and they are absent when the module is.
local function osMsgbox(message, first, second, third)
	if type(nps_nspire) ~= "table" or type(nps_nspire.os_msgbox) ~= "function" then return nil end
	local shown, pressed = pcall(nps_nspire.os_msgbox, "Ki", message, first, second, third)
	if not shown then return nil end
	return pressed
end

-- The framework draws this one, the same list widget Giac uses for its own menus. Measured on the
-- emulator: escape closes it in a single press and returns nil, so a dismissal is not a selection.
local function osMenu(title, items)
	if type(nps_nspire) ~= "table" or type(nps_nspire.os_menu) ~= "function" then return nil end
	local shown, chosen = pcall(nps_nspire.os_menu, title, items)
	if not shown then return nil end
	return chosen
end

local textReader = { active = false, stepScroll = 0, paragraphs = {} }
local templatePicker = { active = false, selection = 1 }
local updateOverlayEditors

function reset()
	for _, v in pairs(theView.widgetList) do
		theView:remove(v)
	end
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
	theView:releaseFocus(theView:getFocus())
	releaseHistory()
	ioffset = 0
	if textReader.active then textReader.focus = fctEditor end
	updateOverlayEditors()
end

function myErrorHandler(line, errMsg, callStack, locals)
	if errMsg then print(errMsg) end
	defaultFocus = nil
	releaseHistory()
	destroyD2Editor(fctEditor and fctEditor.editor)
	theView = nil
	collectgarbage()
	initGUI()
	if errMsg and inited then addME("Script error", errMsg) end
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
	if not fctEditor then return false end
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

-- The palette is registered for the whole document, so the menu key opens it over the viewer too,
-- where the input line is parked off screen. A palette entry that types into a line the student
-- cannot see is worse than one that declines, so every entry that types goes through here and this
-- is where the refusal lives.
function menustring( ch )
    if templatePicker.active then return false end
    if steps.active or physicsBrowser.active or textReader.active then
		steps.status = textReader.active and "close Full Text first, then the menu types into the entry line"
		               or "close the steps first, then the menu types into the entry line"
		platform.window:invalidate()
		return false
	end
	if not fctEditor then return false end
	fctEditor:addString( ch )
	return true
end

-- MATH-011's templates. Measured on the emulator: the division key gives a literal slash and typed
-- content stays flat, but re-setting the whole expression makes the OS typeset it, so an inserted
-- skeleton comes up as a real stacked fraction with the caret inside it. See
-- benchmarks/d2_probe4.lua and native-ui/d2-template-inserted.png. addString already splices and
-- re-sets, so a template is that plus putting the caret back inside the shape it just made.
function template( skeleton, back )
	if not menustring( skeleton ) then return end
	if not back or back <= 0 then return end
	local currentText, curpos = fctEditor.editor:getExpressionSelection()
	if not currentText then return end
	fctEditor.editor:setExpression( currentText, mathmax( 0, curpos - back ) )
end

menu = {
       -- Ki V4: the physics slice. The label says what the entry finds in a beginner's words and
       -- choosing it drops the command into the input editor, where the syntax is theirs to edit.
       -- Which equation applies is still the solver's verdict from what is given.
       { "Physics",
        { "Guided physics problems", function() openPhysicsFixtures() end },
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
       -- MATH-011's templates half. Each one drops the linear form the engine reads and the OS
       -- redraws it in its two-dimensional shape, with the caret already in the box the student
       -- fills first. The power entry duplicates a keypad affordance rather than replacing one:
       -- the caret key opens a superscript box on its own, which is measured in STATUS.md.
       { "Templates",
       	 { "Fraction, one number over another",	function() template( "()/()", 4 ) end },
       	 { "Power, a number raised to another",	function() template( "^()", 1 ) end },
       	 { "Square root",	function() template( "sqrt()", 1 ) end },
       	 { "Cube root, and other roots",	function() template( "surd(,3)", 3 ) end },
       	 { "Derivative, how fast something changes",	function() template( "(,x)", 3 ) end },
       	 { "Integral, the area under a curve",	function() template( "∫(,x)", 3 ) end },
       	 { "Integral between two limits",	function() template( "∫(,x,0,1)", 7 ) end },
       	 { "Equation, two sides that are equal",	function() template( "=", 0 ) end },
       	 "-",
       	 { "Unit: metres",	function() template( " m", 0 ) end },
       	 { "Unit: seconds",	function() template( " s", 0 ) end },
       	 { "Unit: kilograms",	function() template( " kg", 0 ) end },
       	 { "Unit: metres per second",	function() template( " m/s", 0 ) end },
       	 { "Unit: metres per second squared",	function() template( " m/s^2", 0 ) end },
       	 { "Unit: newtons, for force",	function() template( " N", 0 ) end },
       	 { "Unit: joules, for energy and work",	function() template( " J", 0 ) end },
         { "Limit at a point", function() template("limit(,x,0)", 5) end },
         { "Limit from the left", function() template("limit(,x,0,-1)", 8) end },
         { "Limit from the right", function() template("limit(,x,0,1)", 7) end },
         { "Limit at positive infinity", function() template("limit(,x,infinity)", 12) end },
         { "Limit at negative infinity", function() template("limit(,x,-infinity)", 13) end },
       },
       { "Steps",
        { "Full walkthrough (all steps)", function() stepsSetProgression("full") end },
        { "Hint walkthrough (Tab next)", function() stepsSetProgression("hint") end },
       },
       -- Native wording first, the callable form after it. A tool palette has no submenu, second
       -- line or tooltip, so where the pair runs past 44 characters the argument spelling gives way
       -- and never the native name. 44 comes from budgets' emulator measurement: English is complete
       -- at 47 and cut at 50, all-W is cut at 25, and the cut is silent with no ellipsis.
       { "Actions",
       	 { "Open Shell  *",	function() menustring( "*" ) end },
       	 { "Open Script Editor",	function() menustring( "+\"\"" ) end },
		 { "Read Full Text", function() readFullText() end },
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
         { "Browse templates", function() openTemplatePicker() end },
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
       -- 27 of the 30 a tool box may hold. Three more entries and this one has to split.
       { "Matrix & Vector",
       	 { "Solve Linear System  linsolve",	function() menustring( "linsolve(" ) end },
       	 { "Determinant  det(M)",	function() menustring( "det(" ) end },
       	 { "Inverse  inv(M)",	function() menustring( "inv(" ) end },
       	 { "Row Reduce  rref(M)",	function() menustring( "rref(" ) end },
       	 { "Row Echelon  ref(M)",	function() menustring( "ref(" ) end },
       	 { "Kernel  ker(M)",	function() menustring( "ker(" ) end },
       	 { "Image  image(M)",	function() menustring( "image(" ) end },
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

--------------------------------------------------------------------- Ki V4: step-by-step modes
-- The shell above is Ki V1. From here down, nps_nspire.luax.tns supplies a derivation for the four
-- families it covers, and a viewer shows it one step at a time: a focused step in the list, the
-- step on its own with its fuller explanation, its restrictions and what checked it, and a detail
-- level that decides how much of that appears (PRD sections 9 and 17, UI-003, STEP-009, STEP-010,
-- MATH-005). No solver expression is parsed here, which is section 12.3's boundary: the shell hands
-- it to the module as typed, while inspecting only the command prefix and the !v identifier.
--
-- A request is "!d expr", "!i expr" or "!s equation", or any line at all once a mode has been
-- chosen as a mode. "!h on" selects hints and "!h off" restores the full walkthrough.

-- One module, so one load. The shell asked for it above and the step entries are in the same table
-- as the caseval it is already using.
hasSteps = hasStepSurface

steps = {
	mode = nil,
	automatic = true,
	variable = "x",
	detail = 1,
	progression = "full",
	walkthrough = "full",
	revealed = 0,
	result = nil,
	rows = nil,
	focus = 1,
	view = "list",
	scroll = 0,
	collapsed = {},
	visibleSteps = {},
	active = false,
	status = nil,
	recordStatus = nil,
	statusResult = nil,
	histText = {},
	pendingHistory = nil,
	pendingExpression = nil,
	pendingResourceProfile = nil,
	resourceProfileError = nil,
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
-- Mirrors core is_identifier because save and restore must validate without invoking a solver.
STEP_VARIABLE_MAX_BYTES = 4096
STEP_VARIABLE_ERROR = "variable must be a single identifier without whitespace"

function isStepsVariable(value)
	if type(value) ~= "string" or value == "" or #value > STEP_VARIABLE_MAX_BYTES then return false end
	if value == "\207\128" or value == "\226\136\158" then return true end
	local byte = value:byte(1)
	if not ((byte >= 65 and byte <= 90) or (byte >= 97 and byte <= 122) or byte == 95) then
		return false
	end
	for i = 2, #value do
		byte = value:byte(i)
		if not ((byte >= 48 and byte <= 57) or (byte >= 65 and byte <= 90) or
		        (byte >= 97 and byte <= 122) or byte == 95) then
			return false
		end
	end
	return true
end

local function startResourceProfile(operation)
	steps.pendingResourceProfile = nil
	steps.resourceProfileError = nil
	if not resourceProfileEnabled then return nil end
	local ok, active = pcall(nps_nspire.resource_profile_begin, operation)
	if not ok or active ~= true then
		steps.resourceProfileError = ok and "profile begin rejected operation" or conciseFailure(active)
		return nil
	end
	return timer.getMilliSecCounter()
end

local function armResourceProfile(operation, startedAt, profiledAt, r)
	steps.pendingResourceProfile = { operation = operation, startedAt = startedAt,
	                                 profiled = profiledAt ~= nil, result = r }
end

local function finishResourceProfile()
	local pending = steps.pendingResourceProfile
	if not pending then return end
	steps.pendingResourceProfile = nil
	local renderReady = timer.getMilliSecCounter() - pending.startedAt
	local r = pending.result
	-- PERF-005 asks for the first step a student can read, so this runs to the frame rather than to
	-- the solver returning, and it runs in every build: PERF-011 wants the release configuration,
	-- which a profile build is not.
	r.render_ready_ms = renderReady
	if not pending.profiled then return end
	local luaLiveBytes = math.floor(collectgarbage("count") * 1024)
	local metrics = {
		operation = pending.operation,
		request_failed = r.request_failed == true,
		solver_metrics_available = type(r.nodes) == "number" and type(r.child_slots) == "number"
			and type(r.step_count) == "number" and type(r.rewrites) == "number"
			and type(r.giac_calls) == "number",
		render_ready_ms = renderReady,
		lua_live_bytes = luaLiveBytes,
	}
	if metrics.solver_metrics_available then
		metrics.arena_nodes = r.nodes
		metrics.arena_child_slots = r.child_slots
		metrics.derivation_steps = r.step_count
		metrics.rewrites = r.rewrites
		metrics.backend_calls = r.giac_calls
	end
	local ok, report = pcall(nps_nspire.resource_profile_finish, metrics)
	if ok then
		r.resource_profile = report
	else
		r.resource_profile_error = conciseFailure(report)
	end
end

local exactPhysicsPrecision = { kind = "exact", significant_digits = 0 }

PHYSICS_FIXTURES = {
	{
		label = "Change units when the unit is cubed",
		problem = "How many cubic metres is 2.50 cubic centimetres? The number changes by a lot, " ..
		          "because the length shrinks three times over.",
		mode = "unit_conversion",
		run = function() return nps_nspire.unit_conversion("2.50 cm^3", "m^3") end,
	},
	{
		label = "Find a mass from a density and a volume",
		problem = "A block of 3 cubic centimetres is made of something with 2 grams packed into " ..
		          "every cubic centimetre. How heavy is the block?",
		mode = "density",
		run = function() return nps_nspire.density("mass", "density", "2 g/cm^3", "volume", "3 cm^3") end,
	},
	{
		label = "Find how fast something ends up going",
		problem = "It is already moving at 5 metres per second and it gains another 3 metres per " ..
		          "second every second, for 4 seconds. How fast is it going at the end?",
		mode = "kinematics",
		run = function()
			return nps_nspire.kinematics("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s", "x")
		end,
	},
	{
		label = "Add two movements pointing different ways",
		problem = "Two movements, one measured in kilometres and one in metres. Adding them means " ..
		          "matching the units first, then adding the across and the up parts separately.",
		mode = "vector_addition",
		run = function()
			return nps_nspire.vector_addition("(0.00120, 0.0020) km", "5 i + 5 j m")
		end,
	},
	{
		label = "Find the work done by a push at an angle",
		problem = "Work is how much a push actually achieves. The push and the movement point " ..
		          "different ways here, so only part of the push counts, and it can come out negative.",
		mode = "work",
		run = function()
			return nps_nspire.work({
				force = { x = "2", y = "-3", rank = 2, frame = "lab", unit = "N",
				          precision = exactPhysicsPrecision },
				displacement = { x = "-1", y = "4", rank = 2, frame = "lab", unit = "m",
				                 precision = exactPhysicsPrecision },
				force_profile = "constant",
			})
		end,
	},
	{
		label = "Split a speed into sideways and up parts",
		problem = "Something moves at 10 metres per second, aimed 30 degrees above flat. How much " ..
		          "of that is sideways and how much is upwards?",
		mode = "magnitude_angle_to_components",
		run = function()
			return nps_nspire.magnitude_angle_to_components({
				magnitude = "10", angle = "30", rank = 2, frame = "lab", unit = "m/s",
				precision = exactPhysicsPrecision, angle_unit = "degrees",
			})
		end,
	},
	{
		label = "See what happens when units cannot match",
		problem = "Asking to turn a length into a time. There is no answer to this, and the point " ..
		          "is to see it refuse and say why rather than hand back a number.",
		mode = "unit_conversion",
		run = function() return nps_nspire.unit_conversion("3 m", "s") end,
	},
	{
		label = "Find when a fast runner catches a slow one",
		problem = "Atlas sets off at 2 metres per second. Boreal starts 5 seconds later at 4 " ..
		          "metres per second. When are they side by side, and how far along?",
		mode = "catch_up",
		run = function()
			return nps_nspire.catch_up({
				first = {
					name = "Atlas",
					frame = "track",
					position = "0 m",
					velocity = "2 m/s",
					start_time = "0 s",
					motion = "constant_velocity",
				},
				second = {
					name = "Boreal",
					frame = "track",
					position = "0 m",
					velocity = "4 m/s",
					start_time = "5 s",
					motion = "constant_velocity",
				},
			})
		end,
	},
	{
		label = "Find a speed seen from something moving",
		problem = "A drone flies through moving air. Its speed over the ground is one thing and " ..
		          "its speed through the air is another, and the wind is the difference.",
		mode = "relative_motion",
		run = function()
			return nps_nspire.relative_motion({
				subject_name = "drone",
				reference_name = "wind",
				subject_velocity = {
					x = "36", y = "-18", rank = 2, frame = "ground", unit = "km/h",
					precision = exactPhysicsPrecision,
				},
				reference_velocity = {
					x = "3", y = "1", rank = 2, frame = "ground", unit = "m/s",
					precision = exactPhysicsPrecision,
				},
			})
		end,
	},
}

physicsBrowser = {
	active = false,
	focus = 1,
	scroll = 0,
	status = nil,
}

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
		if letter == "m" then return { mode = "manifest", text = rest } end
		if letter == "h" then return { mode = "progression", text = rest } end
		-- Diagnostic. The typed adapter path only exists in this build and cannot be exercised on
		-- the host, so this is where its evidence comes from: it runs every allowlisted operation
		-- through the typed path and the string path and reports where they differ.
		if letter == "t" then return { mode = "typedcheck", text = rest } end
		local m = STEP_MODES[letter]
		-- An empty solver prefix selects its mode without invoking the solver.
		if m and rest == "" then return { mode = "setmode", text = m.key } end
		if m then return { mode = m.key, text = rest } end
		return { mode = "help", text = rest }
	end
	if steps.mode then return { mode = steps.mode, text = s } end
	if steps.automatic then return { mode = "walkthrough", text = expr } end
	return nil
end

-- The answer line carries its own trust, because section 17 wants an unverified result labelled
-- rather than presented like a checked one. A cross-check that is not Giac answering the same
-- question is named for what it did, so an integral does not read as two agreeing antiderivatives.
-- An unlisted method falls back to plain Giac rather than claiming a check it did not run.
local GIAC_METHOD_NAMES = {
	["differentiated the answer"] = "Giac's derivative",
	["solved the substituted equation"] = "Giac's own solve",
	["Giac Simplify and local canonical comparison"] = "Giac's component cross-check",
}

local function giacResultLabel(tag, form)
	if form == "special-function form" then return "exact special-function form" end
	if form == "unevaluated exact form" then return form end
	if form == "numerical approximation" then return form end
	if form == "unsupported symbolic form" then return form end
	return tag
end

local function stepVerdict(r)
	local who = (r.giac_method and GIAC_METHOD_NAMES[r.giac_method]) or "Giac"
	if r.agrees == true then return who .. " agrees" end
	if r.agrees == false then return string.upper(who) .. " DISAGREES" end
	if r.giac_tag == "unavailable" then return "not cross-checked" end
	if r.giac_compare_tag and r.giac_compare_tag ~= "exact" then
		local label = giacResultLabel(r.giac_compare_tag, r.giac_compare_form)
		if r.giac_compare_raw and r.giac_compare_raw ~= "" then
			return who .. " comparison: " .. label .. " <" .. r.giac_compare_raw .. ">"
		end
		return who .. " comparison: " .. label
	end
	if r.giac_tag and r.giac_tag ~= "exact" then
		local label = giacResultLabel(r.giac_tag, r.giac_form)
		if r.giac_raw and r.giac_raw ~= "" then
			return "Giac: " .. label .. " <" .. r.giac_raw .. ">"
		end
		return "Giac: " .. label
	end
	if r.giac_compare_tag then
		return who .. " comparison inconclusive"
	end
	if r.giac_tag and r.giac_raw and r.giac_raw ~= "" then
		return "Giac: " .. giacResultLabel(r.giac_tag, r.giac_form) .. " <" .. r.giac_raw .. ">"
	end
	if r.giac_tag then return "Giac: " .. giacResultLabel(r.giac_tag, r.giac_form) end
	return "not cross-checked"
end

-- A refusal, a halt and a cancellation all carry an outcome, and an outcome is not an answer.
local function hasAnswer(r)
	return r.has_result == true or r.solved or
	       (type(r.result) == "string" and r.result ~= "") or
	       (type(r.canonical) == "string" and r.canonical ~= "") or
	       r.has_components or r.has_polar
end

-- display_result is written here rather than by the bridge, so it has already decided the answer.
local function answerText(r)
	if type(r.display_result) == "string" and r.display_result ~= "" then return r.display_result end
	if not hasAnswer(r) then return nil end
	return r.canonical or r.result or r.outcome
end

local function resultClass(r)
	if r.limit_exists == false then return "DOES NOT EXIST" end
	if r.infinite_limit then return "INFINITE LIMIT" end
	local form = r.result_form
	if form == "unevaluated exact form" then return "UNEVALUATED EXACT" end
	if form == "unsupported symbolic form" then return "UNSUPPORTED" end
	-- A stop the student asked for, whether or not a checked prefix survived it. Ahead of the no
	-- result test below, which would otherwise report their own escape key back to them as a failure.
	if r.outcome == "cancelled" or r.status == "cancelled" then return "STOPPED" end
	if not hasAnswer(r) then return "NO RESULT" end
	local approximate = form == "numerical approximation" or
	                    r.status == "numerically approximated" or
	                    r.giac_tag == "approximate" or
	                    (type(r.precision) == "table" and r.precision.kind == "measured")
	local conditional = r.status == "conditionally solved" or r.giac_tag == "conditional" or
	                    (type(r.assumptions) == "string" and r.assumptions ~= "")
	local classification = form == "special-function form" and "SPECIAL FUNCTION" or
	                       (approximate and "APPROXIMATE" or "EXACT")
	-- A cross check that passed leaves the local status alone, so only agrees says one ran.
	if r.status == "verification failed" then
		classification = classification .. " + CHECK FAILED"
	elseif r.status == "solved but unchecked" then
		classification = classification .. " + UNCHECKED"
	-- Named rather than left to the agrees test below, which a bridge cross-check sets true and so
	-- would badge a corroborated answer as verified. A check that ran is not UNCHECKED either.
	elseif r.status == "solved and corroborated" then
		classification = classification .. " + CORROBORATED"
	elseif r.status ~= "solved and verified" and r.agrees ~= true then
		classification = classification .. " + UNCHECKED"
	end
	if conditional then classification = classification .. " + CONDITIONAL" end
	return classification
end

local function resultNote(r)
	local note = (r.answer_only and "CAS answer  |  " or "") .. resultClass(r) ..
	             "  |  " .. (r.status or r.outcome or "unavailable") .. "  |  " .. stepVerdict(r)
	if not r.solved and r.detail and r.detail ~= "" then note = note .. "  |  " .. r.detail end
	return note
end

local function canonicalStepCount(r)
	return type(r.steps) == "table" and #r.steps or 0
end

local function answerWithoutSteps(r)
	return r.answer_only and canonicalStepCount(r) == 0
end

local function exposedStepCount(r)
	local count = canonicalStepCount(r)
	if steps.walkthrough == "hint" and not answerWithoutSteps(r) then
		return mathmin(count, steps.revealed or 0)
	end
	return count
end

local function finalResultVisible(r)
	if steps.walkthrough ~= "hint" or answerWithoutSteps(r) then return true end
	local count = canonicalStepCount(r)
	return count == 0 or exposedStepCount(r) == count
end

local function walkthroughStatus(r)
	if finalResultVisible(r) then return stepVerdict(r) end
	return string.format("hint %d/%d: Tab reveals next", exposedStepCount(r), canonicalStepCount(r))
end

-- A step contributes more than one line: its rule and goal, then the expression it produced, and
-- at the beginner level its short explanation too. Each row remembers which step it belongs to, so
-- the focus can move by step while the screen scrolls by row. Only the first step's before is
-- shown, since every later one repeats the previous step's after.
-- A closed parent hides later steps until its depth resumes.
local function hasStepChildren(all, index, limit)
	return index < (limit or #all) and all[index + 1].depth > all[index].depth
end

-- Parent results can already include work that later hints have not revealed.
local function projectedStep(r, index)
	local all, limit = r.steps or {}, exposedStepCount(r)
	local s = all[index]
	if not s or index > limit then return nil end
	if limit == #all then return s, false end
	for i = index + 1, #all do
		if all[i].depth <= s.depth then break end
		if i > limit then
			local shown = {}
			for key, value in pairs(s) do shown[key] = value end
			for _, field in ipairs({"action", "after", "short", "detail", "checks", "resolution", "settled_by"}) do
				shown[field] = nil
			end
			return shown, true
		end
	end
	return s, false
end

local function stepPhaseLabel(s)
	if s.phase == "plan" or s.kind == "plan" then return "Plan" end
	if s.phase == "check" or s.kind == "check" then return "Check" end
	if s.kind == "branch" then return "Case" end
	return "Work"
end

-- A case that was ruled out has to say so wherever it is drawn, or a reader takes the condition on
-- screen for an answer. Unresolved carries no evidence, so it is named rather than explained.
local function caseOutcome(s)
	if s.resolution == "rejected" then return "ruled out" end
	if s.resolution == "unresolved" then return "not settled" end
	return nil
end

local function buildRows(r)
	if answerWithoutSteps(r) then
		steps.visibleSteps = {}
		return { { step = 1, kind = "notice", text = "no steps for this one", depth = 0,
		           verified = true, failed = false } }
	end
	local rows = {}
	local visibleSteps = {}
	local started = false
	local hiddenBelow = nil
	local all = r.steps or {}
	local limit = exposedStepCount(r)
	for i = 1, limit do
		local s = projectedStep(r, i)
		if hiddenBelow and s.depth <= hiddenBelow then hiddenBelow = nil end
		if not hiddenBelow then
			local parent = hasStepChildren(all, i, limit)
			local collapsed = parent and steps.collapsed[i] == true
			visibleSteps[#visibleSteps + 1] = i
			rows[#rows + 1] = { step = i, kind = "text", text = s.name .. ": " .. s.goal,
			                        depth = s.depth, phase = stepPhaseLabel(s),
			                        verified = s.verified, failed = s.failed,
			                        branch = parent and (collapsed and "closed" or "open") or nil }
			if not collapsed then
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
				-- A case has neither of those two, so without this row the list names a case and
				-- never shows which one. It leaves started alone: a condition is not a first before.
				if s.case and s.case ~= "" then
					local outcome = caseOutcome(s)
					rows[#rows + 1] = { step = i, kind = "math", depth = s.depth + 1,
					                    text = outcome and (s.case .. "  (" .. outcome .. ")") or s.case,
					                    verified = s.verified, failed = s.failed }
				end
			else
				hiddenBelow = s.depth
			end
		end
	end
	steps.visibleSteps = visibleSteps
	return rows
end

local function prepareWalkthrough(r)
	local count = canonicalStepCount(r)
	steps.result = r
	steps.walkthrough = steps.progression
	steps.revealed = steps.walkthrough == "hint" and count > 0 and 1 or count
	steps.collapsed = {}
	steps.rows = buildRows(r)
	steps.focus = 1
	steps.scroll = 0
	steps.view = "list"
	steps.status = walkthroughStatus(r)
	steps.recordStatus = steps.status
	steps.statusResult = r
end

--------------------------------------------------------------------- two-dimensional display
-- UI-002 and MATH-003 are the same layer seen twice, so every expression the viewer shows goes
-- through here and nothing else in the viewer draws one. The OS does the typesetting: measured on
-- the emulator (benchmarks/d2_probe.lua, native-ui/d2-render-matrix.png), a read-only math box
-- stacks a fraction under a vinculum, raises a superscript, draws a radical, brackets a vector and
-- italicises unit vectors, and it reports the size it needs through its size-change listener.
--
-- Two measurements shape the rest of this. A box too small for its content cannot be scrolled to,
-- so a box is only ever given the size it asked for and shrinks its font rather than clip. And the
-- listener fires while the boxes are still being made, so nothing it calls may assume a finished
-- layout.

-- Giac's spelling does not typeset: int(x^2,x) renders as a flat function call while the integral
-- sign renders with its dx. This rewrites for the eye only, never for the engine, which still sees
-- what the module sent. Whole words only, so a variable named interval keeps its name.
local DISPLAY_WORDS = { int = "∫", integrate = "∫", sqrt = "√", sum = "∑", product = "∏" }
local DISPLAY_CONSTANTS = { pi = "π", infinity = "∞" }
local DISPLAY_RELATIONS = { ["<="] = "≤", [">="] = "≥", ["~="] = "≈", ["=="] = "≡", ["!="] = "≠" }

local function isWordChar(c)
	return (c >= "a" and c <= "z") or (c >= "A" and c <= "Z") or (c >= "0" and c <= "9")
		or c == "_" or (c ~= "" and c:byte() >= 128)
end

local function displayArities(expr)
	local arities, stack = {}, {}
	local quoted, escaped = false, false
	local closing = { ["("] = ")", ["["] = "]", ["{"] = "}" }
	for i = 1, #expr do
		local c, frame = expr:sub(i, i), stack[#stack]
		if quoted then
			if escaped then escaped = false
			elseif c == "\\" then escaped = true
			elseif c == '"' then quoted = false end
		elseif c == '"' then
			quoted = true
			if frame then frame.value = true end
		elseif closing[c] then
			if frame then frame.value = true end
			stack[#stack + 1] = { start = i, close = closing[c], count = 0, valid = true }
		elseif c == ")" or c == "]" or c == "}" then
			if not frame or frame.close ~= c then return {} end
			if c == ")" and frame.valid and frame.value then arities[frame.start] = frame.count + 1 end
			stack[#stack] = nil
		elseif c == "," and frame then
			frame.valid = frame.valid and frame.value
			frame.count, frame.value = frame.count + 1, false
		elseif not c:match("%s") and frame then
			frame.value = true
		end
	end
	return arities
end

local function displayExpression(expr, native)
	if type(expr) ~= "string" or expr == "" then return "" end
	if native and expr:find("^", 1, true) and type(nps_nspire) == "table"
		and type(nps_nspire.math_display) == "function" then
		local ok, display = pcall(nps_nspire.math_display, expr)
		if ok and type(display) == "string" and display ~= "" then expr = display end
	end
	local out = {}
	local arities = displayArities(expr)
	local i, n = 1, #expr
	while i <= n do
		local c = expr:sub(i, i)
		if c == '"' then
			local j = i + 1
			while j <= n do
				local quoted = expr:sub(j, j)
				j = j + 1
				if quoted == "\\" then j = j + 1 elseif quoted == '"' then break end
			end
			out[#out + 1] = expr:sub(i, j - 1)
			i = j
		elseif isWordChar(c) then
			local j = i
			while j <= n and isWordChar(expr:sub(j, j)) do j = j + 1 end
			local word = expr:sub(i, j - 1)
			local sign = DISPLAY_WORDS[word]
			if native and (word == "d" or word == "diff") then sign = "" end
			if native and (word == "limit" or word == "lim") then sign = "lim" end
			local nextToken = j
			while expr:sub(nextToken, nextToken):match("%s") do nextToken = nextToken + 1 end
			local count = arities[nextToken]
			local supported = ((word == "int" or word == "integrate") and (count == 2 or count == 4))
				or ((word == "d" or word == "diff") and (count == 2 or count == 3))
				or ((word == "limit" or word == "lim") and (count == 3 or count == 4))
				or (word == "sqrt" and count == 1)
				or ((word == "sum" or word == "product") and count == 4)
			out[#out + 1] = (sign and supported) and sign
				or DISPLAY_CONSTANTS[word] or word
			i = j
		else
			local relation = DISPLAY_RELATIONS[expr:sub(i, i + 1)]
			out[#out + 1] = relation or c
			i = i + (relation and 2 or 1)
		end
	end
	return table.concat(out)
end

-- Sizes the boxes are allowed to try, largest first. Below the last one a box gives up and the
-- caller falls back to wrapped text, which loses nothing because text wraps and a box does not.
local MATH_FONTS = { 9, 7 }
local DETAIL_MATH_FONTS = { 11, 9, 7 }
local MATH_PREVIEW_BYTES = 2048
local mathBoxes = {}
local mathUsed = {}
local mathLayoutRevision = 0

local function mathBox(slot)
	local box = mathBoxes[slot]
	if box then return box end
	box = { w = 0, h = 0, font = 1, expr = nil, display = "" }
	box.editor = D2Editor.newRichText()
	box.editor:setFontSize(MATH_FONTS[1])
	box.editor:setBorder(0)
	box.editor:setReadOnly(true)
	box.editor:setFocus(false)
	mathBoxes[slot] = box
	return box
end

local function parkMathBox(box)
	box.editor:move(-10000, -10000)
	box.editor:setVisible(false)
end

local function parkMathBoxes()
	for _, box in pairs(mathBoxes) do parkMathBox(box) end
	mathUsed = {}
end

local function measureMath(box)
	mathLayoutRevision = mathLayoutRevision + 1
	box.measurement = (box.measurement or 0) + 1
	local measurement = box.measurement
	box.w, box.h = 0, 0
	box.editor:setSizeChangeListener(nil)
	box.editor:setFontSize(box.fonts[box.font])
	box.editor:resize(box.maxW, STEP_LINE)
	box.editor:setSizeChangeListener(function(_, w, h)
		if box.measurement == measurement and (box.w ~= w or box.h ~= h) then
			box.w, box.h = w, h
			mathLayoutRevision = mathLayoutRevision + 1
			platform.window:invalidate()
		end
	end)
	box.editor:setExpression("\\0el {" .. box.expr .. "}", 0)
	parkMathBox(box)
end

-- Fits an expression and returns its measured box, or nil for text fallback.
--
-- The box is given a width and reports the size the content needs in it, which is not always a
-- size that fits. Measured (benchmarks/d2_probe5.lua), a 250 wide box holding a long unit quantity
-- reported 446 by 36 at font 9 and 237 by 56 at font 7, so the OS breaks the expression when it can
-- and reports one long row when it cannot. Neither dimension is safe to assume.
--
-- What a frame smaller than the reported size does is truncate in silence, with no ellipsis and no
-- way to scroll to the rest, so nothing is placed until both dimensions fit. That is also why the
-- box keeps the width it was measured at rather than shrinking to the reported one: 0.00000250 m^3
-- reported 81 wide in a 240 wide box, and placed at 81 it lost its m^3 to a wrap.
--
-- Synchronous measurements can fit now. Pending callbacks leave the text fallback visible.
local function fitMath(slot, expr, maxW, maxH, fonts)
	if type(expr) == "string" and #expr > MATH_PREVIEW_BYTES then return nil end
	local box = mathBox(slot)
	if box.source ~= expr then
		local display = displayExpression(expr, true)
		box.source, box.display = expr, #display <= MATH_PREVIEW_BYTES and display or nil
	end
	local wanted = box.display
	if not wanted then return nil end
	fonts = fonts or MATH_FONTS
	if box.expr ~= wanted or box.maxW ~= maxW or box.maxH ~= maxH or box.fonts ~= fonts then
		box.expr = wanted
		box.maxW = maxW
		box.maxH = maxH
		box.fonts = fonts
		box.font = 1
		measureMath(box)
	end
	while box.w > 0 and box.h > 0 do
		if box.w <= maxW and box.h <= maxH then return box end
		if box.font >= #box.fonts then break end
		box.font = box.font + 1
		measureMath(box)
	end
	parkMathBox(box)
	return nil
end

local function showMath(slot, box, x, y, maxW)
	mathUsed[slot] = true
	box.editor:move(x, y)
	box.editor:resize(maxW, box.h)
	box.editor:setVisible(true)
	return box.h
end

local function placeMath(slot, expr, x, y, maxW, maxH)
	local box = fitMath(slot, expr, maxW, maxH)
	if box then return showMath(slot, box, x, y, maxW) end
end

-- Anything placed last frame and not this one would otherwise stay on screen, because a math box is
-- an OS widget over the canvas rather than something the next fillRect covers.
local function parkUnusedMath()
	for slot, box in pairs(mathBoxes) do
		if not mathUsed[slot] then parkMathBox(box) end
	end
	mathUsed = {}
end

-- The history editors are OS widgets drawn over the canvas, and setVisible(false) alone left them
-- painted over the viewer on the emulator, so they are moved off screen the way destroyD2Editor
-- parks one. The shell's size-change listeners call reposME after a new entry lands and would
-- move them straight back, so reposME itself parks them while the viewer is up.
local function parkEditors()
	for _, e in ipairs(histME1) do
		e.editor:setVisible(false)
		e.editor:move(-10000, -10000)
	end
	for _, e in ipairs(histME2) do
		e.editor:setVisible(false)
		e.editor:move(-10000, -10000)
	end
	if fctEditor then
		fctEditor.editor:setVisible(false)
		fctEditor.editor:move(-10000, -10000)
	end
end

-- Parked native editors can still receive input while an overlay owns the screen.
routeOverlayEvent = function(event, ...)
	if not steps.active and not physicsBrowser.active and not textReader.active and not templatePicker.active then return false end
	if on[event] then on[event](...) end
	return true
end

local baseReposME = reposME
function reposME()
	if steps.active or physicsBrowser.active or textReader.active or templatePicker.active then
		parkEditors()
		return
	end
	baseReposME()
end

local function setEditorsVisible(flag)
	if not flag then
		for _, e in ipairs(histME1) do e.editor:setFocus(false) end
		for _, e in ipairs(histME2) do e.editor:setFocus(false) end
		if fctEditor then fctEditor.editor:setFocus(false) end
	end
	for _, e in ipairs(histME1) do e.editor:setVisible(flag) end
	for _, e in ipairs(histME2) do e.editor:setVisible(flag) end
	if fctEditor then fctEditor.editor:setVisible(flag) end
	-- Native greys out an Edit command it cannot carry out, so Copy and Paste follow the editors.
	toolpalette.enableCopy(flag)
	toolpalette.enablePaste(flag)
	if inited then reposME() end
end

updateOverlayEditors = function(focused)
	parkMathBoxes()
	local visible = not steps.active and not physicsBrowser.active and not textReader.active and not templatePicker.active
	setEditorsVisible(visible)
	if visible then
		if theView and fctEditor then theView:setFocus(focused or fctEditor) end
		forcefocus = true
	end
	platform.window:invalidate()
end

function templatePicker.close()
    if type(nps_nspire) == "table" and type(nps_nspire.ui_menu_close) == "function" then
        pcall(nps_nspire.ui_menu_close)
    end
    templatePicker.active, templatePicker.pending, templatePicker.image = false, false, nil
    updateOverlayEditors(templatePicker.focus)
end

function templatePicker.fallback()
    templatePicker.failed = true
    templatePicker.close()
    if osMsgbox("The template browser could not open.\nUse Menu, then Templates.") == nil then
        steps.status = "Menu, then Templates"
        platform.window:invalidate()
    end
end

function templatePicker.fail()
    if templatePicker.pending then return end
    templatePicker.failed, templatePicker.pending = true, true
    timer.start(0.01)
end

function templatePicker.create()
    local width, height = platform.window:width(), platform.window:height()
    local ok, opened = pcall(nps_nspire.ui_menu_open, width, height, "Templates", templatePicker.labels,
        templatePicker.descriptions)
    if not ok or not opened then return false end
    local selected, accepted = pcall(nps_nspire.ui_menu_select, templatePicker.selection)
    if not selected or not accepted then return false end
    templatePicker.width, templatePicker.height = width, height
    templatePicker.image = nil
    return true
end

local TEMPLATE_DESCRIPTIONS = {
    ["Fraction"] = "Fill the numerator, then the denominator.",
    ["Power"] = "Enter a base first, then fill the exponent.",
    ["Square root"] = "Fill the expression under the root.",
    ["Cube root"] = "Fill the expression. Change 3 for another root order.",
    ["Derivative"] = "Fill the expression. Change x to your variable.",
    ["Indefinite integral"] = "Find an antiderivative. Fill the expression and variable.",
    ["Integral between two limits"] = "Fill the integrand and variable, then change bounds 0 and 1.",
    ["Equation"] = "Enter the left side first, then fill the right side.",
    ["Unit: metres"] = "Append m to a length value.",
    ["Unit: seconds"] = "Append s to a time value.",
    ["Unit: kilograms"] = "Append kg to a mass value.",
    ["Unit: metres per second"] = "Append m/s to a speed or velocity value.",
    ["Unit: metres per second squared"] = "Append m/s^2 to an acceleration value.",
    ["Unit: newtons"] = "Append N to a force value.",
    ["Unit: joules"] = "Append J to an energy or work value.",
    ["Limit at a point"] = "Fill the expression and variable. Change 0 to the approach point.",
    ["Limit from the left"] = "Approach from smaller values. The last argument is -1.",
    ["Limit from the right"] = "Approach from larger values. The last argument is 1.",
    ["Limit at positive infinity"] = "Find the behavior as the variable increases without bound.",
    ["Limit at negative infinity"] = "Find the behavior as the variable decreases without bound.",
}

function openTemplatePicker()
    if not inited or templatePicker.active or steps.active or physicsBrowser.active or textReader.active then return end
    templatePicker.entries, templatePicker.labels, templatePicker.descriptions = {}, {}, {}
    for i = 2, #menu[2] do
        local entry = menu[2][i]
        if type(entry) == "table" then
            templatePicker.entries[#templatePicker.entries + 1] = entry
            local label = entry[1]:match("^[^,]+")
            if label == "Integral" then label = "Indefinite integral" end
            templatePicker.labels[#templatePicker.labels + 1] = label
            templatePicker.descriptions[#templatePicker.descriptions + 1] = TEMPLATE_DESCRIPTIONS[label] or ""
        end
    end
    templatePicker.focus = theView:getFocus()
    if templatePicker.failed or type(nps_nspire) ~= "table" or type(nps_nspire.ui_menu_open) ~= "function" or
        type(nps_nspire.ui_menu_frame) ~= "function" or type(nps_nspire.ui_menu_select) ~= "function" or
        type(nps_nspire.ui_menu_scroll) ~= "function" or type(nps_nspire.ui_menu_close) ~= "function" or
        type(image) ~= "table" or type(image.new) ~= "function" then return templatePicker.fallback() end
    if not templatePicker.create() then
        return templatePicker.fallback()
    end
    templatePicker.active = true
    updateOverlayEditors()
end

function templatePicker.paint(gc)
    if not templatePicker.pending then
        if templatePicker.width ~= platform.window:width() or templatePicker.height ~= platform.window:height() then
            if not templatePicker.create() then templatePicker.fail() end
        end
        if not templatePicker.pending then
            local ok, encoded, healthy = pcall(nps_nspire.ui_menu_frame)
            if not ok or not healthy then templatePicker.fail()
            elseif encoded then
                local decoded, frame = pcall(image.new, encoded)
                if decoded and frame then templatePicker.image = frame else templatePicker.fail() end
            end
        end
    end
    if templatePicker.image then
        local painted = pcall(gc.drawImage, gc, templatePicker.image, 0, 0)
        if not painted then
            templatePicker.image = nil
            templatePicker.fail()
        end
    end
end

function templatePicker.move(delta)
    if templatePicker.pending then return end
    local next = (templatePicker.selection - 1 + delta) % #templatePicker.entries + 1
    local ok, accepted = pcall(nps_nspire.ui_menu_select, next)
    if not ok or not accepted then return templatePicker.fail() end
    templatePicker.selection = next
end

function templatePicker.scroll(delta)
    if templatePicker.pending then return end
    local ok, accepted, selected = pcall(nps_nspire.ui_menu_scroll, delta)
    if not ok or not accepted or type(selected) ~= "number" or selected ~= math.floor(selected) or
        selected < 1 or selected > #templatePicker.entries then return templatePicker.fail() end
    templatePicker.selection = selected
end

function templatePicker.choose()
    if templatePicker.pending then return end
    local entry = templatePicker.entries[templatePicker.selection]
    templatePicker.close()
    if entry then entry[2]() end
end

function on.timer()
    timer.stop()
    if templatePicker.pending then
        templatePicker.fallback()
    end
end

-- Steps and Physics replace one another. Full Text temporarily covers either.
local function setViewer(viewer)
	if templatePicker.active then templatePicker.close() end
	steps.active = viewer == steps
	steps.detailLayout = nil
	steps.summaryLayout = nil
	physicsBrowser.active = viewer == physicsBrowser
	textReader.active = false
	textReader.paragraphs = {}
	textReader.layout = nil
	textReader.focus = nil
	updateOverlayEditors()
end

function openSteps()
	if not steps.result then return end
	steps.status = steps.statusResult == steps.result and steps.recordStatus
	               or walkthroughStatus(steps.result)
	setViewer(steps)
end

function closeSteps()
	setViewer(nil)
end

local function physicsFixtureAnswer(r)
	if type(r.canonical) == "string" and r.canonical ~= "" then return r.canonical end
	if type(r.result) == "string" and r.result ~= "" then return r.result end
	if r.has_components and type(r.components) == "table" then
		local c = r.components
		if type(c.x) == "string" and type(c.y) == "string" and type(c.unit) == "string" then
			return "(" .. c.x .. " i + " .. c.y .. " j) " .. c.unit
		end
	end
	if r.has_polar and type(r.polar) == "table" then
		local p = r.polar
		if type(p.magnitude) == "string" and type(p.angle) == "string" and
		   type(p.unit) == "string" and type(p.angle_unit) == "string" then
			return p.magnitude .. " " .. p.unit .. " at " .. p.angle .. " " .. p.angle_unit
		end
	end
	return nil
end

local function walkthroughHistoryText(r)
	if not finalResultVisible(r) then
		return string.format("hint ready: %d/%d steps; Tab reveals next",
		                     exposedStepCount(r), canonicalStepCount(r))
	end
	return r.display_result or r.canonical or r.result or r.outcome
end

-- A refusal is prose, so the braces and backslash an editor expression is built from come out.
local function refusalText(reason)
	local out, n = {}, 0
	for i = 1, #reason do
		local c = reason:sub(i, i)
		if c ~= "{" and c ~= "}" and c ~= "\\" then
			n = n + 1
			out[n] = c
		end
	end
	return table.concat(out)
end

-- The typed path records its own refusal, so this is for the surfaces that leave nothing typed.
local function recordRefusal(what, why)
	local reason = refusalText(why or "StepCAS dependency unavailable")
	addME(" " .. what, " " .. reason)
	osMsgbox(what .. ": " .. reason)
	return reason
end

function openPhysicsFixtures()
	local refusal = stepRefusal()
	if refusal then
		steps.status = recordRefusal("Guided physics", refusal)
		platform.window:invalidate()
		return
	end
	physicsBrowser.status = nil
	setViewer(physicsBrowser)
end

local function closePhysicsFixtures()
	setViewer(nil)
end

local function runPhysicsFixture()
	local fixture = PHYSICS_FIXTURES[physicsBrowser.focus]
	local label = "Guided: " .. ((fixture and fixture.label) or tostring(physicsBrowser.focus))
	if not fixture or type(fixture.run) ~= "function" then
		physicsBrowser.status = recordRefusal(label, "fixture unavailable")
		return
	end
	local profileStartedAt = startResourceProfile(fixture.mode)
	local t0 = profileStartedAt or timer.getMilliSecCounter()
	armResourceProfile(fixture.mode, t0, profileStartedAt, { request_failed = true })
	local ok, r = pcall(fixture.run)
	local t1 = timer.getMilliSecCounter()
	if not ok then
		physicsBrowser.status = recordRefusal(label, "solver failed: " .. conciseFailure(r))
		return
	end
	if type(r) ~= "table" or type(r.outcome) ~= "string" or type(r.steps) ~= "table" then
		physicsBrowser.status = recordRefusal(label, "solver returned an invalid record")
		return
	end
	r.total_ms = t1 - t0
	r.heap_kb = math.floor(collectgarbage("count"))
	r.heap_free = readHeapFree()
	r.mode = fixture.mode
	r.input = fixture.problem
	r.display_result = physicsFixtureAnswer(r)
	armResourceProfile(fixture.mode, t0, profileStartedAt, r)
	prepareWalkthrough(r)
	openSteps()
	local prompt = " Guided: " .. fixture.label
	local answer = " " .. walkthroughHistoryText(r)
	addME(prompt, answer)
end

function stepsSetProgression(value)
	if value ~= "full" and value ~= "hint" then return nil end
	steps.progression = value
	steps.automatic = true
	-- A recorded walkthrough keeps the progression it was prepared with, so this one cannot claim it.
	if steps.result and steps.walkthrough ~= value then
		steps.status = "walkthrough: " .. value .. " from the next solve"
	else
		steps.status = value == "hint" and "walkthrough: hint, Tab reveals" or "walkthrough: full"
	end
	if steps.result then
		steps.recordStatus = steps.status
		steps.statusResult = steps.result
	end
	platform.window:invalidate()
	return steps.status
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

-- MATH-012's guard. A record with work in it carries the input it read and the form it normalized
-- to. A refusal typed before parsing finished carries neither, and has nothing a walkthrough shows.
local function invalidExpressionContext(r, text)
	if r.original_expression == nil and r.normalized_expression == nil then
		-- A refusal says what it refused and where it stopped. A record with neither answered nothing.
		if type(r.outcome) ~= "string" or r.outcome == "" or
		   type(r.status) ~= "string" or r.status == "" then
			return true
		end
		return r.solved or r.answer_only or canonicalStepCount(r) > 0 or r.result ~= nil
	end
	return type(r.original_expression) ~= "string" or r.original_expression ~= text or
	       type(r.normalized_expression) ~= "string" or r.normalized_expression == ""
end

-- A solve opens the derivation and returns either its answer or a hint-safe history placeholder.
function runSteps(mode, text)
	if mode == "variable" then
		if not isStepsVariable(text) then
			steps.status = STEP_VARIABLE_ERROR
			return steps.status
		end
		steps.variable = text
		steps.status = "steps in " .. steps.variable
		return "variable " .. steps.variable
	end
	if mode == "setmode" then
		stepsSetMode(text)
		return "every enter: " .. stepModeLabel(text)
	end
	if mode == "plain" then
		stepsSetMode(nil)
		steps.automatic = false
		steps.status = "plain Giac"
		return steps.status
	end
	if mode == "progression" then
		local choice = string.lower(text)
		if choice == "on" or choice == "hint" then return stepsSetProgression("hint") end
		if choice == "off" or choice == "full" then return stepsSetProgression("full") end
		return "hint mode: use !h on or !h off (currently " .. steps.progression .. ")"
	end
	if mode == "reopen" then
		if not steps.result then return "no steps yet" end
		openSteps()
		return steps.status or "steps"
	end
	if mode == "typedcheck" then
		if not hasSteps or not nps_nspire.typed_check then return "no typed path in this build" end
		-- The module writes the report to /ndl/typedcheck.txt.tns itself, because this Lua has
		-- no io library. Only the count comes back here.
		local bad = nps_nspire.typed_check()
		steps.status = "typed check: " .. tostring(bad) .. " disagreements"
		return steps.status
	end
	if mode == "manifest" then
		if not stepManifest then return stepSurfaceError end
		local backend = stepManifest.symbolic_backend
		local modules = stepManifest.installed_modules
		if type(stepManifest.id) ~= "string" or stepManifest.id == "" or
		   type(backend) ~= "table" or type(backend.name) ~= "string" or backend.name == "" or
		   type(backend.version) ~= "string" or backend.version == "" or type(modules) ~= "table" then
			return "capability manifest invalid"
		end
		local build = stepManifest.id:match("([^.]+)$") or stepManifest.id
		if #build > 27 then build = build:sub(1, 12) .. "..." .. build:sub(-12) end
		return stepManifest.artifact .. " " .. build .. ", " .. backend.name .. " " ..
		       backend.version .. ", " .. tostring(#modules) .. " modules"
	end
	if mode == "help" then
		return "!d !i !s expr, !k find v; v0 = 5 m/s; ..., bare !d !i !s !k sets the mode, " ..
		       "!g plain Giac, !v name, !h on|off, !! last steps, !m manifest, !t typed check. " ..
		       "In a kinematics line v0 is the starting speed, v the final speed, " ..
		       "a the acceleration, t the time and x the distance travelled."
	end
	local refusal = stepRefusal()
	if refusal then
		steps.status = refusal
		return steps.status
	end
	if text == "" then return "nothing to work on" end

	-- PERF-006 wants total latency on target hardware with everything resident, which is this call.
	local profileStartedAt = startResourceProfile(mode)
	local t0 = profileStartedAt or timer.getMilliSecCounter()
	armResourceProfile(mode, t0, profileStartedAt, { request_failed = true })
	local r, why = nps_nspire[mode](text, steps.variable)
	local t1 = timer.getMilliSecCounter()
	if r == nil and mode == "walkthrough" and why == nil then
		steps.status = nil
		local answer = nps_nspire.caseval(text) or "Error"
		if profileStartedAt then
			armResourceProfile(mode, t0, profileStartedAt, { total_ms = timer.getMilliSecCounter() - t0 })
		end
		return answer
	end
	if not r then
		steps.status = "steps: " .. tostring(why)
		return steps.status
	end
	if type(r) ~= "table" or (mode == "walkthrough" and (type(r.outcome) ~= "string"
		or type(r.steps) ~= "table" or r.request_expression ~= text or type(r.mode) ~= "string"
		or r.mode == "" or invalidExpressionContext(r, r.original_expression))) then
		steps.status = "steps: solver returned an invalid command record"
		return steps.status
	end
	if (mode == "solve" or mode == "differentiate" or mode == "integrate") and
	   invalidExpressionContext(r, text) then
		steps.status = "steps: solver returned an invalid expression context"
		return steps.status
	end
	r.total_ms = t1 - t0
	r.heap_kb = math.floor(collectgarbage("count"))
	r.heap_free = readHeapFree()
	r.mode = mode == "walkthrough" and r.mode or mode
	r.input = r.original_expression or text
	armResourceProfile(mode, t0, profileStartedAt, r)
	prepareWalkthrough(r)
	openSteps()
	return walkthroughHistoryText(r)
end

-- Greedy word wrap against the real string width, so a long explanation reads as lines rather
-- than running off the right edge.
local function isSpace(c)
	return c == " " or c == "\t" or c == "\n" or c == "\r"
end

-- UI-005 lives here. Breaking on spaces alone leaves an expression as one unbreakable word, and a
-- word wider than the line was then cut at the screen edge with its tail unreachable by any key,
-- which is what a long rule line did. A word too wide for a line of its own is now broken by
-- character, so nothing the viewer draws as text can be off screen. A scanner rather than a
-- pattern, so the cost cannot grow faster than the input.
local function wrapText(gc, text, width)
	text = displayExpression(text)
	local lines = {}
	local line = ""
	local function push()
		if line ~= "" then
			lines[#lines + 1] = line
			line = ""
		end
	end
	-- Steps a whole character at a time, since a break inside a multibyte glyph draws rubbish.
	local function chop(word)
		local part = ""
		local i, n = 1, #word
		while i <= n do
			local j = i + 1
			while j <= n and word:byte(j) >= 128 and word:byte(j) < 192 do j = j + 1 end
			local glyph = word:sub(i, j - 1)
			if part ~= "" and gc:getStringWidth(part .. glyph) > width then
				lines[#lines + 1] = part
				part = glyph
			else
				part = part .. glyph
			end
			i = j
		end
		line = part
	end
	local function place(word)
		local candidate = line == "" and word or (line .. " " .. word)
		if gc:getStringWidth(candidate) <= width then
			line = candidate
		elseif line == "" then
			chop(word)
		else
			push()
			place(word)
		end
	end
	local i, n = 1, #text
	while i <= n do
		while i <= n and isSpace(text:sub(i, i)) do i = i + 1 end
		local j = i
		while j <= n and not isSpace(text:sub(j, j)) do j = j + 1 end
		if j > i then place(text:sub(i, j - 1)) end
		i = j
	end
	push()
	return lines
end

local function wrapSummary(gc, slot, text, width)
	text = type(text) == "string" and text or ""
	local layout = steps.summaryLayout or { entries = {}, bytes = 0, lines = 0 }
	steps.summaryLayout = layout
	local height, metric = gc:getStringHeight("H"), gc:getStringWidth("MWilπ")
	local entry = layout.entries[slot]
	if entry and entry.source == text and entry.width == width and entry.height == height
		and entry.metric == metric and entry.revision == mathLayoutRevision then
		return entry.lines
	end
	if entry then
		layout.bytes = layout.bytes - entry.bytes
		layout.lines = layout.lines - #entry.lines
		layout.entries[slot] = nil
	end
	local lines, bytes = wrapText(gc, text, width), #text
	for _, line in ipairs(lines) do bytes = bytes + #line end
	if layout.bytes + bytes <= 32768 and layout.lines + #lines <= 512 then
		layout.entries[slot] = { source = text, width = width, height = height, metric = metric,
			revision = mathLayoutRevision, lines = lines, bytes = bytes }
		layout.bytes = layout.bytes + bytes
		layout.lines = layout.lines + #lines
	end
	return lines
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

-- For a single-line row, where wrapping would break the grid the row sits in. Everything with room
-- to wrap wraps instead, because this loses the tail and wrapping does not.
fitHeaderText = function(gc, text, width)
	if gc:getStringWidth(text) <= width then return text end
	if gc:getStringWidth("...") > width then return "" end
	local kept, index = "", 1
	while index <= #text do
		local nextIndex = index + 1
		while nextIndex <= #text and text:byte(nextIndex) >= 128 and text:byte(nextIndex) < 192 do
			nextIndex = nextIndex + 1
		end
		local candidate = kept .. text:sub(index, nextIndex - 1)
		if gc:getStringWidth(candidate .. "...") > width then break end
		kept, index = candidate, nextIndex
	end
	return kept .. "..."
end

local function paintPhysicsFixtures(gc)
	local w = platform.window:width()
	local h = platform.window:height()
	local rowHeight = 22
	local listTop = 30
	local footerHeight = 50
	local visible = mathmax(1, math.floor((h - listTop - footerHeight) / rowHeight))
	local focus = physicsBrowser.focus
	if focus - 1 < physicsBrowser.scroll then physicsBrowser.scroll = focus - 1 end
	if focus > physicsBrowser.scroll + visible then physicsBrowser.scroll = focus - visible end

	gc:setColorRGB(255, 255, 255)
	gc:fillRect(0, 0, w, h)
	gc:setFont("sansserif", "b", 10)
	gc:setColorRGB(0, 0, 0)
	gc:drawString("Guided physics", STEP_MARGIN, 2, "top")
	gc:setFont("sansserif", "r", 8)
	gc:setColorRGB(90, 90, 90)
	gc:drawString(fitHeaderText(gc, "Choose a structured example", w - 2 * STEP_MARGIN), STEP_MARGIN, 16, "top")

	physicsBrowser.bands = {}
	for row = 1, visible do
		local index = physicsBrowser.scroll + row
		local fixture = PHYSICS_FIXTURES[index]
		if not fixture then break end
		local y = listTop + (row - 1) * rowHeight
		-- Where each row actually landed, so a tap is tested against what was drawn rather than
		-- against a second copy of the layout arithmetic that can drift from it.
		physicsBrowser.bands[#physicsBrowser.bands + 1] = { top = y, bottom = y + rowHeight,
		                                                    index = index }
		if index == focus then
			gc:setColorRGB(220, 230, 250)
			gc:fillRect(STEP_MARGIN, y, w - 2 * STEP_MARGIN, rowHeight - 1)
		end
		gc:setColorRGB(0, 0, 0)
		local row = tostring(index) .. ". " .. fixture.label
		gc:drawString(fitHeaderText(gc, row, w - 2 * STEP_MARGIN - 9), STEP_MARGIN + 3, y + 3, "top")
	end


	paintScrollbar(gc, w, listTop, visible * rowHeight, #PHYSICS_FIXTURES, visible,
	               physicsBrowser.scroll)

	local selected = PHYSICS_FIXTURES[focus]
	local footer = physicsBrowser.status or (selected and selected.problem) or ""
	gc:setColorRGB(90, 90, 90)
	gc:drawLine(STEP_MARGIN, h - footerHeight, w - STEP_MARGIN, h - footerHeight)
	-- The explanation is the reason a beginner can tell these apart, so it gets the smaller face
	-- and the tighter line to fit whole rather than the list losing a row to it. If it still runs
	-- out, the last line says so rather than stopping mid sentence.
	gc:setFont("sansserif", "r", 7)
	local y = h - footerHeight + 3
	local lines = wrapText(gc, footer, w - 2 * STEP_MARGIN)
	local lineHeight = mathmax(10, gc:getStringHeight("H"))
	local count = mathmax(0, math.floor((h - 16 - y) / lineHeight))
	for i = 1, mathmin(count, #lines) do
		local line = lines[i]
		if i == count and i < #lines then
			local cue = "  T text"
			line = fitHeaderText(gc, line, w - 2 * STEP_MARGIN - gc:getStringWidth(cue)) .. cue
		end
		gc:drawString(line, STEP_MARGIN, y, "top")
		y = y + lineHeight
	end
	gc:setColorRGB(60, 60, 140)
	gc:drawString(fitHeaderText(gc, "enter solve  T text  esc shell", w - 2 * STEP_MARGIN),
	              STEP_MARGIN, h - 14, "top")
end

local function followFocus(height, rowHeight)
	local rows = steps.rows or {}
	local first, last = nil, nil
	for i, row in ipairs(rows) do
		if row.step == steps.focus then
			first = first or i
			last = i
		end
	end
	if not first or height < STEP_LINE then return first, last end
	if first - 1 < steps.scroll then steps.scroll = first - 1 end
	local used = 0
	for i = steps.scroll + 1, last do
		used = used + rowHeight(i)
		if used > height then
			steps.scroll = first - 1
			break
		end
	end
	return first, last
end

-- Draws a header value beside its label, wrapping under itself rather than off the right edge.
-- Answers with the y the next line starts at.
local function wrapUnderLabel(gc, text, x, y, w, bottom, slot)
	local lines = wrapSummary(gc, slot, text, w - x - STEP_MARGIN)
	if #lines == 0 then return y + STEP_LINE end
	local count = mathmax(1, math.floor((bottom - y) / STEP_LINE))
	for i = 1, mathmin(count, #lines) do
		local line = lines[i]
		if i == count and i < #lines then
			local suffix = "  TAB result"
			line = fitHeaderText(gc, line, w - x - STEP_MARGIN - gc:getStringWidth(suffix)) .. suffix
			steps.resultOverflow = true
		end
		gc:drawString(line, x, y, "top")
		y = y + STEP_LINE
	end
	return y
end

local statusIcons = {}
local function paintStatusIcon(gc, icon, x, y, w, h)
	if not hasStepSurface then return false end
	if type(nps_nspire.ui_icon_image) == "function" and x >= 0 and y >= 0 and x + 16 <= w and y + 16 <= h then
		if statusIcons[icon] == nil then
			local ok, encoded = pcall(nps_nspire.ui_icon_image, icon)
			local decoded, retained = false, nil
			if ok and type(encoded) == "string" then decoded, retained = pcall(image.new, encoded) end
			statusIcons[icon] = decoded and retained or false
		end
		if statusIcons[icon] then
			gc:drawImage(statusIcons[icon], x, y)
			return true
		end
	end
	return type(nps_nspire.ui_icon) == "function" and nps_nspire.ui_icon(gc, icon, x, y, w, h)
end

local function paintStepsHeader(gc, w)
	local r = steps.result
	steps.resultOverflow = false
	local wait = r.render_ready_ms and string.format("%dr", r.render_ready_ms)
	             or string.format("%dms", r.total_ms or -1)
	gc:setFont("sansserif", "r", 7)
	local metrics = fitHeaderText(gc, wait, mathmax(0, w - 72))
	local metricsX = w - gc:getStringWidth(metrics) - STEP_MARGIN
	local heading = steps.view == "result" and "RESULT  " or
	                (steps.walkthrough == "hint" and "HINT  " or "STEPS  ")
	local title = heading .. stepModeLabel(r.mode) .. "  " .. steps.variable .. "  " ..
	              string.upper(STEP_DETAILS[steps.detail])
	gc:setFont("sansserif", "b", 9)
	title = fitHeaderText(gc, title, metricsX - 2 * STEP_MARGIN)
	gc:setColorRGB(37, 57, 87)
	gc:fillRect(0, 0, w, STEP_LINE)
	gc:setFont("sansserif", "b", 9)
	gc:setColorRGB(255, 255, 255)
	gc:drawString(title, STEP_MARGIN, 1, "top")
	gc:setFont("sansserif", "r", 7)
	gc:setColorRGB(210, 224, 240)
	gc:drawString(metrics, metricsX, 2, "top")
	local y = STEP_LINE
	if steps.view == "result" then
		steps.headerHeight = y
		gc:setFont("sansserif", "r", 9)
		return y
	end
	local bottom = platform.window:height() - 4 * STEP_LINE

	gc:setFont("sansserif", "b", 8)
	gc:setColorRGB(90, 90, 90)
	gc:drawString("INPUT", STEP_MARGIN, y, "top")
	gc:setFont("sansserif", "r", 9)
	local inputX = STEP_MARGIN + 42
	local inputLines = wrapSummary(gc, "input", r.input or "", w - inputX - STEP_MARGIN)
	local inputRemaining = finalResultVisible(r) and (2 + (r.assumptions and 1 or 0) + (r.interpretation and 2 or 0)) or 0
	local inputH = mathmax(STEP_LINE, mathmin(4 * STEP_LINE, bottom - y - inputRemaining * STEP_LINE))
	local inputW, pairedW = w - inputX - STEP_MARGIN, mathmin(96, math.floor(w / 3))
	local answer = finalResultVisible(r) and answerText(r)
	local pairedInput, pairedAnswer
	if steps.view == "list" and answer and inputW - pairedW - 8 >= STEP_LINE then
		pairedInput = fitMath("pairedInput", r.input or "", inputW - pairedW - 8, inputH - 2)
		if pairedInput then
			pairedAnswer = fitMath("pairedAnswer", answer, pairedW, inputH - STEP_LINE)
		end
	end
	local inputUsed
	if not pairedAnswer then inputUsed = placeMath("input", r.input or "", inputX, y, inputW, inputH - 2) end
	if pairedAnswer then
		showMath("pairedInput", pairedInput, inputX, y, inputW - pairedW - 8)
		local answerX = w - STEP_MARGIN - pairedW
		gc:setFont("sansserif", "b", 8)
		gc:setColorRGB(0, 55, 120)
		gc:drawString("ANSWER", answerX, y, "top")
		showMath("pairedAnswer", pairedAnswer, answerX, y + STEP_LINE, pairedW)
		y = y + mathmax(pairedInput.h + 2, pairedAnswer.h + STEP_LINE)
	elseif inputUsed then
		y = y + mathmax(STEP_LINE, inputUsed + 2)
	else
		for i = 1, mathmin(2, #inputLines) do
			local line = inputLines[i]
			if i == 2 and #inputLines > 2 then
				local suffix = finalResultVisible(r) and "  TAB result" or "..."
				line = fitHeaderText(gc, line, w - inputX - STEP_MARGIN - gc:getStringWidth(suffix)) .. suffix
				steps.resultOverflow = true
			end
			gc:drawString(fitHeaderText(gc, line, w - inputX - STEP_MARGIN), inputX, y, "top")
			y = y + STEP_LINE
		end
		if #inputLines == 0 then y = y + STEP_LINE end
	end

	if answer and not pairedAnswer then
		gc:setFont("sansserif", "b", 8)
		gc:setColorRGB(0, 55, 120)
		gc:drawString("ANSWER", STEP_MARGIN, y, "top")
		-- Reserve summary space for trust and conditions before sizing the answer.
		local answerX = STEP_MARGIN + 48
		local answerW = w - answerX - STEP_MARGIN
		local remaining = 1 + (r.assumptions and 1 or 0) + (r.interpretation and 2 or 0)
		local answerH = mathmax(STEP_LINE, mathmin(math.floor(platform.window:height() / 3),
		                                       bottom - y - remaining * STEP_LINE))
		local used = placeMath("answer", answer, answerX, y, answerW, answerH)
		if used then
			y = y + mathmax(used, STEP_LINE)
		else
			gc:setFont("sansserif", "b", 10)
			local lines = wrapSummary(gc, "answer", answer, answerW)
			local count = mathmax(1, math.floor(answerH / STEP_LINE))
			for i = 1, mathmin(count, #lines) do
				local line = lines[i]
				if i == count and i < #lines then
					line = fitHeaderText(gc, string.format("(%d more lines: TAB result)", #lines - i + 1), answerW)
					steps.resultOverflow = true
				end
				gc:drawString(line, answerX, y, "top")
				y = y + STEP_LINE
			end
		end
	end

	if finalResultVisible(r) then
		local trustX = STEP_MARGIN + 42
		if paintStatusIcon(gc, r.agrees == false and 1 or r.agrees == true and 0 or 2,
		                   trustX, y, w, platform.window:height()) then
			trustX = trustX + 18
		end
		gc:setFont("sansserif", "b", 8)
		if r.agrees == false then
			gc:setColorRGB(180, 0, 0)
		elseif r.agrees == true then
			gc:setColorRGB(0, 100, 65)
		else
			gc:setColorRGB(90, 90, 90)
		end
		gc:drawString("TRUST", STEP_MARGIN, y, "top")
		gc:setFont("sansserif", "r", 8)
		local remaining = (r.assumptions and 1 or 0) + (r.interpretation and 2 or 0)
		y = wrapUnderLabel(gc, resultNote(r), trustX, y, w, bottom - remaining * STEP_LINE, "trust")

		if r.assumptions then
			gc:setFont("sansserif", "b", 8)
			gc:setColorRGB(140, 80, 0)
			gc:drawString("ASSUMES", STEP_MARGIN, y, "top")
			gc:setFont("sansserif", "r", 8)
			y = wrapUnderLabel(gc, r.assumptions, STEP_MARGIN + 54, y, w,
			                   bottom - (r.interpretation and 2 * STEP_LINE or 0), "assumptions")
		end

		if r.interpretation then
			gc:setColorRGB(140, 80, 0)
			gc:setFont("sansserif", "b", 8)
			gc:drawString("MEANING", STEP_MARGIN, y, "top")
			gc:setFont("sansserif", "r", 8)
			y = wrapUnderLabel(gc, r.interpretation, STEP_MARGIN + 54, y, w, mathmin(bottom, y + 2 * STEP_LINE), "meaning")
		end
	end

	gc:setColorRGB(200, 200, 200)
	-- The divider sits inside the last header line rather than below it. Two pixels of gap here is
	-- a whole row of the list at this line height, and the row it cost used to be drawn over the
	-- footer instead of dropped.
	gc:drawLine(STEP_MARGIN, y - 1, w - STEP_MARGIN, y - 1)
	gc:setFont("sansserif", "r", 9)
	-- The answer's height is whatever the OS asked for, so the list below is told what the header
	-- actually took rather than counting the lines it was expected to take.
	steps.headerHeight = y
	return y
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

local function paintStepsList(gc, w, h, y)
	local rows = steps.rows or {}
	local top = y
	local bottom = h - STEP_LINE - (steps.result.steps_truncated and STEP_LINE or 0)
	local height = mathmax(0, bottom - top)
	local capacity = mathmax(1, math.floor(height / STEP_LINE) + 2)
	local prepared = {}
	local function layout(index)
		if prepared[index] then return prepared[index] end
		local row = rows[index]
		local maxX = w - STEP_MARGIN - gc:getStringWidth("...")
		local x = mathmin(STEP_MARGIN + row.depth * 10, mathmin(maxX, math.floor(w / 3)))
		local slot = "list" .. ((index - 1) % capacity + 1)
		local prefix = 0
		if row.kind == "math" then
			for previous = index - 1, 1, -1 do
				if rows[previous].step ~= row.step or rows[previous].kind == "math" then break end
				prefix = prefix + STEP_LINE
			end
		end
		local box = row.kind == "math" and height - prefix >= STEP_LINE and
			fitMath(slot, row.text, w - x - STEP_MARGIN, mathmin(4 * STEP_LINE, height - prefix) - 4) or nil
		local item = { x = x, slot = slot, box = box, height = box and mathmax(STEP_LINE, box.h + 4) or STEP_LINE }
		prepared[index] = item
		return item
	end
	local _, last = followFocus(height, function(index) return layout(index).height end)
	prepared = {}
	steps.bands = {}
	steps.listOverflow = false
	local shown = steps.scroll
	for index = steps.scroll + 1, #rows do
		local row, item = rows[index], layout(index)
		if y + item.height > bottom then break end
		steps.bands[#steps.bands + 1] = { top = y, bottom = y + item.height, step = row.step }
		if row.step == steps.focus then
			gc:setColorRGB(220, 230, 250)
			gc:fillRect(STEP_MARGIN, y, w - 2 * STEP_MARGIN, item.height)
		end
		rowColor(gc, row)
		local mark = (row.kind == "text" and not row.verified) and "  (unverified)" or ""
		local branch = row.branch == "closed" and "[+] " or (row.branch == "open" and "[-] " or "")
		local x = item.x
		if row.kind == "text" then
			gc:setFont("sansserif", "b", 7)
			gc:drawString(string.upper(row.phase or "Work"), x, y + 1, "top")
			x = mathmin(x + gc:getStringWidth("CHECK") + 8, w - STEP_MARGIN - gc:getStringWidth("..."))
			gc:setFont("sansserif", "r", 9)
			rowColor(gc, row)
		end
		if item.box then
			showMath(item.slot, item.box, x, y + 2, w - x - STEP_MARGIN)
		else
			gc:drawString(fitHeaderText(gc, displayExpression(branch .. row.text .. mark), w - x - STEP_MARGIN), x, y, "top")
			if row.step == steps.focus and row.kind == "math" then steps.listOverflow = true end
		end
		y = y + item.height
		shown = index
	end
	steps.listOverflow = steps.listOverflow or (last ~= nil and last > shown)
	paintScrollbar(gc, w, top, height, #rows, #steps.bands, steps.scroll)
	if steps.result.steps_truncated then
		gc:setColorRGB(180, 0, 0)
		gc:drawString(fitHeaderText(gc, "step list cut short: the record was too deep to hand over",
		                            w - 2 * STEP_MARGIN), STEP_MARGIN, y, "top")
	end
end

local function stepInfo(r, index, detailed)
	local all = r.steps or {}
	local s, pending = projectedStep(r, index)
	if not s then return {} end
	local out = {}
	local function add(text, color)
		out[#out + 1] = { text = text, color = color }
	end
	local function addMath(slot, label, expr, color)
		if expr and expr ~= "" then
			out[#out + 1] = { slot = slot, label = label, math = expr, color = color }
		end
	end
	local black, grey, blue, red, brown = {0, 0, 0}, {90, 90, 90}, {0, 0, 140}, {180, 0, 0}, {140, 80, 0}
	local trust = s.failed and "  FAILED ITS CHECK" or (s.verified and "" or "  (unverified)")
	add(string.format("Step %d of %d: %s%s", index, #all, s.name, trust), s.failed and red or black)
	add("Phase: " .. stepPhaseLabel(s), blue)
	if s.kind == "plan" then
		add("Strategy: " .. s.goal, black)
	elseif s.kind == "check" then
		if s.action and s.action ~= "" then add("Check: " .. s.action, black) end
		addMath("from", "Expected:", s.before, blue)
		addMath("to", "Observed:", s.after, s.failed and red or blue)
	elseif s.kind == "branch" then
		addMath("from", "Condition:", s.case, brown)
	else
		local verified = s.kind == "transformation" and s.verified == true and not s.failed
		if s.action and s.action ~= "" then add((verified and "Do: " or "Attempted: ") .. s.action, black) end
		addMath("to", verified and "Write:" or "Unverified:", s.after, verified and blue or red)
	end
	if s.short and s.short ~= "" then add("Why: " .. s.short, grey) end
	if s.kind ~= "plan" and s.kind ~= "check" and s.kind ~= "branch" then
		addMath("from", "Starting from:", s.before, blue)
	end
	if s.kind ~= "plan" then add("Goal: " .. s.goal, black) end
	if pending then add("Reveal the remaining work to see this step's result.", grey) end
	if s.resolution == "solved" then
		add("This case gives an answer" ..
		    (s.settled_by and s.settled_by ~= "" and ", checked by " .. s.settled_by or ""), black)
	elseif s.resolution == "rejected" then
		add("This case is ruled out" ..
		    (s.settled_by and s.settled_by ~= "" and ": " .. s.settled_by or ""), brown)
	elseif s.resolution then
		add("This case is not settled yet", brown)
	end
	if s.domain and s.domain ~= "" then add("Requires: " .. s.domain, brown) end
	if s.assumes and s.assumes ~= "" then add("Assumes: " .. s.assumes, brown) end
	if detailed then
		if s.detail and s.detail ~= "" then add(s.detail, black) end
		if s.checks and s.checks ~= "" then add("Checked by: " .. s.checks, grey) end
		if s.rule then add("Rule " .. s.rule .. ", claim: " .. tostring(s.claim), grey) end
	end
	if r.normalized_expression then
		add("Normalized input: " .. r.normalized_expression, blue)
	end
	return out
end

local function resultLines()
	local r = steps.result
	local out = {}
	local function add(text, color)
		out[#out + 1] = { text = text, color = color }
	end
	add("Input: " .. (r.input or ""), {90, 90, 90})
	local answer = answerText(r)
	if answer then
		out[#out + 1] = { slot = "answer", label = r.answer_only and "CAS answer:" or "Answer:",
		                  math = answer, color = {0, 0, 140} }
	end
	add(resultNote(r), r.agrees == false and {180, 0, 0} or {90, 90, 90})
	if r.assumptions then add("Assumes: " .. r.assumptions, {140, 80, 0}) end
	if r.interpretation then add("Meaning: " .. r.interpretation, {140, 80, 0}) end
	return out
end

local function sameDetailSource(left, right)
	if #left ~= #right then return false end
	for index, item in ipairs(left) do
		local other = right[index]
		if item.text ~= other.text or item.math ~= other.math or item.label ~= other.label
			or item.slot ~= other.slot then return false end
		for component = 1, 3 do
			if item.color[component] ~= other.color[component] then return false end
		end
	end
	return true
end

-- Expand oversized maths before pagination so scrolling can reach every line.
local function paintDetail(gc, w, h, y, source, scrollState, plainText)
	scrollState = scrollState or steps
	local bottom = h - STEP_LINE
	local items = source
	if not plainText then
		local fontHeight = gc:getStringHeight("H")
		local layout = scrollState.detailLayout
		if layout and layout.width == w and layout.height == h and layout.top == y
			and layout.fontHeight == fontHeight and layout.revision == mathLayoutRevision
			and sameDetailSource(source, layout.source) then
			items = layout.items
		else
		items = {}
		for _, item in ipairs(source) do
		if item.math then
			gc:setFont("sansserif", "b", 8)
			item.x = STEP_MARGIN + gc:getStringWidth(item.label) + 4
			item.width = w - item.x - STEP_MARGIN
			item.box = fitMath(item.slot, item.math, item.width, bottom - y - 6, DETAIL_MATH_FONTS)
			if item.box then
				item.height = mathmax(item.box.h, STEP_LINE) + 6
				items[#items + 1] = item
			else
				gc:setFont("sansserif", "r", 9)
				for _, ln in ipairs(wrapText(gc, item.label .. " " .. item.math, w - 2 * STEP_MARGIN)) do
					items[#items + 1] = { text = ln, color = item.color }
				end
			end
			else
				gc:setFont("sansserif", "r", 9)
				for _, line in ipairs(wrapText(gc, item.text, w - 2 * STEP_MARGIN)) do
					items[#items + 1] = { text = line, color = item.color }
				end
			end
		end
		local bytes = 0
		for _, item in ipairs(items) do bytes = bytes + #(item.text or item.math or "") end
		for _, item in ipairs(source) do
			bytes = bytes + #(item.text or item.math or "") + #(item.label or "")
		end
		scrollState.detailLayout = #items <= 512 and bytes <= 32768 and {
			width = w, height = h, top = y, fontHeight = fontHeight,
			revision = mathLayoutRevision, source = source, items = items,
		} or nil
		end
	end
	gc:setFont("sansserif", "r", 9)
	scrollState.stepScroll = mathmax(0, mathmin(scrollState.stepScroll or 0, mathmax(0, #items - 1)))
	local shown = 0
	local index = scrollState.stepScroll + 1
	while index <= #items do
		local item = items[index]
		local used = item.height or STEP_LINE
		if y + used > bottom then break end
		if item.box then
			gc:setFont("sansserif", "b", 8)
			gc:setColorRGB(item.color[1], item.color[2], item.color[3])
			showMath(item.slot, item.box, item.x, y + 3, item.width)
			local labelY = y + 3 + mathmax(0, math.floor((item.box.h - gc:getStringHeight(item.label)) / 2))
			gc:drawString(item.label, STEP_MARGIN, labelY, "top")
			gc:setFont("sansserif", "r", 9)
		else
			gc:setColorRGB(item.color[1], item.color[2], item.color[3])
			gc:drawString(item.text, STEP_MARGIN, y, "top")
		end
		y = y + used
		shown = shown + 1
		index = index + 1
	end
	if shown < #items then
		return string.format("%d-%d/%d", scrollState.stepScroll + 1, scrollState.stepScroll + shown, #items)
	end
	return nil
end

function readFullText()
	if templatePicker.active then templatePicker.close() end
	if textReader.active then return end
	local paragraphs = {}
	local function add(label, value)
		if value ~= nil and tostring(value) ~= "" then
			paragraphs[#paragraphs + 1] = label .. tostring(value)
		end
	end
	if steps.active and steps.result then
		local r = steps.result
		add("Input: ", r.input)
		add("Variable: ", steps.variable)
		add("Operation: ", stepModeLabel(r.mode))
		if finalResultVisible(r) then
			add("Answer: ", answerText(r))
			add("Trust: ", resultNote(r))
			add("Assumes: ", r.assumptions)
			add("Meaning: ", r.interpretation)
		end
		for _, item in ipairs(stepInfo(r, steps.focus, true)) do
			if item.math then add(item.label .. " ", item.math) else add("", item.text) end
		end
		add("Render ready ms: ", r.render_ready_ms)
		add("Solver ms: ", r.total_ms)
		add("Arena nodes: ", r.nodes)
		add("Steps: ", r.step_count)
		add("Backend calls: ", r.giac_calls)
		add("Lua heap KiB: ", r.heap_kb)
	elseif physicsBrowser.active then
		local selected = PHYSICS_FIXTURES[physicsBrowser.focus]
		if selected then add("", selected.label) add("", selected.problem) end
		add("Status: ", physicsBrowser.status)
	else
		local focused = theView and theView:getFocus()
		local selected
		for index, editor in ipairs(histME1) do
			if editor == focused or histME2[index] == focused then selected = steps.histText[index] end
		end
		if selected then
			add("Input: ", selected[1])
			add("Result: ", selected[2])
		elseif fctEditor then
			add("Input: ", fctEditor:getExpression())
		end
		add("Status: ", steps.status)
		add("Variable: ", steps.variable)
		add("", stepRefusal())
		if stepRefusal() then add("", stepSurfaceRemedy) end
	end
	add("Last error: ", failureDetails)
	add("", deviceIdentityLine())
	add("", giacLabel())
	if not versionShaped(giacRuntimeVersion()) or backendVersionRefusal() then
		add("Version response: ", giacVersionDetails)
	end
	if stepManifest then
		add("Module: ", stepManifest.id)
		local backend = stepManifest.symbolic_backend
		if backend then add("Backend: ", tostring(backend.name) .. " " .. tostring(backend.version)) end
	end
	textReader.paragraphs = paragraphs
	textReader.layout = nil
	textReader.stepScroll = 0
	textReader.active = true
	textReader.focus = theView and theView:getFocus()
	updateOverlayEditors()
end

local function closeTextReader()
	textReader.active = false
	textReader.paragraphs = {}
	textReader.layout = nil
	local focused = textReader.focus
	textReader.focus = nil
	updateOverlayEditors(focused)
end

local function paintTextReader(gc)
	local w, h = platform.window:width(), platform.window:height()
	gc:setColorRGB(255, 255, 255)
	gc:fillRect(0, 0, w, h)
	gc:setFont("sansserif", "r", 9)
	local layout = textReader.layout
	local fontHeight = gc:getStringHeight("Ag")
	if not layout or layout.width ~= w or layout.fontHeight ~= fontHeight then
		layout = { width = w, fontHeight = fontHeight, lines = {} }
		for _, paragraph in ipairs(textReader.paragraphs) do
			for _, line in ipairs(wrapText(gc, paragraph, w - 2 * STEP_MARGIN)) do
				layout.lines[#layout.lines + 1] = {text = line, color = {0, 0, 0}}
			end
		end
		textReader.layout = layout
	end
	local count = paintDetail(gc, w, h, STEP_LINE, layout.lines, textReader, true)
	gc:setColorRGB(37, 57, 87)
	gc:drawString(fitHeaderText(gc, "Full text", w - 2 * STEP_MARGIN), STEP_MARGIN, 1, "top")
	local footer = "UP/DOWN scroll  ESC back"
	if count and gc:getStringWidth(footer .. "  " .. count) <= w - 2 * STEP_MARGIN then
		footer = footer .. "  " .. count
	end
	gc:drawString(fitHeaderText(gc, footer, w - 2 * STEP_MARGIN), STEP_MARGIN, h - STEP_LINE, "top")
end

local function paintStepsFooter(gc, w, h, detail)
	local y = h - STEP_LINE
	gc:setColorRGB(242, 244, 247)
	gc:fillRect(0, y, w, STEP_LINE)
	gc:setColorRGB(190, 196, 205)
	gc:drawLine(0, y, w, y)
	gc:setFont("sansserif", "r", 7)
	gc:setColorRGB(55, 65, 78)
	local hint
	local count
	-- Whether a hint remains is not what the mode records, so every cue takes the one visibility
	-- decision rather than recomputing a two-valued answer to a three-valued question.
	local hinting = steps.walkthrough == "hint" and canonicalStepCount(steps.result) > 0
	local hintCue = hinting and (finalResultVisible(steps.result) and "ANSWER shown" or "TAB next hint")
	if steps.view == "result" then
		hint = "UP/DOWN scroll  ESC list"
		count = "result"
	elseif hinting then
		count = string.format("hint %d/%d", exposedStepCount(steps.result), canonicalStepCount(steps.result))
		hint = steps.view == "step" and hintCue .. "  U/D scroll  ESC list"
		                               or hintCue .. "  ENTER detail  ESC close"
	else
		hint = steps.view == "step" and "UP/DOWN scroll  L/R step  ESC list"
		                               or "UP/DOWN select  L/R fold  ENTER details"
		-- A record with no steps has no position to count, and only answer_only says an answer arrived.
		count = answerWithoutSteps(steps.result) and "answer only"
		        or canonicalStepCount(steps.result) == 0 and "no steps"
		        or string.format("%d/%d", steps.focus, canonicalStepCount(steps.result))
	end
	if steps.view == "list" and steps.listOverflow then
		hint = hinting and ("ENTER details  " .. hintCue) or "ENTER all work  UP/DOWN select"
	end
	if detail then count = count .. "  " .. detail end
	local readerHint = "  T text"
	hint = fitHeaderText(gc, hint, w - gc:getStringWidth(count .. readerHint) - 3 * STEP_MARGIN) .. readerHint
	gc:drawString(hint, STEP_MARGIN, y + 2, "top")
	gc:drawString(count, w - gc:getStringWidth(count) - STEP_MARGIN, y + 2, "top")
end

local function paintSteps(gc)
	local w = platform.window:width()
	local h = platform.window:height()
	gc:setColorRGB(255, 255, 255)
	gc:fillRect(0, 0, w, h)
	gc:setFont("sansserif", "r", 9)
	local y = paintStepsHeader(gc, w)
	local detail = nil
	if steps.view == "step" then
		detail = paintDetail(gc, w, h, y,
			stepInfo(steps.result, steps.focus, STEP_DETAILS[steps.detail] == "beginner"))
	elseif steps.view == "result" then
		detail = paintDetail(gc, w, h, y, resultLines())
	else
		paintStepsList(gc, w, h, y)
	end
	paintStepsFooter(gc, w, h, detail)
	parkUnusedMath()
end

local function moveFocus(delta)
	if answerWithoutSteps(steps.result) then return end
	local visible = steps.visibleSteps or {}
	if #visible == 0 then return end
	local position = 1
	for i, step in ipairs(visible) do
		if step == steps.focus then
			position = i
			break
		end
	end
	position = mathmax(1, mathmin(#visible, position + delta))
	steps.focus = visible[position]
	steps.stepScroll = 0
end

local function foldBranch(direction)
	local all = steps.result.steps or {}
	if not hasStepChildren(all, steps.focus, exposedStepCount(steps.result)) then
		moveFocus(direction == "right" and 1 or -1)
		return
	end
	if direction == "left" and not steps.collapsed[steps.focus] then
		steps.collapsed[steps.focus] = true
	elseif direction == "right" and steps.collapsed[steps.focus] then
		steps.collapsed[steps.focus] = nil
	else
		moveFocus(direction == "right" and 1 or -1)
		return
	end
	steps.rows = buildRows(steps.result)
	steps.stepScroll = 0
end

local function revealNextStep()
	local r = steps.result
	if not r or steps.walkthrough ~= "hint" or answerWithoutSteps(r) then return end
	local count = canonicalStepCount(r)
	local revealed = exposedStepCount(r)
	if revealed >= count then return end
	local nextStep = revealed + 1
	local depth = r.steps[nextStep].depth
	for i = nextStep - 1, 1, -1 do
		if r.steps[i].depth < depth then
			steps.collapsed[i] = nil
			depth = r.steps[i].depth
		end
	end
	steps.revealed = nextStep
	steps.rows = buildRows(r)
	steps.focus = nextStep
	steps.stepScroll = 0
	steps.status = walkthroughStatus(r)
	steps.recordStatus = steps.status
	steps.statusResult = r
end

-- Detail views scroll independently of the selected native step.
local function moveDown(delta)
	if steps.view ~= "list" then
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
			if physicsBrowser.active then closePhysicsFixtures() else closeSteps() end
		end
		platform.window:invalidate()
	end
end

-- The shell's handlers, kept so the viewer can hand every key back to them when it is closed.
local baseOn = {
	paint = on.paint, enterKey = on.enterKey, escapeKey = on.escapeKey, tabKey = on.tabKey,
	backtabKey = on.backtabKey, arrowUp = on.arrowUp, arrowDown = on.arrowDown,
	arrowLeft = on.arrowLeft, arrowRight = on.arrowRight, charIn = on.charIn,
	mouseDown = on.mouseDown, mouseUp = on.mouseUp, mouseMove = on.mouseMove,
}

-- Which row a tap landed on, from the bands the last paint recorded.
local function bandAt(bands, y)
	for _, band in ipairs(bands or {}) do
		if y >= band.top and y < band.bottom then return band end
	end
	return nil
end

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

local function paintOverlay(gc, painter, label)
	local ok, err = pcall(painter, gc)
	if not ok then
		-- A math box the next fillRect does not cover would sit over the message saying it failed.
		parkMathBoxes()
		gc:setFont("sansserif", "r", 7)
		gc:setColorRGB(180, 0, 0)
		failureDetails = label .. " paint failed: " .. tostring(err)
		gc:drawString(fitHeaderText(gc, failureDetails, platform.window:width() - 4), 2, 2, "top")
	else
		finishResourceProfile()
	end
end

function on.paint(gc)
	if templatePicker.active then return templatePicker.paint(gc) end
	if textReader.active then return paintOverlay(gc, paintTextReader, "full text") end
	if physicsBrowser.active then return paintOverlay(gc, paintPhysicsFixtures, "physics browser") end
	if steps.active then return paintOverlay(gc, paintSteps, "steps") end
	baseOn.paint(gc)
	replayHistory()
	finishResourceProfile()
	if steps.status then
		gc:setFont("sansserif", "r", 9)
		local status = steps.status
		if gc:getStringWidth(status) > scrWidth - 16 then
			local cue = "  HELP text"
			status = fitHeaderText(gc, status, scrWidth - 16 - gc:getStringWidth(cue)) .. cue
		end
		gc:setColorRGB(60, 60, 140)
		gc:drawString(status, scrWidth - gc:getStringWidth(status) - 8, 0, "top")
	end
end

-- Enter opens the focused step and esc closes it again, so the two keys walk in and out of the
-- record; with the viewer closed every key is the shell's.
on.enterKey = guarded(function()
	if templatePicker.active then return templatePicker.choose() end
	if textReader.active then return closeTextReader() end
	if physicsBrowser.active then return runPhysicsFixture() end
	if not steps.active then return baseOn.enterKey() end
	if steps.view == "list" then
		if canonicalStepCount(steps.result) > 0 then
			steps.view = "step"
			steps.stepScroll = 0
		end
	else
		steps.view = "list"
	end
end)
on.returnKey = on.enterKey

on.escapeKey = guarded(function()
	if templatePicker.active then return templatePicker.close() end
	if textReader.active then return closeTextReader() end
	if physicsBrowser.active then return closePhysicsFixtures() end
	if not steps.active then return baseOn.escapeKey() end
	if steps.view ~= "list" then
		steps.view = "list"
	else
		closeSteps()
	end
end)

-- Measured on the emulator: an arrow reaches the four named handlers and never on.arrowKey, so one
-- press is one call and the viewer has no double dispatch to defend against.
local function viewerArrow(key)
	if physicsBrowser.active then
		if key == "down" then
			physicsBrowser.focus = mathmin(#PHYSICS_FIXTURES, physicsBrowser.focus + 1)
		elseif key == "up" then
			physicsBrowser.focus = mathmax(1, physicsBrowser.focus - 1)
		end
		return
	end
	if key == "down" then moveDown(1)
	elseif key == "up" then moveDown(-1)
	elseif key == "right" then
		if steps.view == "list" then foldBranch("right") elseif steps.view == "step" then moveFocus(1) end
	elseif key == "left" then
		if steps.view == "list" then foldBranch("left") elseif steps.view == "step" then moveFocus(-1) end
	end
end

on.arrowDown = guarded(function()
	if templatePicker.active then return templatePicker.move(1) end
	if textReader.active then textReader.stepScroll = textReader.stepScroll + 1 return end
	if not steps.active and not physicsBrowser.active then return baseOn.arrowDown() end
	viewerArrow("down")
end)
on.arrowUp = guarded(function()
	if templatePicker.active then return templatePicker.move(-1) end
	if textReader.active then textReader.stepScroll = mathmax(0, textReader.stepScroll - 1) return end
	if not steps.active and not physicsBrowser.active then return baseOn.arrowUp() end
	viewerArrow("up")
end)
on.arrowRight = guarded(function()
	if templatePicker.active then return templatePicker.scroll(-48) end
	if textReader.active then return end
	if not steps.active and not physicsBrowser.active then return baseOn.arrowRight() end
	viewerArrow("right")
end)
on.arrowLeft = guarded(function()
	if templatePicker.active then return templatePicker.scroll(48) end
	if textReader.active then return end
	if not steps.active and not physicsBrowser.active then return baseOn.arrowLeft() end
	viewerArrow("left")
end)

on.tabKey = guarded(function()
	if templatePicker.active then return templatePicker.move(1) end
	if textReader.active then textReader.stepScroll = textReader.stepScroll + 8 return end
	if not steps.active and not physicsBrowser.active then return baseOn.tabKey() end
	if not steps.active then return end
	if steps.view == "result" then
		steps.view = "list"
	elseif finalResultVisible(steps.result) and steps.resultOverflow then
		steps.view = "result"
		steps.stepScroll = 0
	else
		revealNextStep()
	end
end)
on.backtabKey = guarded(function()
	if templatePicker.active then return templatePicker.move(-1) end
	if textReader.active then textReader.stepScroll = mathmax(0, textReader.stepScroll - 8) return end
	if not steps.active and not physicsBrowser.active then return baseOn.backtabKey() end
	if physicsBrowser.active then return end
	stepsToggleDetail()
end)
on.help = guarded(function()
	if templatePicker.active then return end
	if textReader.active then return end
	if steps.active then stepsToggleDetail() else readFullText() end
end)

-- Focus opens on item 1, so the entry a student can undo comes first and delete cannot be reached by
-- a reflex enter. The menu consumes escape itself, so on.escapeKey never sees the dismissal.
on.contextMenu = guarded(function()
	if templatePicker.active then return end
	if textReader.active then return end
	if not inited or steps.active or physicsBrowser.active then return end
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
	if templatePicker.active then return end
	if textReader.active then return end
	if (steps.active or physicsBrowser.active) and (ch == "t" or ch == "T") then return readFullText() end
	if not steps.active and not physicsBrowser.active then return baseOn.charIn(ch) end
end)

-- UI-001. The viewer had no touchpad path at all: every tap went on reaching the shell's widgets,
-- which openSteps has just parked at -10000, so the calculator's own pointer did nothing here.
--
-- The gesture is the one the native lists use. A tap moves the selection, and a tap on what is
-- already selected is the confirm, which is what enter would have done. That way a tap can never
-- run a solve the student did not aim at, since the first tap only ever selects. In an open step
-- there is nothing to select, so a tap is the way back to the list, the same as escape.
on.mouseUp = guarded(function(x, y)
	if templatePicker.active then return end
	if textReader.active then
		if y < platform.window:height() / 2 then
			textReader.stepScroll = mathmax(0, textReader.stepScroll - 4)
		else textReader.stepScroll = textReader.stepScroll + 4 end
		return
	end
	if physicsBrowser.active then
		local band = bandAt(physicsBrowser.bands, y)
		if not band then return end
		if band.index == physicsBrowser.focus then return runPhysicsFixture() end
		physicsBrowser.focus = band.index
		return
	end
	if not steps.active then return baseOn.mouseUp(x, y) end
	if steps.view ~= "list" then
		steps.view = "list"
		return
	end
	local band = bandAt(steps.bands, y)
	if not band then return end
	if band.step == steps.focus then
		if canonicalStepCount(steps.result) > 0 then
			steps.view = "step"
			steps.stepScroll = 0
		end
		return
	end
	steps.focus = band.step
	steps.stepScroll = 0
end)

on.mouseDown = guarded(function(x, y)
	if templatePicker.active then return end
	if textReader.active then return end
	if not steps.active and not physicsBrowser.active then return baseOn.mouseDown(x, y) end
end)

on.mouseMove = guarded(function(x, y)
	if templatePicker.active then return end
	if textReader.active then return end
	if not steps.active and not physicsBrowser.active then return baseOn.mouseMove(x, y) end
end)

-- The document keeps history, variable, solver mode, detail and progression preferences, and the line
-- typed but not yet entered, which is the work a student notices missing first. The derivation is
-- recomputed on the next request rather than saved, since the core is deterministic and a saved
-- record would be a copy of what it produces. A restored table is data (section 17): every field
-- is type checked and ranged before it is used.
function on.save()
	local expression
	local variable = isStepsVariable(steps.variable) and steps.variable or "x"
	if fctEditor and fctEditor.editor and fctEditor.editor:getExpression() then
		local typed = fctEditor:getExpression()
		if type(typed) == "string" and typed ~= "" then expression = typed end
	end
	return { variable = variable, mode = steps.mode, detail = steps.detail,
	         progression = steps.progression, automatic = steps.automatic,
	         history = steps.histText, expression = expression }
end

function on.restore(saved)
	if type(saved) ~= "table" then return end
	if type(saved.automatic) == "boolean" then steps.automatic = saved.automatic end
	if isStepsVariable(saved.variable) then
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
	if saved.progression == "full" or saved.progression == "hint" then
		steps.progression = saved.progression
	end
	if type(saved.history) == "table" then
		local pending = {}
		for _, pair in ipairs(saved.history) do
			if type(pair) == "table" and type(pair[1]) == "string" and type(pair[2]) == "string" then
				if #pending == HISTORY_MAX_ENTRIES then table.remove(pending, 1) end
				pending[#pending + 1] = { pair[1], pair[2] }
			end
		end
		if #pending > 0 then steps.pendingHistory = pending end
	end
	if type(saved.expression) == "string" and saved.expression ~= "" then
		steps.pendingExpression = saved.expression
	end
	if steps.mode then
		steps.status = "every enter: " .. stepModeLabel(steps.mode) .. " steps in " .. steps.variable
	elseif not steps.automatic then
		steps.status = "plain Giac"
	end
end
