-- ti_info: the formulas and the solving method for PHYS 2410 chapters 1 to 4.
--
-- A reference rather than a solver. Nothing here calls the native module, so it loads instantly and
-- works whether or not StepCAS is installed.
--
-- What renders was measured on a CX II running 6.40.74 rather than assumed. drawString carries
-- v₀, t², √, θ, Δ, π, ρ, ·, ×, ° and the relations, and refuses combining diacritics, so k with a
-- caret is written plainly. Displayed formulas go through read-only D2Editor boxes instead, where a
-- Unicode subscript inside the expression renders and an underscore does not.
--
-- The method pages follow the marked key for this test rather than a textbook's ordering, because the
-- marks are given for the shape of the work: knowns per axis, symbolic formula, the vanishing terms
-- struck out, rearrangement, substitution carrying units, and a boxed answer.

platform.apilevel = '2.0'

local L = 14
local HEAD = 2
local BODY = 9

-- Cards come first and are named by what the question sounds like, not by which chapter it came
-- from. In a test the reader knows the wording in front of them and does not know the chapter, so
-- recognition is the index. Every card has the same five parts in the same order, so once you have
-- read one you know where to look in all of them.
local cards = {
	{
		name = "Speeds up or slows down in a line",
		lines = {
			"LOOKS LIKE",
			"A car accelerates from 10 to 30 m/s in 4 s.",
			"How far does it travel? A train brakes to a",
			"stop. Anything moving straight with a steady",
			"change of speed.",
			"",
			"YOU NEED  three of these five",
			"v\226\130\128 start speed    v end speed",
			"a acceleration   t time    \206\148x distance",
			"",
			"FORMULA  pick the one missing what you",
			"neither know nor want",
			{ m = "v=v\226\130\128+a*t", alt = "v = v0 + a t" },
			{ m = "\206\148x=v\226\130\128*t+1/2*a*t^2", alt = "dx = v0 t + a t^2/2" },
			{ m = "v^2=v\226\130\128^2+2*a*\206\148x", alt = "v^2 = v0^2 + 2 a dx" },
			"",
			"STEPS",
			"1  Column of knowns, with units.",
			"2  Write the formula in symbols.",
			"3  Rearrange for the unknown.",
			"4  Substitute, carry units, box it.",
			"",
			"TRAP",
			"Slowing down means a has the opposite sign",
			"to v, not that a is negative by itself.",
		},
	},
	{
		name = "Dropped, or thrown straight up",
		lines = {
			"LOOKS LIKE",
			"A stone is dropped from a 45 m bridge. A ball",
			"is thrown up at 12 m/s. How long, how fast,",
			"how high.",
			"",
			"YOU NEED",
			"a = \2269.80 m/s\194\178 always, taking up as positive",
			"v\226\130\128 = 0 if it was dropped or released",
			"v = 0 at the highest point",
			"",
			"FORMULA  same three as the straight line",
			{ m = "\206\148y=v\226\130\128*t+1/2*a*t^2", alt = "dy = v0 t + a t^2/2" },
			{ m = "t=sqrt(2*\206\148y/a)", alt = "t = sqrt(2 dy / a)" },
			"",
			"STEPS",
			"1  Choose up as positive and keep it.",
			"2  Falling below the start makes \206\148y negative.",
			"3  Dropped from rest kills the v\226\130\128t term.",
			"4  Solve, then check the sign is sensible.",
			"",
			"TRAP",
			"At the top v is zero but a is still \2269.80.",
			"Gravity does not switch off.",
		},
	},
	{
		name = "Position given as a formula in t",
		lines = {
			"LOOKS LIKE",
			"x = 50t + 10t\194\178. Find the average velocity over",
			"the first 3 s, the velocity at t = 3 s, the",
			"acceleration at t = 3 s.",
			"",
			"YOU NEED  the formula and the instants",
			"",
			"FORMULA",
			{ m = "v=dx/dt", alt = "v = dx/dt" },
			{ m = "a=dv/dt", alt = "a = dv/dt" },
			{ m = "v_avg=\206\148x/\206\148t", alt = "v_avg = dx / dt" },
			"",
			"STEPS",
			"1  Average asks for two instants. Work out",
			"   x at each and divide the change by \206\148t.",
			"2  Instantaneous asks for one instant.",
			"   Differentiate, then put the time in.",
			"3  Acceleration is one more derivative.",
			"",
			"TRAP",
			"Average and instantaneous are different",
			"numbers whenever a is not zero. 80 and 110",
			"in the example above.",
		},
	},
	{
		name = "Two objects, one clock",
		lines = {
			"LOOKS LIKE",
			"A key falls 45 m into a boat that is 12 m away",
			"and moving steadily. A car passes under a",
			"dropped ball. One catches the other.",
			"",
			"YOU NEED  enough about ONE of them to get t",
			"",
			"STEPS",
			"1  Two columns, one per object, kept apart.",
			"2  Solve the object you have enough about.",
			"   That gives t.",
			"3  Carry that same t into the other column.",
			"4  Use it there to get what was asked.",
			"",
			"WORKED",
			"Key: \206\148y = \22645 m, v\226\130\128 = 0, a = \2269.80",
			{ m = "t=sqrt(2*(-45)/(-9.80))=3.03", alt = "t = sqrt(2(-45)/(-9.80)) = 3.03 s" },
			"Boat: constant speed, so",
			{ m = "v=12/3.03=3.96", alt = "v = 12 / 3.03 = 3.96 m/s" },
			"",
			"TRAP",
			"Do not average the two motions. They are",
			"separate, joined only at one instant.",
		},
	},
	{
		name = "Launched at an angle",
		lines = {
			"LOOKS LIKE",
			"A stone is fired at 42 m/s at 60\194\176 and hits a",
			"cliff 5.5 s later. Anything that flies.",
			"",
			"YOU NEED  v\226\130\128, the angle, and one more thing",
			"",
			"FORMULA  split once, never mix again",
			{ m = "v\226\130\128x=v\226\130\128*cos(\206\184)", alt = "v0x = v0 cos(th)" },
			{ m = "v\226\130\128y=v\226\130\128*sin(\206\184)", alt = "v0y = v0 sin(th)" },
			"across  ax = 0, so x = v\226\130\128x t",
			{ m = "\206\148y=v\226\130\128y*t-1/2*g*t^2", alt = "dy = v0y t - g t^2/2" },
			"",
			"STEPS",
			"1  Two columns, across and up.",
			"2  Across has no acceleration, so its speed",
			"   never changes.",
			"3  Up is a free fall problem.",
			"4  The two share only t.",
			"",
			"TRAP",
			"A negative vy at the end just means it is",
			"falling when it arrives.",
		},
	},
	{
		name = "Highest point, hang time, range",
		lines = {
			"LOOKS LIKE",
			"How high does it get, how long is it in the",
			"air, how far does it land.",
			"",
			"YOU NEED  v\226\130\128y, and g",
			"",
			"FORMULA",
			"at the top the vertical speed is zero",
			{ m = "t=v\226\130\128y/g", alt = "t_up = v0y / g" },
			{ m = "H=v\226\130\128y^2/(2*g)", alt = "H = v0y^2 / (2 g)" },
			"level ground only:",
			{ m = "R=v\226\130\128^2*sin(2*\206\184)/g", alt = "R = v0^2 sin(2 th) / g" },
			"",
			"STEPS",
			"1  Get v\226\130\128y first.",
			"2  Time to the top, then double it for the",
			"   whole flight on level ground.",
			"3  Range is the across speed times that time.",
			"",
			"CHECK IT TWICE",
			"Do H both ways, by time and by the v\194\178",
			"formula. Same answer means no slip.",
		},
	},
	{
		name = "Add or subtract vectors",
		lines = {
			"LOOKS LIKE",
			"Two displacements, two forces, two velocities",
			"to be combined. Anything with i, j, k in it.",
			"",
			"FORMULA  add the components separately",
			{ m = "3*i-2*j+4*k", alt = "3i - 2j + 4k" },
			"",
			"STEPS",
			"1  Put both into component form.",
			"2  Add the i parts, then the j parts, then",
			"   the k parts. Nothing crosses over.",
			"3  Scaling multiplies every component.",
			"",
			"TRAP",
			"Units must match before you add. Kilometers",
			"and metres in the same sum is the usual",
			"lost mark.",
		},
	},
	{
		name = "Magnitude and angle, or back again",
		lines = {
			"LOOKS LIKE",
			"A speed of 20 m/s at 30\194\176 above horizontal.",
			"Or components given, direction wanted.",
			"",
			"FORMULA",
			{ m = "ax=a*cos(\206\184)", alt = "ax = a cos(th)" },
			{ m = "ay=a*sin(\206\184)", alt = "ay = a sin(th)" },
			{ m = "a=sqrt(ax^2+ay^2)", alt = "a = sqrt(ax^2 + ay^2)" },
			{ m = "tan(\206\184)=ay/ax", alt = "tan(th) = ay / ax" },
			"",
			"STEPS",
			"1  Angle is from the positive x axis unless",
			"   the question says otherwise.",
			"2  Compute, then check the quadrant.",
			"",
			"TRAP",
			"A calculator gives the arctangent in only",
			"two quadrants. Check the signs of both",
			"components and add 180\194\176 if needed.",
		},
	},
	{
		name = "Dot product, or angle between",
		lines = {
			"LOOKS LIKE",
			"a . b asked for, or the angle between two",
			"vectors, or is this perpendicular.",
			"",
			"FORMULA",
			{ m = "a*b=ax*bx+ay*by+a_z*b_z", alt = "a.b = ax bx + ay by + az bz" },
			{ m = "a*b=a*b*cos(\207\134)", alt = "a.b = a b cos(phi)" },
			"",
			"STEPS",
			"1  Multiply matching components and add.",
			"2  For the angle, divide by both magnitudes",
			"   and take the inverse cosine.",
			"",
			"TRAP",
			"The answer is a number, not a vector. Zero",
			"means the two are perpendicular.",
		},
	},
	{
		name = "Cross product",
		lines = {
			"LOOKS LIKE",
			"a \195\151 b asked for, a vector perpendicular to",
			"both, or the area they span.",
			"",
			"FORMULA  set out the determinant",
			"   | i    j    k  |",
			"   | ax   ay   a_z |",
			"   | bx   by   b_z |",
			"",
			"i  (ayb_z \226\136\146 a_zby)",
			"j  \226\136\146(axb_z \226\136\146 a_zbx)",
			"k  (axby \226\136\146 aybx)",
			"",
			"STEPS",
			"1  Write the three rows.",
			"2  Expand along the top, one term at a time.",
			"3  Mind the minus on the j term.",
			"",
			"TRAP",
			"Order matters. b \195\151 a is the negative of",
			"a \195\151 b. Parallel vectors give zero.",
		},
	},
	{
		name = "Wind, current, as seen from",
		lines = {
			"LOOKS LIKE",
			"A plane in wind, a boat in a current, a speed",
			"relative to something else that is moving.",
			"",
			"FORMULA  chain the labels",
			{ m = "v_PA=v_PB+v_BA", alt = "v_PA = v_PB + v_BA" },
			"the inner letters match and cancel",
			"",
			"STEPS",
			"1  Name all three velocities with two",
			"   letters each, and say which is wanted.",
			"2  Write the chain so the inner letters",
			"   cancel. That is the check.",
			"3  Work in components.",
			"4  Convert to magnitude and angle last.",
			"",
			"TRAP",
			"Report the bearing as asked, such as 22.2\194\176",
			"south of west, not as a bare angle.",
		},
	},
	{
		name = "Rank these and justify",
		lines = {
			"LOOKS LIKE",
			"Four paths, three kicks, rank by speed or",
			"time or height. Marks are for the reason.",
			"",
			"STEPS",
			"1  Write the formula for the quantity first.",
			"2  Say which parts are the same for every",
			"   case and which differ.",
			"3  Rank from that, and name any ties.",
			"",
			"THE TWO THAT CATCH PEOPLE",
			"average velocity uses displacement, so",
			"  paths between the same two points tie",
			"average speed uses path length, so they",
			"  separate by how far they went",
			"",
			"PROJECTILES ON LEVEL GROUND",
			"time and height come from the up part",
			"range comes from the across part",
			"",
			"TRAP",
			"A tie is a real answer. Say it and say why.",
		},
	},
	{
		name = "A graph is given, read it",
		lines = {
			"LOOKS LIKE",
			"A curve is drawn and you are asked what is",
			"happening, or to rank points, or to say where",
			"it stops or turns around.",
			"",
			"YOU NEED  the axis labels, nothing else",
			"",
			"STEPS",
			"1  Read the vertical axis first. x, v and a",
			"   graphs look alike and mean different",
			"   things, and this is the lost mark.",
			"2  Find what is asked: a height, a slope, or",
			"   an area. Each answers a different",
			"   question.",
			"3  Read it off using the list below.",
			"4  Check the sign. Below the axis is",
			"   negative, whatever the axis.",
			"",
			"ON AN x AGAINST t GRAPH",
			"height        where it is",
			"above axis    positive side of the origin",
			"crosses axis  passing through the origin",
			"slope         how fast, and which way",
			"flat spot     momentarily at rest",
			"steeper       faster",
			"curved        the speed is changing",
			"peak or dip   it turns around there",
			"",
			"ON A v AGAINST t GRAPH",
			"height        how fast, and which way",
			"above axis    moving in the positive direction",
			"crosses axis  reverses direction there",
			"flat          steady speed, a = 0",
			"slope         the acceleration",
			"area          the displacement",
			"area below    counts as negative",
			"",
			"ON AN a AGAINST t GRAPH",
			"height        the acceleration",
			"flat          constant acceleration",
			"at zero       steady speed, not stopped",
			"area          the change in velocity",
			"",
			"TRAP",
			"A point ON the horizontal axis does not mean",
			"stopped unless the axis is v. On an x graph it",
			"means at the origin, on an a graph it means",
			"steady speed.",
		},
	},
	{
		name = "Units, density, figures",
		lines = {
			"LOOKS LIKE",
			"Convert, or find a density, or how many",
			"significant figures.",
			"",
			"FORMULA",
			{ m = "\207\129=m/V", alt = "rho = m / V" },
			"",
			"STEPS",
			"1  Multiply by a ratio that equals 1, set so",
			"   the unit you do not want cancels.",
			"2  One ratio at a time. Carry the units.",
			"3  Round once, at the end.",
			"",
			"TRAP",
			"Going cm to m is 100 on a length, so it is",
			"100\194\179 on a volume. Cube the conversion.",
			"",
			"Answer carries the fewest figures that went",
			"into it.",
		},
	},
}

local chapters = {
	{
		title = "W  Reading a word problem",
		key = "0",
		topics = {
			{
				name = "START HERE",
				lines = {
					"Stuck on a problem right now? Do this.",
					"",
					"1  Write down what it asks for. One symbol,",
					"   with a question mark. Nothing else yet.",
					"",
					"2  Circle every number in the question and",
					"   write it in a column with its unit.",
					"",
					"3  Press tab to Hidden knowns. Add the ones",
					"   the words gave you without numbers.",
					"",
					"4  Count your knowns. Three or four means",
					"   go. Two means you missed a word, so",
					"   read it again.",
					"",
					"5  Press 5 for the equation chooser.",
					"",
					"THAT IS THE WHOLE METHOD.",
					"Everything else in here is detail for one",
					"of those five steps.",
					"",
					"LOST? PRESS THESE",
					"a-z   find any word in the book",
					"tab   next page",
					"0-7   jump to a chapter",
					"esc   back to the chapter menu",
				},
			},
			{
				name = "What the phrases mean",
				lines = {
					"Translate before you calculate. Almost every",
					"lost mark on this paper is a phrase read",
					"wrong, not arithmetic done wrong.",
					"",
					"PHRASE                 MEANS",
					"from rest              v₀ = 0",
					"dropped, released      v₀ = 0 and a = -g",
					"comes to rest, stops   v = 0",
					"constant velocity      a = 0",
					"steady speed           a = 0",
					"uniform acceleration   a is constant",
					"ignoring air           a = -g exactly",
					"free fall              a = -g",
					"at its highest point   vy = 0",
					"just before it lands   the final instant",
					"thrown up at           v₀y positive",
					"at angle th above      v₀x = v₀ cosθ",
					"the horizontal         v₀y = v₀ sinθ",
					"level ground           start height =",
					"                       finish height",
					"",
					"how fast               v or speed",
					"how far                displacement or",
					"                       distance, check",
					"how long               t",
					"how high               vertical position",
					"how much later         a difference in t",
					"",
					"average                two instants, use",
					"                       a difference",
					"at t = 3 s             one instant, use",
					"instantaneous          a derivative",
					"",
					"relative to, as seen   relative motion,",
					"from, with respect to  chain the labels",
					"due north              plus j",
					"east of north          measured from",
					"                       north toward east",
				},
			},
			{
				name = "The seven reading steps",
				lines = {
					"1  Read it once for the story. Do not pick",
					"   up a pen yet. Know what is happening",
					"   before you know what is given.",
					"",
					"2  Draw it. A rough sketch with the motion",
					"   arrowed is worth more than neat algebra",
					"   on the wrong setup.",
					"",
					"3  Circle every number, with its unit.",
					"",
					"4  Underline the sentence that asks the",
					"   question. Write that symbol down and",
					"   put a question mark next to it. This",
					"   is the one thing students skip, and it",
					"   is why answers come back for the wrong",
					"   quantity.",
					"",
					"5  Name every body and every axis. One",
					"   column each, kept apart on the page.",
					"",
					"6  Translate each phrase into a symbol,",
					"   using the list on the previous page.",
					"",
					"7  Hunt the hidden knowns before you",
					"   choose an equation.",
				},
			},
			{
				name = "Hidden knowns",
				lines = {
					"The number you need is often not given as",
					"a number. It is given as a word.",
					"",
					"dropped               v₀ = 0",
					"starts from rest      v₀ = 0",
					"comes to a stop       v = 0",
					"at the top of a rise  vy = 0",
					"horizontal launch     v₀y = 0",
					"free fall anywhere    a = −9.80 m/s²",
					"projectile, across    ax = 0",
					"constant velocity     a = 0",
					"level ground          dy = 0 over the",
					"                      whole flight",
					"returns to the start  displacement = 0",
					"                      but distance is",
					"                      not zero",
					"",
					"Count what you have after this hunt. Four",
					"knowns for a constant acceleration problem",
					"usually means you can go straight to an",
					"equation. Three means you are probably",
					"missing a word somewhere in the question.",
				},
			},
			{
				name = "Traps in the wording",
				lines = {
					"DISTANCE AGAINST DISPLACEMENT",
					"How far did it travel asks for distance.",
					"How far is it from the start asks for",
					"displacement. A round trip has a large",
					"distance and zero displacement.",
					"",
					"SPEED AGAINST VELOCITY",
					"Speed has no direction and is never",
					"negative. Velocity has a sign. Average",
					"speed and average velocity are computed",
					"from different things and often differ.",
					"",
					"SIGNS",
					"Pick up as positive and keep it for the",
					"whole problem. Then a downward drop is a",
					"negative displacement and g is negative.",
					"Switching convention halfway is the most",
					"common source of a wrong sign.",
					"",
					"MIXED UNITS",
					"km/h beside m/s, cm beside m, grams",
					"beside kilograms. Convert everything",
					"before the first equation, not during.",
					"",
					"THE EXTRA NUMBER",
					"Some questions give a number you do not",
					"need. Having an unused known is not",
					"automatically a mistake.",
					"",
					"MORE THAN ONE PART",
					"Find the height and the speed is two",
					"answers. Both get boxed.",
				},
			},
			{
				name = "Which topic is this question",
				lines = {
					"Sort the question before solving it.",
					"",
					"One object, one straight line, speeds",
					"given                  chapter 2, the",
					"                       five equations",
					"",
					"Position given as a formula in t",
					"                       chapter 2,",
					"                       differentiate",
					"",
					"Two objects, one meeting or one shared",
					"instant                chapter 2 twice,",
					"                       joined by t",
					"",
					"i, j, k anywhere in the question",
					"                       chapter 3",
					"",
					"Dot or cross asked for, or an angle",
					"between two vectors    chapter 3",
					"",
					"Launched at an angle, or anything that",
					"flies                  chapter 4,",
					"                       projectile",
					"",
					"Wind, current, or as seen from",
					"                       chapter 4,",
					"                       relative motion",
					"",
					"Rank these and justify",
					"                       name the formula,",
					"                       then compare",
				},
			},
		},
	},
	{
		title = "1  Measurement",
		key = "1",
		topics = {
			{
				name = "SI base units and prefixes",
				lines = {
					"length   meter      m",
					"mass     kilogram   kg",
					"time     second     s",
					"",
					"G 10⁹    M 10⁶    k 10³",
					"c 10⁻²   m 10⁻³   u 10⁻⁶   n 10⁻⁹",
				},
			},
			{
				name = "Converting units",
				lines = {
					"Multiply by a ratio that equals 1, arranged",
					"so the unit you do not want cancels.",
					"",
					"  1 km = 1000 m, so the ratio",
					"  (1000 m / 1 km) is just the number 1.",
					"",
					"  3.5 km = 3.5 km x (1000 m / 1 km)",
					"         = 3500 m",
					"",
					"Carry the units through every line. If they",
					"do not cancel to the unit you wanted, the",
					"setup is wrong, not the arithmetic.",
				},
			},
			{
				name = "Density",
				lines = {
					"ρ = m / V",
					"",
					"ρ  density   kg/m³",
					"m    mass      kg",
					"V    volume    m³",
					"",
					"1 g/cm³ = 1000 kg/m³",
				},
			},
			{
				name = "Significant figures",
				lines = {
					"An answer carries the fewest significant",
					"figures of the numbers that went into it.",
					"",
					"Keep extra digits inside the working and",
					"round once, at the end. Rounding at every",
					"line walks the answer away from the truth.",
					"",
					"Exact counts and defined ratios do not",
					"limit the figures.",
				},
			},
		},
	},
	{
		title = "2  Straight-line motion",
		key = "2",
		topics = {
			{
				name = "Velocity against speed",
				lines = {
					"average velocity = displacement / time",
					"  v_avg = (x2 - x1) / (t2 - t1)",
					"",
					"average speed = path length / time",
					"  s_avg = (total distance) / (t2 - t1)",
					"",
					"These are different quantities, and that",
					"difference is examined directly.",
					"",
					"Displacement only knows where you started",
					"and where you finished, so it can be zero",
					"on a long journey, and it can be negative.",
					"Distance counts every meter traveled and",
					"is never negative.",
				},
			},
			{
				name = "Instantaneous rates",
				lines = {
					"v = dx/dt         slope of x against t",
					"a = dv/dt         slope of v against t",
					"a = d2x/dt2       second derivative",
					"",
					"Given x(t) as a formula, differentiate to",
					"get v(t), and again to get a(t).",
					"",
					"Average needs two instants. Instantaneous",
					"needs one. A question naming a single time",
					"is asking for a derivative, not a ratio.",
				},
			},
			{
				name = "Constant acceleration: the five",
				lines = {
					"1   leaves out x",
					{ m = "v=v\226\130\128+a*t", alt = "v = v0 + a t" },
					"2   leaves out v",
					{ m = "\206\148x=v\226\130\128*t+1/2*a*t^2", alt = "dx = v0 t + a t^2/2" },
					"3   leaves out t",
					{ m = "v^2=v\226\130\128^2+2*a*\206\148x", alt = "v^2 = v0^2 + 2 a dx" },
					"4   leaves out a",
					{ m = "\206\148x=(v\226\130\128+v)/2*t", alt = "dx = (v0 + v) t/2" },
					"5   leaves out v\226\130\128",
					{ m = "\206\148x=v*t-1/2*a*t^2", alt = "dx = v t - a t^2/2" },
					"",
					"Each one leaves out a different quantity.",
					"List what you know, see what you want, and",
					"pick the equation missing the one thing you",
					"neither know nor want.",
					"",
					"These hold only while a is constant.",
				},
			},
			{
				name = "Free fall",
				lines = {
					"Constant acceleration with a = -g,",
					"taking up as the positive direction.",
					"",
					"  g = 9.80 m/s²",
					"",
					"Everything falls at the same rate here,",
					"whatever its mass, because air is ignored.",
					"",
					"Dropped from rest means v₀ = 0, which kills",
					"the v₀ t term. Falling below the start means",
					"the displacement is negative.",
				},
			},
		},
	},
	{
		title = "3  Vectors",
		key = "3",
		topics = {
			{
				name = "Components and direction",
				lines = {
					"ax = a cosθ",
					"ay = a sinθ",
					"",
					"a = √(ax² + ay²)",
					"tanθ = ay / ax",
					"",
					"theta is measured from the positive x axis,",
					"counterclockwise.",
					"",
					"A calculator returns the arctangent in only",
					"two quadrants, so check the signs of ax and",
					"ay and add 180 degrees when the vector",
					"points into the other half of the plane.",
				},
			},
			{
				name = "Unit vector form",
				lines = {
					"a = ax i + ay j + az k",
					"",
					"i, j, k point along positive x, y, z and",
					"each has length 1. They carry the direction",
					"so the coefficients are plain numbers.",
					"",
					"Adding vectors means adding components:",
					"  a + b = (ax+bx) i + (ay+by) j + (az+bz) k",
					"",
					"Scaling multiplies every component:",
					"  4 b = 4bx i + 4by j + 4bz k",
				},
			},
			{
				name = "Dot product",
				lines = {
					"a · b = ax bx + ay by + az bz",
					"a · b = a b cosφ",
					"",
					"The result is a number, not a vector.",
					"",
					"Zero when the two are perpendicular.",
					"Largest when they point the same way.",
					"Negative when they point more apart than",
					"together.",
				},
			},
			{
				name = "Cross product",
				lines = {
					"Set out the determinant",
					"",
					"    | i    j    k  |",
					"    | ax   ay   az |",
					"    | bx   by   bz |",
					"",
					"a × b =  (ay bz - az by) i",
					"       - (ax bz - az bx) j",
					"       + (ax by - ay bx) k",
					"",
					"Mind the minus on the j term.",
					"",
					"Magnitude is a b sinφ, the result is",
					"perpendicular to both, and it is zero when",
					"they are parallel. Order matters:",
					"  b × a = -(a × b)",
				},
			},
		},
	},
	{
		title = "4  Motion in two dimensions",
		key = "4",
		topics = {
			{
				name = "Position, velocity, acceleration",
				lines = {
					"r = x i + y j + z k",
					"v = dr/dt",
					"a = dv/dt",
					"",
					"v points along the path, always tangent",
					"to it. a need not.",
					"",
					"Each component behaves like its own",
					"straight-line problem, which is what makes",
					"the whole of chapter 2 reusable here.",
				},
			},
			{
				name = "Projectile motion",
				lines = {
					"Split at the start and never mix again.",
					"",
					"across          up",
					"ax = 0          ay = -g",
					"v₀x = v₀ cosθ  v₀y = v₀ sinθ",
					"vx = v₀x        vy = v₀y - g t",
					"x - x0 = v₀x t  y - y0 = v₀y t - g t² / 2",
					"",
					"The two halves share only t. That shared",
					"time is how you get from one to the other.",
					"",
					"At the highest point vy = 0, which is what",
					"you solve for the time to the top.",
					"",
					"H = v₀y² / (2 g)   above the launch",
					"",
					"Range on level ground only:",
					"  R = v₀^2 sin2θ / g",
					"",
					"Final speed from the two components:",
					"  v = √(vx² + vy²)",
				},
			},
			{
				name = "Relative motion",
				lines = {
					"v_PA = v_PB + v_BA",
					"",
					"Read the subscripts as a chain: P as seen",
					"from A equals P as seen from B plus B as",
					"seen from A. The inner letters match and",
					"cancel, which is the check that you have",
					"written it the right way round.",
					"",
					"Reversing a pair flips the sign:",
					"  v_AB = -v_BA",
					"",
					"Work it in components, then convert to a",
					"magnitude and an angle at the end.",
					"",
					"Report a compass bearing the way it was",
					"asked for, such as 22.2 degrees south of",
					"west, not as a bare angle.",
				},
			},
			{
				name = "Uniform circular motion",
				lines = {
					"a = v² / r       toward the center",
					"T = 2πr / v    time for one lap",
					"",
					"The speed is constant but the velocity is",
					"not, because the direction keeps changing,",
					"and that change is the acceleration.",
				},
			},
		},
	},
	{
		title = "5  How to lay out the work",
		key = "5",
		topics = {
			{
				name = "The six steps that earn marks",
				lines = {
					"1  Draw it, and label what is given.",
					"",
					"2  Make a knowns column for each body and",
					"   each axis, kept apart. A falling key and",
					"   the boat below it get one column each.",
					"",
					"3  Write the general formula in symbols,",
					"   before any number goes in.",
					"",
					"4  Strike out the terms that vanish, and",
					"   leave them visible. v₀ t crossed out",
					"   because the drop started from rest shows",
					"   you knew why it went.",
					"",
					"5  Rearrange in symbols. Solve for the",
					"   unknown first, substitute second.",
					"",
					"6  Put the numbers in with their units,",
					"   and box the answer with its unit.",
					"",
					"Units travel through every line. They are",
					"the cheapest check you have that the setup",
					"is right.",
				},
			},
			{
				name = "Choosing the equation",
				lines = {
					"Write down what you know and what is",
					"wanted. The gap between them picks the",
					"equation for you.",
					"",
					"Known v₀, a, t and want v:",
					"  v = v₀ + a t",
					"",
					"Known v₀, a, x and want v, no time given:",
					"  v² = v₀^2 + 2 a x",
					"",
					"Known v₀, v, t and want x, no a given:",
					"  x = (v₀ + v) t / 2",
					"",
					"If the quantity you neither know nor want",
					"appears in the equation you picked, pick a",
					"different one.",
				},
			},
			{
				name = "Two bodies, one clock",
				lines = {
					"When two things move at once, the thing",
					"they share is the time.",
					"",
					"Solve the body you have enough about,",
					"which gives t. Carry that t across into",
					"the other body's column and use it there.",
					"",
					"A key falling 45 m from rest gives t from",
					"the vertical drop. The boat traveling at",
					"constant speed then has v = 12 m / t,",
					"using the same t.",
					"",
					"Do not average the two. They are separate",
					"motions joined at one instant.",
				},
			},
			{
				name = "Checking an answer twice",
				lines = {
					"Where a second route exists, take it. An",
					"answer reached two independent ways is the",
					"strongest check available on paper.",
					"",
					"Maximum height, route one:",
					"  t_up = -v₀y / ay, then put t_up into",
					"  y = v₀y t + ay t² / 2",
					"",
					"Maximum height, route two:",
					"  vy = 0 at the top, so",
					"  H = -v₀y² / (2 ay)",
					"",
					"Same number from a different equation",
					"means the first was not a slip.",
					"",
					"Also check: is the sign sensible, is the",
					"size believable, is the unit right.",
				},
			},
			{
				name = "Ranking questions",
				lines = {
					"Name the formula first, then rank from it.",
					"Marks are for the justification, and a bare",
					"ordering earns few of them.",
					"",
					"Ties are common and are a real answer. Say",
					"which are tied and why.",
					"",
					"Average velocity uses displacement, so four",
					"paths between the same two points over the",
					"same time all tie, however they wander.",
					"",
					"Average speed uses path length, so the same",
					"four paths separate by how far they went.",
					"",
					"For projectiles on level ground, time of",
					"flight and maximum height are fixed by the",
					"vertical component alone, and the range is",
					"what the horizontal component changes.",
				},
			},
		},
	},
	{
		title = "6  Worked examples",
		key = "6",
		topics = {
			{
				name = "One falls while another cruises",
				lines = {
					"A key falls from a bridge 45 m above the",
					"water, into a boat moving at constant",
					"velocity that is 12 m from the point of",
					"impact when the key is released. Find the",
					"speed of the boat.",
					"",
					"SAME PROBLEM, OTHER WORDINGS",
					"A ball is dropped from a window onto a car",
					"passing below. A parcel is released from a",
					"hovering drone onto a moving truck. A stone",
					"drops into a river onto a floating log.",
					"One thing falls, one thing cruises, and",
					"they meet. The clock is what they share.",
					"",
					"KEY                BOAT",
					"dy = -45 m         dx = 12 m",
					"ay = −9.80 m/s²   ax = 0",
					"v₀ = 0             v₀ = ?",
					"",
					"1  dy = v₀ t + ay t² / 2",
					"2  v₀ t struck out, released from rest",
					"3  t = √(2 dy / ay)",
					"4  t = √(2(-45 m)/(−9.80 m/s²))",
					"     t = 3.03 s",
					"",
					"5  dx = v₀ t + ax t² / 2",
					"6  ax t² / 2 struck out, constant velocity",
					"7  v₀ = dx / t = 12 m / 3.03 s",
					"",
					"   v₀ = 3.96 m/s",
					"",
					"Both minus signs are needed in step 4. Down",
					"is negative for the drop and for gravity,",
					"and the ratio comes out positive.",
				},
			},
			{
				name = "Position given as a formula",
				lines = {
					"A proton moves along x with",
					"x = 50 t + 10 t², x in m, t in s. Find",
					"the average velocity over the first 3.0 s,",
					"the instantaneous velocity at t = 3.0 s,",
					"and the instantaneous acceleration there.",
					"",
					"SAME PROBLEM, OTHER WORDINGS",
					"x = 3 t² - 2 t, find when the particle is",
					"momentarily at rest. Find when it turns",
					"around. Find the velocity as it passes the",
					"origin. All of them are the derivative,",
					"read at a chosen instant.",
					"",
					"(a) average needs two instants",
					"1  v_avg = dx / dt = (x(3) - x(0)) / 3",
					"2  x(0) = 0",
					"3  x(3) = 50(3) + 10(3)^2 = 240 m",
					"4  v_avg = (240 m - 0) / 3 s",
					"",
					"   v_avg = 80 m/s",
					"",
					"(b) instantaneous needs one instant",
					"5  v = dx/dt = 50 + 20 t",
					"6  v = 50 + 20(3.0)",
					"",
					"   v = 110 m/s",
					"",
					"(c) differentiate once more",
					"7  a = dv/dt = 20",
					"",
					"   a = 20 m/s²",
					"",
					"Average and instantaneous differ here, 80",
					"against 110, because the speed is changing.",
					"They agree only when a = 0.",
				},
			},
			{
				name = "Cross and dot together",
				lines = {
					"d1 = 3i - 2j + 4k, d2 = -5i + 2j - k.",
					"Find (d1 + d2) . (d1 × 4d2).",
					"",
					"SAME PROBLEM, OTHER WORDINGS",
					"Find the angle between two vectors, use",
					"the dot. Find a vector perpendicular to",
					"both, use the cross. Find the area of the",
					"parallelogram they span, that is the",
					"magnitude of the cross.",
					"",
					"1  d1 + d2 = (3-5)i + (-2+2)j + (4-1)k",
					"",
					"   d1 + d2 = -2i + 0j + 3k",
					"",
					"2  4d2 = -20i + 8j - 4k",
					"",
					"3  set out the determinant",
					"     | i     j    k  |",
					"     | 3    -2    4  |",
					"     | -20   8   -4  |",
					"",
					"4  i term  ((-2)(-4) - (4)(8))   = -24",
					"   j term -((3)(-4) - (4)(-20))  = -68",
					"   k term  ((3)(8) - (-2)(-20))  = -16",
					"",
					"   d1 × 4d2 = -24i - 68j - 16k",
					"",
					"5  dot it with step 1",
					"   (-2)(-24) + (0)(-68) + (3)(-16)",
					"   = 48 + 0 - 48",
					"",
					"   result = 0",
					"",
					"Zero was predictable. d1 × 4d2 is at right",
					"angles to d1 and to d2, so it is at right",
					"angles to any sum of them. Seeing that is",
					"worth as much as the arithmetic.",
				},
			},
			{
				name = "Projectile onto a cliff",
				lines = {
					"A stone is launched at 42.0 m/s at 60.0",
					"degrees above horizontal and strikes a",
					"cliff 5.50 s later. Find the cliff height,",
					"the speed at impact, and the maximum",
					"height above the ground.",
					"",
					"SAME PROBLEM, OTHER WORDINGS",
					"A ball rolls off a table, which is the same",
					"with theta = 0 so v₀y = 0. A cannon fires",
					"onto a hill. A golf ball lands on a green",
					"above the tee. Landing higher or lower than",
					"the launch only changes the sign of dy.",
					"",
					"ACROSS             UP",
					"v₀x = v₀ cos60°   v₀y = v₀ sin60°",
					"    = 21.0 m/s         = 36.4 m/s",
					"ax = 0             ay = −9.80 m/s²",
					"                   dt = 5.50 s",
					"",
					"(a) height of the cliff",
					"1  h = v₀y t + ay t² / 2",
					"2  h = 36.4(5.50) + (-9.80)(5.50)^2 / 2",
					"",
					"   h = 52.0 m",
					"",
					"(b) speed at impact",
					"3  vfx = v₀x = 21.0 m/s, since ax = 0",
					"4  vfy = v₀y + ay t",
					"5  vfy = 36.4 + (-9.80)(5.50) = -12.5 m/s",
					"6  vf = √(vfx² + vfy²)",
					"7  vf = √(21.0^2 + (-12.5)^2)",
					"",
					"   vf = 27.3 m/s",
					"",
					"The minus on vfy says it is falling at",
					"impact. It squares away, but it tells you",
					"the stone is past the top.",
					"",
					"(c) maximum height, route one",
					"8   vy = 0 at the top, so t_H = -v₀y / ay",
					"9   t_H = -36.4 / -9.80 = 3.71 s",
					"10  H = v₀y t_H + ay t_H^2 / 2",
					"",
					"    H = 67.6 m",
					"",
					"(c) maximum height, route two",
					"11  vy² = v₀y² + 2 ay H, with vy = 0",
					"12  H = -v₀y² / (2 ay)",
					"13  H = -(36.4)^2 / (2(-9.80))",
					"",
					"    H = 67.6 m",
					"",
					"Two routes, one answer. Take the second",
					"whenever there is time for it.",
				},
			},
			{
				name = "Wind, current and relative motion",
				lines = {
					"A plane has airspeed 500 km/h. It sets out",
					"for a point 800 km due north, must head",
					"20.0 degrees east of north to go straight",
					"there, and arrives in 2.00 h. Find the",
					"magnitude and direction of the wind.",
					"",
					"SAME PROBLEM, OTHER WORDINGS",
					"A boat crossing a river with a current. A",
					"swimmer aiming upstream. Two cars closing",
					"on each other. Whenever a question says",
					"as seen from, or relative to, this is it.",
					"",
					"1  name the three velocities",
					"   v_PW  plane through the wind, 500 km/h",
					"         at 20.0 deg east of north",
					"   v_PE  plane over the ground",
					"   v_WE  wind over the ground, wanted",
					"",
					"2  v_PE = 800 km / 2.00 h = 400 km/h north",
					"",
					"3  chain rule for subscripts",
					"   v_PE = v_PW + v_WE",
					"   so   v_WE = v_PE - v_PW",
					"",
					"4  components, x east and y north",
					"   v_PE = 400 j",
					"   v_PW = 500 sin20° i + 500 cos20° j",
					"        = 171 i + 470 j",
					"",
					"5  v_WE = 400j - (171i + 470j)",
					"",
					"   v_WE = -171 i - 69.8 j  km/h",
					"",
					"6  magnitude",
					"   √(171^2 + 69.8^2)",
					"",
					"   v_WE = 185 km/h",
					"",
					"7  direction, both components negative so",
					"   it blows toward the southwest",
					"   tanθ = 69.8 / 171",
					"",
					"   th = 22.2 deg south of west",
					"",
					"Answer the bearing the way it was asked.",
					"A bare angle loses the marks that naming",
					"south of west earns.",
				},
			},
			{
				name = "Ranking: velocity against speed",
				lines = {
					"Four paths run from the same start to the",
					"same finish over the same time interval,",
					"across a grid. Rank by average velocity,",
					"then by average speed, and justify.",
					"",
					"(a) average velocity",
					"1  v_avg = dx / dt",
					"2  dx is the same for every path, they",
					"   share a start and a finish",
					"3  dt is given as the same",
					"",
					"   all four tie",
					"",
					"(b) average speed",
					"4  s_avg = d / t, with d the path length",
					"5  now the wandering counts, so the paths",
					"   separate by how far each travels",
					"",
					"   rank by path length, longest first,",
					"   and say which are tied",
					"",
					"The whole question is the difference",
					"between displacement and distance. Write",
					"both formulas down before ranking and the",
					"justification writes itself.",
					"",
					"A tie is a real answer. Say it plainly",
					"and say why.",
				},
			},
			{
				name = "Ranking: projectile paths",
				lines = {
					"Three footballs are kicked from ground",
					"level and reach the same maximum height,",
					"landing at different distances. Rank by",
					"time of flight, initial vertical velocity,",
					"initial horizontal velocity, and initial",
					"speed.",
					"",
					"(a) time of flight",
					"   All tie. Time aloft is set by the",
					"   vertical motion alone, and they all",
					"   rise to the same height.",
					"",
					"(b) initial vertical component",
					"   All tie. H = v₀y² / (2g), so equal",
					"   heights mean equal v₀y.",
					"",
					"(c) initial horizontal component",
					"   Rank by range. x = v₀x t with t the",
					"   same for all, so the one that goes",
					"   furthest has the largest v₀x.",
					"",
					"(d) initial speed",
					"   Same order as (c). v₀ = √(v₀x² +",
					"   v₀y²), and since every v₀y is equal,",
					"   v₀x alone decides the order.",
					"",
					"Each part is one formula plus one",
					"sentence. Name the formula, say which",
					"quantity is fixed and which varies, then",
					"give the order.",
				},
			},
			{
				name = "Units, density and figures",
				lines = {
					"A block measures 2.0 cm by 3.0 cm by",
					"4.0 cm and has mass 48 g. Give its",
					"density in kg/m³.",
					"",
					"SAME PROBLEM, OTHER WORDINGS",
					"Will it float. How much does a slab of it",
					"weigh. Convert a speed from km/h to m/s.",
					"All of them are the chain-link method.",
					"",
					"1  V = 2.0 x 3.0 x 4.0 = 24 cm³",
					"2  ρ = m / V = 48 g / 24 cm³",
					"3  ρ = 2.0 g/cm³",
					"",
					"4  convert, one ratio at a time",
					"   2.0 g/cm³ x (1 kg / 1000 g)",
					"                x (10⁶ cm³ / 1 m³)",
					"",
					"   ρ = 2000 kg/m³",
					"",
					"Check the cube. Going from cm to m is a",
					"factor of 100 on a length, so it is 100^3,",
					"a million, on a volume. Forgetting to cube",
					"the conversion is the usual slip here.",
					"",
					"Two significant figures in, two out.",
				},
			},
		},
	},
	{
		title = "7  Graphs",
		key = "7",
		topics = {
			{
				name = "Constant velocity",
				plot = {
					{ label = "x", rgb = { 0, 0, 0 }, f = function(u) return -0.8 + 1.6 * u end },
					{ label = "v", rgb = { 0, 90, 200 }, f = function(u) return 0.55 end },
					{ label = "a", rgb = { 200, 40, 40 }, f = function(u) return 0 end },
				},
				lines = {
					"No acceleration. Nothing curves.",
					"",
					"x   straight slanted line",
					"v   flat line above zero",
					"a   sits on the zero line",
					"",
					"HOW TO DRAW IT",
					"Mark t across and the quantity up. Put the",
					"zero line in the middle if the value can go",
					"negative. Plot the start value, then one",
					"more point, and join them. A constant slope",
					"needs only two points.",
					"",
					"Steeper x means faster. A downward slope",
					"means moving back toward the origin.",
				},
			},
			{
				name = "Speeding up from rest",
				plot = {
					{ label = "x", rgb = { 0, 0, 0 }, f = function(u) return -0.85 + 1.7 * u * u end },
					{ label = "v", rgb = { 0, 90, 200 }, f = function(u) return -0.1 + 0.9 * u end },
					{ label = "a", rgb = { 200, 40, 40 }, f = function(u) return 0.45 end },
				},
				lines = {
					"Constant positive acceleration.",
					"",
					"x   parabola, opening upward, starting",
					"    flat because v is zero at t = 0",
					"v   straight line rising from zero",
					"a   flat line above zero",
					"",
					"HOW TO DRAW IT",
					"Work downward. Draw a first, it is flat.",
					"v is then a line whose slope is that value",
					"of a. x is then a curve whose slope at",
					"each instant is the height of v.",
					"",
					"Because v starts at zero, x starts",
					"horizontal and steepens. That flat start",
					"is the giveaway for released from rest.",
				},
			},
			{
				name = "Thrown straight up",
				plot = {
					{ label = "y", rgb = { 0, 0, 0 }, f = function(u) return -0.85 + 3.4 * u * (1 - u) end },
					{ label = "v", rgb = { 0, 90, 200 }, f = function(u) return 0.8 - 1.6 * u end },
					{ label = "a", rgb = { 200, 40, 40 }, f = function(u) return -0.5 end },
				},
				lines = {
					"Free fall, taking up as positive.",
					"",
					"y   parabola opening downward",
					"v   straight line falling through zero",
					"a   flat line below zero, at -g",
					"",
					"READ THE TOP OF THE ARC",
					"Where y peaks, v crosses zero. That is the",
					"same instant, and it is how you find the",
					"time to the top.",
					"",
					"a never changes. It does not go to zero at",
					"the peak. Gravity is still pulling while",
					"the object is momentarily still, which is",
					"the most common misreading of this graph.",
					"",
					"The v line is straight the whole way, with",
					"slope -g, through the turning point.",
				},
			},
			{
				name = "Projectile, y against x",
				plot = {
					{ label = "y", rgb = { 0, 0, 0 }, f = function(u) return -0.85 + 3.4 * u * (1 - u) end },
				},
				lines = {
					"Height against horizontal distance, not",
					"against time. The shape is the flight path",
					"as seen from the side.",
					"",
					"On level ground it is a symmetric",
					"parabola. Rising half mirrors falling half.",
					"",
					"HOW TO DRAW IT",
					"Get v₀x and v₀y first. Mark the peak at",
					"half the flight time, height v₀y² / 2g.",
					"Mark the range on the axis. Join with a",
					"smooth arc through those three points.",
					"",
					"Landing higher than launch cuts the arc",
					"early, so only the left part is drawn, and",
					"it is no longer symmetric.",
					"",
					"Steeper launch gives a taller narrower",
					"arc. Forty five degrees gives the longest",
					"range on level ground.",
				},
			},
			{
				name = "Slope and area",
				plot = {
					{ label = "v", rgb = { 0, 90, 200 }, f = function(u) return -0.5 + 1.2 * u end },
				},
				lines = {
					"Two readings turn one graph into three.",
					"",
					"SLOPE, going down the list",
					"slope of x against t  gives v",
					"slope of v against t  gives a",
					"",
					"AREA, going back up",
					"area under v against t  gives",
					"  the displacement",
					"area under a against t  gives",
					"  the change in velocity",
					"",
					"Area below the zero line counts as",
					"negative. A v graph that spends equal",
					"area above and below encloses zero net",
					"area, so the object finished where it",
					"started, however far it actually went.",
					"",
					"For the straight v line drawn here, the",
					"area is just a triangle plus a rectangle,",
					"which is why x comes out quadratic in t.",
				},
			},
			{
				name = "What a point means to a watcher",
				plot = {
					{ label = "x", rgb = { 0, 0, 0 }, f = function(u)
						return -0.2 + 1.9 * u * (1 - u) * 2 - 0.6 * u
					end },
				},
				lines = {
					"Someone standing still is watching the",
					"object. This is what each place on the",
					"curve above looks like to them.",
					"",
					"STARTS BELOW THE LINE",
					"The object begins on the negative side of",
					"the origin, behind the watcher's zero mark.",
					"",
					"CURVE RISING",
					"Moving toward the positive direction, and",
					"the steeper it rises the faster it goes",
					"past.",
					"",
					"CROSSES THE LINE",
					"It passes the watcher's zero mark at that",
					"instant. Nothing special happens to it, it",
					"is simply level with the origin.",
					"",
					"TOP OF THE ARC",
					"Flat for an instant, so it is momentarily",
					"at rest. This is where it stops and turns",
					"around, the furthest point away.",
					"",
					"CURVE FALLING",
					"Coming back toward the watcher, and past",
					"them if it crosses the line again.",
					"",
					"THE SHAPE ITSELF",
					"Curved rather than straight means the speed",
					"is changing the whole time, so there is an",
					"acceleration even where the curve looks",
					"briefly flat.",
				},
			},
			{
				name = "Same motion, three graphs",
				lines = {
					"One object, thrown up and caught. The three",
					"graphs of it do not look alike, and reading",
					"the wrong axis is the usual lost mark.",
					"",
					"POSITION, x against t",
					"an arch. Starts low, peaks, comes back.",
					"The peak is the highest it got.",
					"",
					"VELOCITY, v against t",
					"a straight line sloping down, crossing zero",
					"at the moment of the peak above. Positive",
					"while rising, negative while falling.",
					"",
					"ACCELERATION, a against t",
					"a flat line below zero the whole time. It",
					"does not change, not even at the top.",
					"",
					"LINING THEM UP",
					"The instant the x arch peaks is the same",
					"instant the v line crosses zero. Reading",
					"one graph tells you where to look on the",
					"others.",
					"",
					"GOING BETWEEN THEM",
					"down the list, take the slope",
					"up the list, take the area",
				},
			},
			{
				name = "Naming a graph from its shape",
				lines = {
					"Straight and flat",
					"  the quantity is not changing",
					"",
					"Straight and slanted",
					"  changing at a steady rate, so the",
					"  quantity below it is constant",
					"",
					"Curved",
					"  the rate itself is changing, so the",
					"  quantity below it is not constant",
					"",
					"Curve flat at one point",
					"  the rate is momentarily zero there,",
					"  a turning point",
					"",
					"Line crossing zero",
					"  direction reverses at that instant",
					"",
					"A KINK IS A WARNING",
					"A sharp corner in a v graph means the",
					"acceleration jumped instantly, which real",
					"objects do not do. In these chapters a",
					"corner usually means two separate phases",
					"of motion, and each phase gets its own",
					"knowns column.",
				},
			},
		},
	},
}

local view = { chapter = 1, topic = 0, scroll = 0, mode = "toc", pick = 2 }

-- Declared here because body() reads the contents list and is defined before it.
local toc = nil
local contents

-- Typeset math comes from read-only D2Editor boxes, the same path nps_v4.lua:1435 uses. Measured on
-- this handheld: a Unicode subscript inside the expression renders, an underscore does not, letter
-- subscripts come out as a missing glyph, and 3*i-2*j+4*k is italicised as unit vectors.
-- A box smaller than its content draws nothing at all, so the height comes from the size listener
-- and a formula that has not been measured yet is given three rows to sit in.
local MATH_POOL = 6
local MATH_FONT = 10
local boxes = {}
local measured = {}

local function buildBoxes()
	if #boxes > 0 or D2Editor == nil or D2Editor.newRichText == nil then
		return
	end
	for i = 1, MATH_POOL do
		local slot = { editor = D2Editor.newRichText() }
		slot.editor:setFontSize(MATH_FONT)
		slot.editor:setFocus(false)
		slot.editor:setReadOnly(true)
		slot.editor:setBorder(0)
		slot.editor:setVisible(false)
		slot.editor:setSizeChangeListener(function(editor, w, h)
			editor:resize(w + 2, h + 2)
			local expr = slot.expr
			if expr and (measured[expr] == nil or measured[expr].h ~= h) then
				measured[expr] = { w = w, h = h }
				if platform.window then platform.window:invalidate() end
			end
		end)
		boxes[i] = slot
	end
end

local function itemHeight(item)
	if type(item) == "table" and item.m then
		local m = measured[item.m]
		return m and (m.h + 4) or (L * 3)
	end
	return L
end

local function current()
	return chapters[view.chapter]
end

local function body()
	if view.mode == "card" then
		return cards[view.card].lines
	end
	if view.mode == "toc" then
		local out = {}
		for i, row in ipairs(contents()) do
			out[i] = row.text
		end
		return out
	end
	if view.mode == "search" then
		local rowsOut = {}
		for i, hit in ipairs(view.hits or {}) do
			rowsOut[i] = hit.text
		end
		if #rowsOut == 0 then
			rowsOut[1] = view.query == "" and "type to search" or "no match"
		end
		return rowsOut
	end
	if view.topic == 0 then
		local names = {}
		for i, t in ipairs(current().topics) do
			names[i] = t.name
		end
		return names
	end
	return current().topics[view.topic].lines
end

local function plotOf()
	if view.mode ~= "browse" or view.topic == 0 then
		return nil
	end
	return current().topics[view.topic].plot
end

-- Every topic in the book on one scrollable list, chapter headings included. The reader cannot hold
-- eight chapters in their head to decide which one a question belongs to, so the whole contents is
-- on screen and reachable rather than remembered.
function contents()
	if toc then
		return toc
	end
	toc = {}
	toc[#toc + 1] = { head = true, text = "WHAT DOES YOUR QUESTION LOOK LIKE?" }
	for i, card in ipairs(cards) do
		toc[#toc + 1] = { card = i, text = "  " .. card.name }
	end
	toc[#toc + 1] = { head = true, text = "" }
	toc[#toc + 1] = { head = true, text = "REFERENCE" }
	for ci, chapter in ipairs(chapters) do
		toc[#toc + 1] = { head = true, text = chapter.title }
		for ti, topic in ipairs(chapter.topics) do
			toc[#toc + 1] = { chapter = ci, topic = ti, text = "    " .. topic.name }
		end
	end
	return toc
end

local function rows()
	local h = platform.window and platform.window:height() or 240
	local top = plotOf() and 118 or 0
	return math.max(1, math.floor((h - 34 - top) / L))
end

-- Headings are passed over rather than landed on, so holding the arrow never parks the cursor on a
-- row that enter would do nothing with.
local function tocStep(from, delta)
	local list = contents()
	local i = from + delta
	while i >= 1 and i <= #list and list[i].head do
		i = i + delta
	end
	if i < 1 or i > #list then
		return from
	end
	if i < 1 or i > #list then
		return from
	end
	return i
end

-- Curves come back in -1 to 1 and the zero line sits at the middle of the box, so a quantity that
-- changes sign crosses a line the reader can see rather than leaving the panel.
local function drawPlot(gc, plot, x0, y0, w, h)
	local mid = y0 + h / 2
	gc:setColorRGB(150, 150, 150)
	gc:drawLine(x0, mid, x0 + w, mid)
	gc:setColorRGB(0, 0, 0)
	gc:drawLine(x0, y0, x0, y0 + h)
	gc:drawLine(x0, y0 + h, x0 + w, y0 + h)

	gc:setFont("sansserif", "r", 7)
	gc:drawString("0", x0 - 8, mid - 6, "top")
	gc:drawString("t", x0 + w - 6, y0 + h + 1, "top")

	local legend = x0 + 6
	for _, curve in ipairs(plot) do
		local c = curve.rgb
		gc:setColorRGB(c[1], c[2], c[3])
		local px, py
		for step = 0, 60 do
			local u = step / 60
			local value = curve.f(u)
			if value > 1 then value = 1 end
			if value < -1 then value = -1 end
			local qx = x0 + u * w
			local qy = mid - value * (h / 2 - 2)
			if px then
				gc:drawLine(px, py, qx, qy)
			end
			px, py = qx, qy
		end
		gc:drawString(curve.label, legend, y0 + 1, "top")
		legend = legend + 14
	end
	gc:setColorRGB(0, 0, 0)
end

-- How far the page can scroll is found by filling the viewport from the last row backwards, because
-- a typeset formula is about three text rows tall and counting uniform rows caps the scroll short of
-- the end. That left the bottom of every card carrying a formula unreachable.
local function clamp()
	local list = body()
	local viewport = (platform.window and platform.window:height() or 240) - 38
	local limit = #list - 1
	local used = 0
	for i = #list, 1, -1 do
		used = used + itemHeight(list[i])
		if used > viewport then
			break
		end
		limit = i - 1
	end
	if limit < 0 then limit = 0 end
	if view.scroll > limit then view.scroll = limit end
	if view.scroll < 0 then view.scroll = 0 end
end

local function repaint()
	clamp()
	if platform.window then
		platform.window:invalidate()
	end
end

function on.paint(gc)
	local w = platform.window:width()
	local text = body()
	local visible = rows()

	gc:setColorRGB(0, 0, 0)
	gc:fillRect(0, 0, w, 18)
	gc:setColorRGB(255, 255, 255)
	gc:setFont("sansserif", "b", HEAD + 8)
	if view.mode == "card" then
		gc:drawString(cards[view.card].name, 4, 1, "top")
		gc:drawString(view.card .. "/" .. #cards, w - 40, 1, "top")
	elseif view.mode == "toc" then
		gc:drawString("ti_info   pick what your question looks like", 4, 1, "top")
	elseif view.mode == "search" then
		gc:drawString("find: " .. (view.query or ""), 4, 1, "top")
		gc:drawString(#(view.hits or {}) .. " hit", w - 46, 1, "top")
	else
		-- In a topic the chapter alone does not say where you are, and the reader cannot hold it
		-- between screens, so the trail is on screen rather than remembered.
		local label = current().title
		if view.topic ~= 0 then
			label = string.sub(current().title, 1, 1) .. "  " .. current().topics[view.topic].name
		end
		gc:drawString(label, 4, 1, "top")
		local marker = view.topic == 0 and "menu" or (view.topic .. "/" .. #current().topics)
		gc:drawString(marker, w - 40, 1, "top")
	end

	local y = 22
	local plot = plotOf()
	if plot then
		drawPlot(gc, plot, 18, 24, w - 34, 96)
		y = 142
	end

	buildBoxes()
	gc:setColorRGB(0, 0, 0)
	gc:setFont("sansserif", "r", BODY)
	local bottom = platform.window:height() - 16
	local used = 0
	local selY = nil
	local i = view.scroll + 1
	while i <= #text and y + itemHeight(text[i]) <= bottom do
		local item = text[i]
		if view.mode == "toc" then
			local row = contents()[i]
			if row.head then
				gc:setColorRGB(0, 0, 150)
				gc:setFont("sansserif", "b", BODY)
			else
				gc:setColorRGB(0, 0, 0)
				gc:setFont("sansserif", "r", BODY)
			end
			gc:drawString(item, 8, y, "top")
		elseif type(item) == "table" and item.m then
			-- A measured box is placed where the row would have gone. Until the listener has run its
			-- size is unknown, so the plain spelling is drawn in the slot and replaced next frame.
			local slot = used < MATH_POOL and boxes[used + 1] or nil
			if slot then
				used = used + 1
				if slot.expr ~= item.m then
					slot.expr = item.m
					slot.editor:setExpression("\\0el {" .. item.m .. "}", 0)
				end
				slot.editor:move(10, y)
				slot.editor:setVisible(true)
			end
			if not measured[item.m] then
				gc:drawString(item.alt or item.m, 6, y, "top")
			end
		else
			gc:drawString(item, view.topic == 0 and 10 or 6, y, "top")
		end
		local cursor = view.mode == "toc" and view.pick
			or (view.mode == "search" and view.hit)
			or (view.topic == 0 and view.selected or nil)
		if cursor == i then
			selY = y
		end
		y = y + itemHeight(item)
		i = i + 1
	end

	for n = used + 1, #boxes do
		boxes[n].editor:setVisible(false)
	end

	if selY then
		gc:setColorRGB(0, 0, 160)
		gc:drawString(">", 2, selY, "top")
	end

	-- In search mode the row is a matched line, so name where it came from or the hit is unplaceable.
	if view.mode == "search" and (view.hits or {})[view.hit or 1] then
		local hit = view.hits[view.hit]
		gc:setColorRGB(90, 90, 90)
		gc:setFont("sansserif", "r", 7)
		local from = hit.card and "card" or (chapters[hit.chapter].title .. "  " .. hit.where)
		gc:drawString(from, 4, platform.window:height() - 22, "top")
	end

	gc:setColorRGB(110, 110, 110)
	gc:setFont("sansserif", "r", 7)
	local help
	if view.mode == "card" then
		help = "up/down scroll   tab next card   esc list   a-z find"
	elseif view.mode == "toc" then
		help = "up/down pick   enter open   a-z find   0-7 reference"
	elseif view.mode == "search" then
		help = "type to filter   enter go   del erase   esc cancel"
	elseif view.topic == 0 then
		help = "enter open   tab next   left/right chapter   a-z find"
	else
		help = "up/down scroll   tab next topic   esc back   a-z find"
	end
	gc:drawString(help, 4, platform.window:height() - 12, "top")
end

function on.arrowDown()
	if view.mode == "card" then
		view.scroll = view.scroll + 1
		repaint()
		return
	end
	if view.mode == "toc" then
		view.pick = tocStep(view.pick, 1)
		if view.pick > view.scroll + rows() then
			view.scroll = view.pick - rows()
		end
		repaint()
		return
	end
	if view.mode == "search" then
		view.hit = math.min((view.hit or 1) + 1, math.max(1, #(view.hits or {})))
		if view.hit > view.scroll + rows() then
			view.scroll = view.hit - rows()
		end
		repaint()
		return
	end
	if view.topic == 0 then
		view.selected = math.min((view.selected or 1) + 1, #current().topics)
		if view.selected > view.scroll + rows() then
			view.scroll = view.selected - rows()
		end
	else
		view.scroll = view.scroll + 1
	end
	repaint()
end

function on.arrowUp()
	if view.mode == "card" then
		view.scroll = view.scroll - 1
		repaint()
		return
	end
	if view.mode == "toc" then
		view.pick = tocStep(view.pick, -1)
		if view.pick <= view.scroll + 1 then
			view.scroll = math.max(0, view.pick - 2)
		end
		repaint()
		return
	end
	if view.mode == "search" then
		view.hit = math.max((view.hit or 1) - 1, 1)
		if view.hit <= view.scroll then
			view.scroll = view.hit - 1
		end
		repaint()
		return
	end
	if view.topic == 0 then
		view.selected = math.max((view.selected or 1) - 1, 1)
		if view.selected <= view.scroll then
			view.scroll = view.selected - 1
		end
	else
		view.scroll = view.scroll - 1
	end
	repaint()
end

function on.arrowRight()
	if view.mode == "search" then return end
	view.chapter = view.chapter % #chapters + 1
	view.topic = 0
	view.selected = 1
	view.scroll = 0
	repaint()
end

function on.arrowLeft()
	if view.mode == "search" then return end
	view.chapter = (view.chapter - 2) % #chapters + 1
	view.topic = 0
	view.selected = 1
	view.scroll = 0
	repaint()
end

function on.enterKey()
	if view.mode == "toc" then
		local row = contents()[view.pick]
		if row and row.card then
			view.card = row.card
			view.mode = "card"
			view.scroll = 0
		elseif row and row.chapter then
			view.chapter, view.topic = row.chapter, row.topic
			view.selected = row.topic
			view.mode = "browse"
			view.scroll = 0
		end
		repaint()
		return
	end
	if view.mode == "search" then
		local hit = (view.hits or {})[view.hit or 1]
		view.mode = "browse"
		view.scroll = 0
		if hit and hit.card then
			view.card = hit.card
			view.mode = "card"
			view.scroll = math.max(0, (hit.line or 1) - 2)
		elseif hit then
			view.chapter, view.topic = hit.chapter, hit.topic
			view.selected = hit.topic
			-- Land on the matched line, not merely the page holding it. A match below the fold on a
			-- long page is invisible otherwise, and the reader has to hunt for what they just found.
			view.scroll = math.max(0, (hit.line or 1) - 2)
		end
		repaint()
		return
	end
	if view.topic == 0 then
		view.topic = view.selected or 1
		view.scroll = 0
	end
	repaint()
end

on.returnKey = on.enterKey

function on.escapeKey()
	if view.mode == "toc" then
		return
	end
	if view.mode == "card" then
		view.mode = "toc"
		view.scroll = math.max(0, view.pick - 2)
		repaint()
		return
	end
	if view.mode == "browse" and view.topic ~= 0 then
		-- Escape goes back to the contents rather than a chapter menu, so there is one place to be
		-- lost in rather than two.
		view.mode = "toc"
		view.scroll = math.max(0, view.pick - 2)
		repaint()
		return
	end
	if view.mode == "search" then
		view.mode = "browse"
		view.scroll = 0
		repaint()
		return
	end
	if view.topic ~= 0 then
		view.topic = 0
		view.scroll = 0
	end
	repaint()
end

-- Plain substring matching, lowered on both sides. No pattern is built from the typed text, so a
-- query containing a bracket or a percent sign searches for that character instead of failing or
-- quietly meaning something else.
local function runSearch()
	view.hits = {}
	view.hit = 1
	view.scroll = 0
	local needle = string.lower(view.query or "")
	if needle == "" then
		return
	end
	-- Cards are searched first and land straight on the card, because during a test the card is the
	-- answer and the reference page behind it is the long way round.
	for i, card in ipairs(cards) do
		local hit = string.find(string.lower(card.name), needle, 1, true)
		local where = 1
		if not hit then
			for li, line in ipairs(card.lines) do
				local text = type(line) == "table" and (line.alt or line.m) or line
				if text ~= "" and string.find(string.lower(text), needle, 1, true) then
					hit, where = true, li
					break
				end
			end
		end
		if hit then
			view.hits[#view.hits + 1] = { card = i, text = card.name, where = "card", line = where }
		end
	end
	for ci, chapter in ipairs(chapters) do
		for ti, topic in ipairs(chapter.topics) do
			if string.find(string.lower(topic.name), needle, 1, true) then
				view.hits[#view.hits + 1] =
					{ chapter = ci, topic = ti, text = topic.name, where = chapter.title, line = 1 }
			end
			for li, line in ipairs(topic.lines) do
				-- A formula line is a table, and it is searched by its plain spelling so a query for
				-- a symbol still finds the typeset row it belongs to.
				local text = type(line) == "table" and (line.alt or line.m) or line
				if text ~= "" and string.find(string.lower(text), needle, 1, true) then
					view.hits[#view.hits + 1] =
						{ chapter = ci, topic = ti, text = text, where = topic.name, line = li }
					break
				end
			end
		end
	end
end

function on.charIn(ch)
	if view.mode == "search" then
		view.query = (view.query or "") .. ch
		runSearch()
		repaint()
		return
	end
	for i, chapter in ipairs(chapters) do
		if chapter.key == ch then
			-- A chapter key scrolls the contents to that chapter rather than opening a second kind
			-- of menu, so the list stays the one way through the book.
			for n, row in ipairs(contents()) do
				if row.chapter == i and row.topic == 1 then
					view.mode = "toc"
					view.pick = n
					view.scroll = math.max(0, n - 2)
					break
				end
			end
			repaint()
			return
		end
	end
	if string.find("abcdefghijklmnopqrstuvwxyz", ch, 1, true) then
		view.mode = "search"
		view.query = ch
		runSearch()
		repaint()
	end
end

function on.backspaceKey()
	if view.mode ~= "search" then
		return
	end
	local q = view.query or ""
	if q == "" then
		view.mode = "browse"
	else
		view.query = string.sub(q, 1, #q - 1)
		runSearch()
	end
	repaint()
end

on.deleteKey = on.backspaceKey

-- Tab walks topic to topic without a trip back through the menu, and rolls into the next chapter
-- at the end, so the whole book is reachable from one key.
local function step(delta)
	if view.mode == "search" or view.mode == "toc" then
		return
	end
	if view.mode == "card" then
		view.card = (view.card - 1 + delta) % #cards + 1
		view.scroll = 0
		repaint()
		return
	end
	local target = (view.topic == 0 and (view.selected or 1) or view.topic) + delta
	if target > #current().topics then
		view.chapter = view.chapter % #chapters + 1
		target = 1
	elseif target < 1 then
		view.chapter = (view.chapter - 2) % #chapters + 1
		target = #current().topics
	end
	view.topic = target
	view.selected = target
	view.scroll = 0
	repaint()
end

function on.tabKey()
	step(1)
end

function on.backTabKey()
	step(-1)
end

on.shiftTabKey = on.backTabKey

function on.resize()
	repaint()
end
