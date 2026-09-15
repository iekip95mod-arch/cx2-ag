-- ti_voc: the vocabulary of PHYS 2410 chapters 1 to 4, with the book's definitions.
--
-- Lookup rather than reading. Typing filters the list as you go, because in a test you already know
-- the word and only need what it means, and stepping through an alphabet you can already recite is
-- wasted time. Definitions follow the Review and Summary of each chapter.
--
-- Notation was measured on a CX II running 6.40.74. drawString carries v₀, t², √, θ, Δ, π, ρ, ·, ×
-- and the relations, and refuses combining diacritics, so a hatted k is written plainly.

platform.apilevel = '2.0'

local L = 14
local BODY = 9

-- ch is the chapter the term is defined in, so a reader can go back to the right page of the book.
-- sym is the symbol and its SI unit, which is the part most often needed and least often given.
local terms = {
	{ name = "Acceleration, average", ch = 2, sym = "a_avg, m/s²", eq = { "a_avg = Δv / Δt" },
		def = {
			"The ratio of a change in velocity Δv to the",
			"time interval Δt in which the change occurs.",
			"",
			"The algebraic sign gives the direction.",
		} },
	{ name = "Acceleration, instantaneous", ch = 2, sym = "a, m/s²",
		eq = { "a = dv/dt = d²x/dt²" },
		def = {
			"The first time derivative of velocity, and",
			"the second time derivative of position.",
			"",
			"On a graph of v against t, the acceleration",
			"at any time is the slope of the curve at the",
			"point representing that time.",
		} },
	{ name = "Acceleration, centripetal", ch = 4, sym = "a, m/s²", eq = { "a = v² / r" },
		def = {
			"The acceleration of a particle in uniform",
			"circular motion. Constant in magnitude and",
			"directed toward the center of the circle.",
			"",
			"Centripetal means center seeking.",
		} },
	{ name = "Associative law", ch = 3,
		def = {
			"Vector addition obeys it: the grouping of",
			"the vectors being added does not change the",
			"sum.",
		} },
	{ name = "Base quantity", ch = 1,
		def = {
			"A physical quantity chosen as fundamental,",
			"such as length, time and mass. Each is",
			"defined by a standard and given a unit.",
			"",
			"All other quantities are defined in terms of",
			"the base quantities and their units.",
		} },
	{ name = "Chain-link conversion", ch = 1,
		def = {
			"Converting units by multiplying the original",
			"data successively by conversion factors",
			"written as unity, manipulating the units like",
			"algebraic quantities until only the wanted",
			"units remain.",
		} },
	{ name = "Commutative law", ch = 3,
		def = {
			"Vector addition obeys it, so the order of",
			"the two vectors does not change the sum.",
			"",
			"The scalar product obeys it too.",
			"The vector product does NOT.",
		} },
	{ name = "Components of a vector", ch = 3, sym = "ax, ay",
		eq = { "ax = a cosθ      ay = a sinθ" },
		def = {
			"Found by dropping perpendicular lines from",
			"the ends of the vector onto the coordinate",
			"axes.",
			"",
			"θ is the angle between the positive x axis",
			"and the direction of the vector. The sign of",
			"a component gives its direction along that",
			"axis.",
		} },
	{ name = "Constant acceleration", ch = 2,
		eq = { "v = v₀ + a t", "Δx = v₀t + a t²/2", "v² = v₀² + 2 a Δx",
		       "Δx = (v₀ + v) t/2", "Δx = v t − a t²/2" },
		def = {
			"The special case described by the five",
			"equations of Table 2-1.",
			"",
			"Those equations are not valid when the",
			"acceleration is not constant.",
		} },
	{ name = "Density", ch = 1, sym = "ρ, kg/m³", eq = { "ρ = m / V" },
		def = {
			"The mass per unit volume of a material.",
		} },
	{ name = "Displacement", ch = 2, sym = "Δx, m", eq = { "Δx = x₂ − x₁" },
		def = {
			"The change in the position of a particle.",
			"",
			"A vector quantity. Positive if the particle",
			"moved in the positive direction of the axis,",
			"negative if it moved the other way.",
		} },
	{ name = "Displacement, in two dimensions", ch = 4, sym = "Δr, m",
		eq = { "Δr = r₂ − r₁" },
		def = {
			"The change in a particle's position vector.",
		} },
	{ name = "Distance", ch = 2, sym = "m",
		def = {
			"The total path length covered, counting",
			"every meter moved, independent of",
			"direction.",
			"",
			"Not the same as displacement. Distance has",
			"no direction and no sign, and it never",
			"decreases. Four different paths between the",
			"same two points share one displacement and",
			"have four different distances.",
			"",
			"Average speed uses distance. Average",
			"velocity uses displacement. That is the",
			"whole difference between them.",
		} },
	{ name = "Dot product", ch = 3,
		def = {
			"See Scalar product. The two names are used",
			"for the same operation.",
		} },
	{ name = "Free-fall acceleration", ch = 2, sym = "g, 9.8 m/s²",
		eq = { "a = −g,  g = 9.8 m/s²" },
		def = {
			"The constant acceleration of an object",
			"rising or falling freely near Earth's",
			"surface.",
			"",
			"The constant acceleration equations describe",
			"the motion with two changes of notation: the",
			"motion is referred to a vertical y axis with",
			"+y up, and a is replaced by −g, where g is",
			"the magnitude of the free-fall acceleration.",
			"",
			"Near Earth's surface g = 9.8 m/s².",
		} },
	{ name = "Instantaneous velocity", ch = 2, sym = "v, m/s", eq = { "v = dx/dt" },
		def = {
			"The limit of the average velocity as the",
			"time interval shrinks to zero.",
			"",
			"May be found as the slope, at that",
			"particular time, of the graph of x against t.",
			"",
			"Speed is the magnitude of instantaneous",
			"velocity.",
		} },
	{ name = "Magnitude", ch = 3, eq = { "a = √(ax² + ay²)" },
		def = {
			"The size of a vector, without its direction.",
			"Never negative.",
		} },
	{ name = "Position", ch = 2, sym = "x, m",
		def = {
			"Locates a particle with respect to the",
			"origin, or zero point, of an axis.",
			"",
			"Positive or negative according to which side",
			"of the origin the particle is on, or zero at",
			"the origin.",
		} },
	{ name = "Position vector", ch = 4, sym = "r, m", eq = { "r = x i + y j + z k" },
		def = {
			"The location of a particle relative to the",
			"origin of a coordinate system.",
			"",
			"Described either by a magnitude and one or",
			"two angles, or by its components.",
		} },
	{ name = "Projectile motion", ch = 4,
		eq = { "x = v₀cosθ₀ t", "y = v₀sinθ₀ t − g t²/2", "ax = 0,  ay = −g" },
		def = {
			"The motion of a particle launched with an",
			"initial velocity, where during the flight",
			"the horizontal acceleration is zero and the",
			"vertical acceleration is the free-fall",
			"acceleration −g, taking upward as positive.",
		} },
	{ name = "Range, horizontal", ch = 4, sym = "R, m", eq = { "R = v₀² sin2θ₀ / g" },
		def = {
			"The horizontal distance from the launch",
			"point to the point at which the particle",
			"returns to the launch height.",
			"",
			"That formula holds only when the landing",
			"height equals the launch height.",
		} },
	{ name = "Reference frame", ch = 4,
		def = {
			"The physical object to which measurements",
			"are attached, and which the observer is",
			"treated as riding along with.",
			"",
			"The velocity of a particle depends on the",
			"reference frame of whoever is measuring it.",
			"A duck flying alongside another sees it as",
			"stationary, while somebody on the ground",
			"sees it flying north.",
			"",
			"Observers on frames moving at constant",
			"velocity relative to each other measure",
			"DIFFERENT velocities and the SAME",
			"acceleration.",
		} },
	{ name = "Relative motion", ch = 4, sym = "v_PA, m/s",
		eq = { "v_PA = v_PB + v_BA", "a_PA = a_PB" },
		def = {
			"Motion measured from a frame that is",
			"itself moving.",
			"",
			"Read the two letters after v as a chain:",
			"P as seen from A. The inner letters of the",
			"sum match and cancel, which is the check",
			"that it was written the right way round.",
			"",
			"Reversing a pair flips the sign, so",
			"v_AB is the negative of v_BA.",
		} },
	{ name = "Resultant", ch = 3,
		def = {
			"The single vector you get by adding two or",
			"more vectors together. The sum itself.",
			"",
			"Found head to tail in a drawing, or by",
			"adding the components separately.",
		} },
	{ name = "Right-hand rule", ch = 3,
		def = {
			"Gives the direction of a vector product.",
			"The result is perpendicular to the plane",
			"defined by the two vectors being multiplied.",
		} },
	{ name = "Scalar", ch = 3,
		def = {
			"A quantity with magnitude only, such as",
			"temperature. Specified by a number with a",
			"unit, and obeys the rules of ordinary",
			"arithmetic and algebra.",
		} },
	{ name = "Scalar product", ch = 3, sym = "a · b",
		eq = { "a · b = a b cosφ", "a · b = axbx + ayby + azbz" },
		def = {
			"A scalar built from two vectors, where φ is",
			"the angle between their directions. It is the",
			"product of the magnitude of one vector and",
			"the scalar component of the second along the",
			"direction of the first.",
			"",
			"Obeys the commutative law.",
		} },
	{ name = "Significant figures", ch = 1,
		def = {
			"The digits in a measurement that carry",
			"meaning. An answer carries the fewest",
			"significant figures of the values that went",
			"into it.",
		} },
	{ name = "SI units", ch = 1,
		def = {
			"The International System of Units, the",
			"system this book emphasizes.",
			"",
			"Standards, which must be both accessible and",
			"invariable, have been established for the",
			"base quantities by international agreement.",
		} },
	{ name = "Speed, average", ch = 2, sym = "s_avg, m/s",
		eq = { "s_avg = total distance / Δt" },
		def = {
			"Depends on the total distance the particle",
			"moves in the time interval, not on its",
			"start and finish positions.",
			"",
			"Never negative. This is the one most often",
			"confused with average velocity.",
		} },
	{ name = "Standard", ch = 1,
		def = {
			"The reference a base quantity is defined",
			"against. Must be both accessible and",
			"invariable.",
		} },
	{ name = "Trajectory", ch = 4,
		def = {
			"The path of a particle in projectile",
			"motion. It is parabolic.",
		} },
	{ name = "Uniform circular motion", ch = 4,
		eq = { "a = v² / r   toward the center", "T = 2πr / v" },
		def = {
			"A particle traveling along a circle or",
			"circular arc of radius r at constant speed.",
			"",
			"The speed is constant but the velocity is",
			"not, because the direction keeps changing.",
		} },
	{ name = "Unit vector", ch = 3, sym = "i, j, k",
		def = {
			"A vector of magnitude one, directed along a",
			"positive coordinate axis, in a right-handed",
			"coordinate system.",
			"",
			"i, j and k point along positive x, y and z.",
			"They carry the direction, so the numbers",
			"multiplying them are plain scalars.",
		} },
	{ name = "Unit-vector notation", ch = 3, eq = { "a = ax i + ay j + az k" },
		def = {
			"Writing a vector in terms of unit vectors.",
			"",
			"ax i, ay j and az k are the vector",
			"components. ax, ay and az are the scalar",
			"components.",
		} },
	{ name = "Vector", ch = 3,
		def = {
			"A quantity with both magnitude and",
			"direction, such as displacement (5 m,",
			"north). Obeys the rules of vector algebra.",
		} },
	{ name = "Vector product", ch = 3, sym = "a × b",
		eq = { "c = a b sinφ", "b × a = −(a × b)" },
		def = {
			"A vector built from two vectors, where φ is",
			"the smaller of the angles between their",
			"directions. Its magnitude is c.",
			"",
			"Its direction is perpendicular to the plane",
			"defined by the two vectors, given by a",
			"right-hand rule.",
			"",
			"Does NOT obey the commutative law.",
		} },
	{ name = "Velocity, average", ch = 2, sym = "v_avg, m/s", eq = { "v_avg = Δx / Δt" },
		def = {
			"For a particle moving from x₁ to x₂ during",
			"Δt = t₂ − t₁.",
			"",
			"A vector quantity, and its sign gives the",
			"direction of motion. It does not depend on",
			"the actual distance moved, only on the",
			"original and final positions.",
			"",
			"On a graph of x against t it is the slope of",
			"the straight line joining the two ends of",
			"the interval.",
		} },
	{ name = "Velocity, in two dimensions", ch = 4, sym = "v, m/s",
		eq = { "v = vx i + vy j + vz k" },
		def = {
			"The limit of Δr/Δt as Δt shrinks to zero.",
			"",
			"Always directed along the tangent to the",
			"particle's path at the particle's position.",
		} },
}

local view = { pick = 1, scroll = 0, query = "", mode = "list" }

local filtered = nil

-- Plain substring matching on the term name, lowered on both sides, so a query carrying a bracket
-- or a percent sign searches for that character rather than meaning something else.
local function matches()
	if filtered then
		return filtered
	end
	filtered = {}
	local needle = string.lower(view.query)
	for i, term in ipairs(terms) do
		if needle == "" or string.find(string.lower(term.name), needle, 1, true) then
			filtered[#filtered + 1] = i
		end
	end
	return filtered
end

local function refilter()
	filtered = nil
	view.pick = 1
	view.scroll = 0
end

local function rows()
	local h = platform.window and platform.window:height() or 240
	return math.max(1, math.floor((h - 36) / L))
end

local function repaint()
	if platform.window then
		platform.window:invalidate()
	end
end

function on.paint(gc)
	local w = platform.window:width()
	local h = platform.window:height()

	gc:setColorRGB(0, 0, 0)
	gc:fillRect(0, 0, w, 18)
	gc:setColorRGB(255, 255, 255)
	gc:setFont("sansserif", "b", 10)

	if view.mode == "term" then
		local term = terms[view.term]
		gc:drawString(term.name, 4, 1, "top")
		gc:drawString("ch " .. term.ch, w - 34, 1, "top")
	else
		local hits = matches()
		gc:drawString(view.query == "" and "ti_voc   type to narrow"
			or ("find: " .. view.query), 4, 1, "top")
		gc:drawString(#hits .. "", w - 26, 1, "top")
	end

	gc:setColorRGB(0, 0, 0)
	gc:setFont("sansserif", "r", BODY)
	local y = 22

	if view.mode == "term" then
		local term = terms[view.term]
		-- Symbol, unit and formula sit above the prose and in their own colors, because they are what
		-- a reader mid-question is usually after and reading a paragraph to find them wastes the time
		-- the lookup was meant to save.
		if term.sym then
			gc:setColorRGB(0, 0, 150)
			gc:drawString(term.sym, 6, y, "top")
			y = y + L
		end
		for _, line in ipairs(term.eq or {}) do
			gc:setColorRGB(150, 40, 0)
			gc:drawString(line, 6, y, "top")
			y = y + L
		end
		if term.sym or term.eq then
			y = y + 3
		end
		gc:setColorRGB(0, 0, 0)
		for i = view.scroll + 1, #term.def do
			if y + L > h - 16 then
				break
			end
			gc:drawString(term.def[i], 6, y, "top")
			y = y + L
		end
	else
		local hits = matches()
		if #hits == 0 then
			gc:drawString("no term matches " .. view.query, 8, y, "top")
		end
		for n = view.scroll + 1, math.min(#hits, view.scroll + rows()) do
			local term = terms[hits[n]]
			if n == view.pick then
				gc:setColorRGB(0, 0, 160)
				gc:drawString(">", 2, y, "top")
			else
				gc:setColorRGB(0, 0, 0)
			end
			gc:drawString(term.name, 10, y, "top")
			gc:setColorRGB(130, 130, 130)
			gc:drawString(tostring(term.ch), w - 16, y, "top")
			gc:setColorRGB(0, 0, 0)
			y = y + L
		end
	end

	gc:setColorRGB(110, 110, 110)
	gc:setFont("sansserif", "r", 7)
	gc:drawString(view.mode == "term" and "up/down scroll   esc back   tab next term"
		or "type to narrow   enter open   del erase", 4, h - 12, "top")
end

function on.arrowDown()
	if view.mode == "term" then
		view.scroll = math.min(view.scroll + 1, math.max(0, #terms[view.term].def - 1))
	else
		view.pick = math.min(view.pick + 1, #matches())
		if view.pick > view.scroll + rows() then
			view.scroll = view.pick - rows()
		end
	end
	repaint()
end

function on.arrowUp()
	if view.mode == "term" then
		view.scroll = math.max(0, view.scroll - 1)
	else
		view.pick = math.max(1, view.pick - 1)
		if view.pick <= view.scroll then
			view.scroll = view.pick - 1
		end
	end
	repaint()
end

function on.enterKey()
	if view.mode == "list" then
		local hits = matches()
		if hits[view.pick] then
			view.term = hits[view.pick]
			view.mode = "term"
			view.scroll = 0
		end
	end
	repaint()
end

on.returnKey = on.enterKey

function on.escapeKey()
	if view.mode == "term" then
		view.mode = "list"
		view.scroll = 0
	elseif view.query ~= "" then
		view.query = ""
		refilter()
	end
	repaint()
end

-- Tab walks the filtered set, so narrowing to a handful and stepping through them stays inside the
-- same handful rather than returning to the whole alphabet.
function on.tabKey()
	local hits = matches()
	if view.mode ~= "term" or #hits == 0 then
		return
	end
	local at = 1
	for n, index in ipairs(hits) do
		if index == view.term then
			at = n
		end
	end
	view.term = hits[at % #hits + 1]
	view.scroll = 0
	repaint()
end

function on.charIn(ch)
	if view.mode == "term" then
		view.mode = "list"
	end
	view.query = view.query .. ch
	refilter()
	repaint()
end

function on.backspaceKey()
	if view.mode == "term" then
		view.mode = "list"
		repaint()
		return
	end
	if view.query ~= "" then
		view.query = string.sub(view.query, 1, #view.query - 1)
		refilter()
	end
	repaint()
end

on.deleteKey = on.backspaceKey

function on.resize()
	repaint()
end
