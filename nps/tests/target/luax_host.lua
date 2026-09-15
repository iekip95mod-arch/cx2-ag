-- Exercises the native Lua bridge with scripted Giac through the CMake luaxhost target.

local failures = 0
local checks = 0

local function check(ok, what)
    checks = checks + 1
    if not ok then
        failures = failures + 1
        print("FAIL: " .. what)
    end
    return ok
end

-- A check here that also stands as evidence for a numbered requirement. This run is the only one
-- that reads a manifest carrying the release target list, so without a route into the evidence file
-- the requirements it settles look unevidenced. Same tab-separated shape run_tests.cc writes.
local evidence_records = {}

local function evidence(requirement, ok, what)
    check(ok, what)
    evidence_records[#evidence_records + 1] = {
        requirement = requirement, passed = ok and true or false, what = what,
    }
end

local function is_sha256(value)
    return type(value) == "string" and #value == 64 and not value:find("[^0-9a-f]")
end

package.cpath = arg[1] .. ";" .. package.cpath
local nps = require("nps_split")
check(type(nps.walkthrough) == "function", "ordinary mathematical commands have a native walkthrough dispatcher")
do
    check(nps.math_display("1*2^-1") == "(1 / 2)" and
          nps.math_display("x^-1*x") == "((1 / x) * x)",
          "math display formats fractions without cancelling an excluded denominator")
    for _, expression in ipairs({"x^^2", string.rep("x", 4097), string.rep("sin(", 70) .. "x" .. string.rep(")", 70)}) do
        local display, reason = nps.math_display(expression)
        check(display == nil and type(reason) == "string", "invalid or oversized math display refuses explicitly")
    end
    check(not pcall(nps.math_display, "x\0y") and not pcall(nps.math_display, {}),
          "math display rejects invalid Lua arguments before owning native resources")
    check(nps.math_display("3/4") == "(3 / 4)", "math display remains usable after refusal")
end
do
    local fills = {}
    local gc = {
        setColorRGB = function() end,
        fillRect = function(_, x, y, w, h) fills[#fills + 1] = {x, y, w, h} end,
    }
    check(nps.ui_panel(gc, 320, 240, 18, 18) and #fills == 4,
          "the native canvas paints through the caller's graphics context")
    check(nps.ui_icon(gc, 0, 3, 3, 320, 240) and #fills > 8,
          "nRGBlib icon runs cross the Lua compositor boundary")
    for icon = 0, 4 do
        local encoded = nps.ui_icon_image(icon)
        check(type(encoded) == "string" and #encoded == 532 and encoded:byte(1) == 16 and
                  encoded:byte(5) == 16 and encoded:byte(17) == 16 and encoded:byte(19) == 1,
              "native icons cross the bridge as bounded TI bitmap strings")
    end
    for _, invalid in ipairs({-1, 5, 1.5, math.huge, "0", {}}) do
        check(not pcall(nps.ui_icon_image, invalid), "bitmap icons reject invalid identifiers before rendering")
    end
    local calls = #fills
    check(not nps.ui_panel(gc, 320, 240, 200, 100) and #fills == calls,
          "invalid panel geometry never emits a partial frame")
    for _, field in ipairs({"setColorRGB", "fillRect"}) do
        local failing = setmetatable({}, {__index = function(_, key)
            if key == field then error("graphics lookup failed") end
            return gc[key]
        end})
        local ok, painted = pcall(nps.ui_panel, failing, 320, 240, 18, 18)
        check(ok and not painted, "graphics metamethod errors remain inside the protected canvas call")
    end
    for _, value in ipairs({0, -1, 4097, 1.5, math.huge, "320", {}}) do
        check(not pcall(nps.ui_panel, gc, value, 240, 18, 18),
              "native canvas validates dimensions before allocating its frame")
    end
end
local identity = nps.device_identity()
check(identity.ndl_revision == 2001 and identity.ndless_revision == nil,
      "the native identity publishes its measured runtime revision under the current key")

if type(nps.resource_profile_finish) == "function" then
    local function metrics(available)
        return {
            operation = "walkthrough", render_ready_ms = 17, lua_live_bytes = 4096,
            request_failed = false, solver_metrics_available = available,
            arena_nodes = 11, arena_child_slots = 12, derivation_steps = 13,
            rewrites = 14, backend_calls = 15,
        }
    end
    for _, available in ipairs({false, true}) do
        local supplied = metrics(available)
        if not available then
            supplied.arena_nodes = "unknown"
            supplied.arena_child_slots = {}
            supplied.derivation_steps = -1
            supplied.rewrites = math.huge
            supplied.backend_calls = false
        end
        check(nps.resource_profile_begin("walkthrough"), "a profile interval begins")
        local ok, finished = pcall(nps.resource_profile_finish, supplied)
        check(ok and finished.interval_active, "known and unavailable solver metrics both finish")
        if ok and type(nps.test_profile_metrics) == "function" then
            local observed = nps.test_profile_metrics()
            check(observed.solver_metrics_available == available and not observed.request_failed and
                  observed.render_ready_ms == 17 and observed.lua_live_bytes == 4096 and
                  observed.operation == "walkthrough", "profile flags and actual rendering metrics reach native code")
            check(observed.arena_nodes == (available and 11 or 0) and
                  observed.arena_child_slots == (available and 12 or 0) and
                  observed.derivation_steps == (available and 13 or 0) and
                  observed.rewrites == (available and 14 or 0) and
                  observed.backend_calls == (available and 15 or 0),
                  "unavailable solver counters are not read or passed as measured values")
        end
    end
    local supplied = metrics(nil)
    supplied.request_failed = nil
    check(nps.resource_profile_begin("walkthrough"), "the default profile interval begins")
    local ok = pcall(nps.resource_profile_finish, supplied)
    check(ok, "omitted profile flags default to false")
    if ok and type(nps.test_profile_metrics) == "function" then
        local observed = nps.test_profile_metrics()
        check(not observed.request_failed and not observed.solver_metrics_available and observed.arena_nodes == 0,
              "omitted availability never makes counters into measurements")
    end
    supplied = metrics(false)
    supplied.request_failed = true
    nps.resource_profile_begin("walkthrough")
    nps.resource_profile_finish(supplied)
    if type(nps.test_profile_metrics) == "function" then
        check(nps.test_profile_metrics().request_failed, "request failure reaches the native report")
    end
    for _, field in ipairs({"request_failed", "solver_metrics_available"}) do
        for _, value in ipairs({0, 1, "false", {}}) do
            supplied = metrics(true)
            supplied[field] = value
            nps.resource_profile_begin("walkthrough")
            local accepted, message = pcall(nps.resource_profile_finish, supplied)
            check(not accepted and tostring(message):find(field, 1, true) ~= nil,
                  field .. " requires an actual boolean")
            check(nps.resource_profile_begin("walkthrough"), "invalid flags do not leave an active interval")
            nps.resource_profile_finish(metrics(false))
        end
    end
    for _, field in ipairs({"render_ready_ms", "lua_live_bytes", "arena_nodes", "arena_child_slots",
                            "derivation_steps", "rewrites", "backend_calls"}) do
        for _, value in ipairs({-1, 0.5, 4294967296, math.huge, 0/0, "12"}) do
            supplied = metrics(true)
            supplied[field] = value
            nps.resource_profile_begin("walkthrough")
            local accepted = pcall(nps.resource_profile_finish, supplied)
            check(not accepted, field .. " enforces the target integer measurement range")
            check(nps.resource_profile_begin("walkthrough"), "invalid counters do not leave an active interval")
            nps.resource_profile_finish(metrics(false))
        end
    end
    for _, supplied_metrics in ipairs({false, 12, "invalid", {operation = ""}, {operation = 12},
                                       {operation = "walk\0through"}}) do
        nps.resource_profile_begin("walkthrough")
        check(not pcall(nps.resource_profile_finish, supplied_metrics),
              "invalid profile tables and operation names are refused")
        if type(nps.test_profile_metrics) == "function" then
            local observed = nps.test_profile_metrics()
            check(observed.request_failed and not observed.solver_metrics_available,
                  "validation errors close the profile as failed with unknown solver metrics")
        end
        check(nps.resource_profile_begin("walkthrough"), "invalid profile input leaves no active interval")
        nps.resource_profile_finish(metrics(false))
    end
    for _, field in ipairs({"operation", "render_ready_ms", "request_failed", "solver_metrics_available"}) do
        for _, mode in ipairs({"throw", "inherit"}) do
            supplied = metrics(true)
            local inherited = field == "operation" and "walkthrough" or
                              field == "render_ready_ms" and 17 or true
            supplied[field] = nil
            local accessor_calls = 0
            local index = {[field] = inherited}
            if mode == "throw" then
                index = function()
                    accessor_calls = accessor_calls + 1
                    error("profile accessor must not run")
                end
            end
            setmetatable(supplied, {__index = index})
            nps.resource_profile_begin("walkthrough")
            local accepted = pcall(nps.resource_profile_finish, supplied)
            local optional = field == "request_failed" or field == "solver_metrics_available"
            check(accepted == optional and accessor_calls == 0,
                  field .. " uses raw values without executing or inheriting from __index")
            if optional and accepted and type(nps.test_profile_metrics) == "function" then
                check(nps.test_profile_metrics()[field] == false,
                      "an absent raw " .. field .. " retains its false default")
            end
            check(nps.resource_profile_begin("walkthrough"),
                  "metatable-bearing profile input cannot leave an active interval")
            nps.resource_profile_finish(metrics(false))
        end
    end
    supplied = metrics(true)
    supplied.render_ready_ms = 4294967295
    supplied.lua_live_bytes = 4294967295
    nps.resource_profile_begin("walkthrough")
    check(pcall(nps.resource_profile_finish, supplied), "the target uint32 maximum remains accepted")
end

local abi_observations = { 13.25, 13.25, 13.25, -7.5 }
check(nps.test_lua_number_observation_comparator(unpack(abi_observations)),
      "the pure Lua number observation comparator accepts its expected tuple")
for channel = 1, #abi_observations do
    local saved = abi_observations[channel]
    abi_observations[channel] = 0.5
    check(not nps.test_lua_number_observation_comparator(unpack(abi_observations)),
          "the pure Lua number observation comparator rejects a changed channel " .. channel)
    abi_observations[channel] = saved
end

-- PLAT-009 is one requirement over six items, so it is one evidence row over the whole block below
-- rather than six rows that could each pass while the requirement fails. The count is taken here and
-- read again where the block ends, which is why nothing unrelated may be added between them.
local failures_before_manifest = failures

local manifest = nps.capability_manifest()
check(type(manifest) == "table", "the release capability manifest is published")
local split_prefix = "stepcas.split.inputs-sha256."
check(type(manifest.id) == "string" and manifest.id:sub(1, #split_prefix) == split_prefix and
      is_sha256(manifest.id:sub(#split_prefix + 1)),
      "the split manifest id is derived from its build inputs")
check(manifest.artifact == "split" and manifest.stepcas_version == "nps 0.2",
      "the published manifest identifies the split StepCAS artifact")
check(type(manifest.supported_targets) == "table" and #manifest.supported_targets == 2,
      "the published manifest lists only the two release target combinations")
check(manifest.supported_targets[1].calculator_model == "TI-Nspire CX II non-CAS" and
       manifest.supported_targets[1].os_version == "6.2.0.333" and
       manifest.supported_targets[1].ndl_version == "r2022" and
       manifest.supported_targets[2].calculator_model == "TI-Nspire CX II non-CAS" and
       manifest.supported_targets[2].os_version == "6.4.0.74" and
       manifest.supported_targets[2].ndl_version == "r2022",
       "the published targets match the non-CAS release contract")
check(manifest.symbolic_backend.name == "Giac" and
       manifest.symbolic_backend.version == "runtime-unverified" and
       manifest.symbolic_backend.interface_id == "lua5.1.luagiac.caseval-v1" and
       manifest.symbolic_backend.deployment == "external-required-unvalidated",
      "the split manifest does not claim an unchecked external Giac version")
check(type(manifest.installed_modules) == "table" and #manifest.installed_modules == 24,
      "the published manifest lists the compiled solver and content modules")
local expected_modules = {
    "algebra.linear-equation.one-unknown",
    "algebra.quadratic.pure-square.one-unknown",
    "algebra.formula-rearrangement.single-occurrence",
    "algebra.polynomial-rewrite.single-expression",
    "number.integer-method.literal",
    "matrix.ref.rational",
    "matrix.rref.rational",
    "matrix.det.rational",
    "calculus.derivative.single-variable",
    "calculus.integral.indefinite.single-variable",
    "calculus.integral.definite.single-variable",
    "calculus.limit.single-variable",
    "calculus.tangent-line.single-variable",
    "calculus.linearization.single-variable",
    "physics.kinematics.constant-acceleration.one-dimension",
    "physics.kinematics.catch-up.equal-position",
    "physics.kinematics.relative-motion.components.two-dimension",
    "physics.density.mass-volume",
    "physics.vectors.cartesian-addition.two-dimension",
    "physics.vectors.magnitude-components.two-dimension",
    "physics.forces.newton-second-law",
    "physics.work.constant-force-dot-product",
    "units.chain-link-conversion",
    "units.si"
}
for index, id in ipairs(expected_modules) do
    check(manifest.installed_modules[index].id == id,
          "the published manifest identifies installed module " .. id)
end
check(type(manifest.schema_versions) == "table" and #manifest.schema_versions == 2,
      "the published manifest versions its machine-readable schemas")
check(manifest.schema_version == 2 and manifest.schema_versions[1].id == "capability-manifest" and
       manifest.schema_versions[1].version == 2 and
       manifest.schema_versions[2].id == "solution-context" and
       manifest.schema_versions[2].version == 3,
       "the published manifest names both schema versions")
check(type(manifest.integrity_identifiers) == "table" and #manifest.integrity_identifiers == 3,
      "the split manifest carries source, package and UI identities")
check(manifest.integrity_identifiers[1].component == "stepcas.build-inputs" and
      manifest.integrity_identifiers[1].scheme == "sha256" and
      is_sha256(manifest.integrity_identifiers[1].value),
      "the split manifest identifies its StepCAS source inputs")
check(manifest.integrity_identifiers[2].component == "artifact.package" and
      manifest.integrity_identifiers[2].scheme == "runtime-sha256-sidecar" and
      manifest.integrity_identifiers[2].value == "nps_split.luax.sha256.tns",
      "the split manifest points at the package digest it checks for itself")
check(nps.test_native_artifact_package_scheme() == manifest.integrity_identifiers[2].scheme,
      "the Lua bridge preserves the native artifact package integrity scheme")
check(manifest.integrity_identifiers[3].component == "ui.document" and
      manifest.integrity_identifiers[3].scheme == "external-sha256-sidecar" and
      manifest.integrity_identifiers[3].value == "nps_v3.sha256.tns",
      "the split manifest points at its UI document digest, which stays external")

-- The wording matches run_tests.cc's PLAT-009 row on purpose. That row is written from a host
-- manifest with the release target list compiled out, so it never fires; this one reads a manifest
-- that carries the list, and checks each target by model, OS and Ndl version rather than by
-- count. Two rows saying different things about one requirement would be worse than one.
evidence("PLAT-009", failures == failures_before_manifest,
         "the build publishes a capability manifest carrying its StepCAS version, its supported "
         .. "model and OS and Ndl combinations, its symbolic backend version and interface, its "
         .. "installed solver and content modules, its schema versions and its integrity "
         .. "identifiers, with every element of every list filled rather than merely present")

local allocator_headroom_kb = nps.allocator_headroom_kb()
check(type(allocator_headroom_kb) == "number", "allocator headroom is available in release builds")
check(allocator_headroom_kb > 0 and allocator_headroom_kb <= 64 * 1024,
      "allocator headroom is a bounded KiB value")
check(allocator_headroom_kb == math.floor(allocator_headroom_kb) and allocator_headroom_kb % 64 == 0,
      "allocator headroom has 64 KiB resolution")
check(allocator_headroom_kb == 64 * 1024,
      "allocator headroom reports a successful 64 MiB ceiling probe")

-- What the scripted Giac answers next. A function raises or returns a non-string on purpose.
local replies = {}
local giac_calls = 0
luagiac = {
    caseval = function(command)
        giac_calls = giac_calls + 1
        local reply = table.remove(replies, 1)
        if type(reply) == "function" then return reply(command) end
        return reply or "0"
    end,
}

local function script(...)
    replies = { ... }
end

local function command_has_rule(record, rule)
    for _, step in ipairs(record.steps or {}) do
        if step.rule == rule then return true end
    end
    return false
end

local command_cases = {
    {"solve(2*x+5=13,x)", "solve", "2*x+5=13", {"[[4]]", "0"}, "4", "eq.linear.check-by-substitution"},
    {"diff(x^2,x)", "differentiate", "x^2", {"2*x", "0"}, nil, nil},
    {"int(x^2,x)", "integrate", "x^2", {"x^2", "0"}, nil, nil},
    {"simplify(3*x+5*x)", "simplify", "3*x+5*x", {"0"}, "(8 * x)", "alg.collect-like-terms"},
    {"expand((x+1)*(x+2))", "expand", "(x+1)*(x+2)", {"0"}, nil, "alg.distribute"},
    {"factor(x^2-1)", "factor", "x^2-1", {"0"}, nil, "alg.factor.difference-of-squares"},
    {"rearrange(v=u+a*t,a)", "rearrange", "v=u+a*t", {"0"}, nil, nil},
}
for _, case in ipairs(command_cases) do
    script(unpack(case[4]))
    giac_calls = 0
    local record = nps.walkthrough(case[1], "x", "exact")
    check(type(record) == "table" and record.solved and not record.answer_only,
          case[2] .. " exposes a native walkthrough")
    check(record.request_expression == case[1] and record.original_expression == case[3] and
          record.mode == case[2], case[2] .. " keeps full request and operand context separately")
    check(type(record.steps) == "table" and #record.steps > 0,
          case[2] .. " copies the actual derivation records")
    if case[5] then check(record.result == case[5], case[2] .. " returns the native mathematical result") end
    if case[6] then check(command_has_rule(record, case[6]), case[2] .. " includes its semantic rule") end
    local verified_check = false
    for _, step in ipairs(record.steps) do
        if step.kind == "check" and step.verified then verified_check = true end
    end
    check(verified_check or (case[2] == "differentiate" and record.agrees),
          case[2] .. " includes its final verification")
    if case[2] == "solve" or case[2] == "differentiate" or case[2] == "integrate" then
        check(record.agrees and record.giac_calls == 2 and giac_calls == 2,
              case[2] .. " retains the public bridge cross-check policy")
    end
end

do
    for _, case in ipairs({
        {"int(x^2,x,0,1)", "1/3", "defint.fundamental-theorem"},
        {"limit((x^2-1)/(x-1),x,1)", "2", "limit.lhopital"},
        {"limit(1/x,x,infinity)", "0", "limit.infinity"},
        {"lim(1/x,x,-\226\136\158)", "0", "limit.infinity"},
        {"lim((3*x^2+1)/(2*x^2-5),x,\226\136\158)", "3/2", "limit.infinity"},
    }) do
        script(case[2], "0")
        giac_calls = 0
        local record = nps.walkthrough(case[1], "x", "exact")
        check(record.solved and record.has_result and not record.answer_only and record.agrees and
              giac_calls == 2 and command_has_rule(record, case[3]),
              case[1] .. " exposes native calculus with an independent backend comparison")
        check(record.outcome == "evaluated" and record.outcome ~= record.status,
              case[1] .. " names what the calculus engine did rather than repeating its status")
    end
    -- CALC-010. The tangent family answers without a backend comparison of its own, so the bridge
    -- has to carry the slope, the point value and whether the relation is an approximation.
    for _, case in ipairs({
        {"tangent(x^2,x,3)", "tangent.line", false, "equal"},
        {"linearize(x^2,x,3)", "tangent.linearization", true, "approximately equal"},
    }) do
        script("0", "0")
        local record = nps.walkthrough(case[1], "x", "exact")
        check(record.solved and record.has_result and not record.answer_only and
              command_has_rule(record, case[2]) and command_has_rule(record, "tangent.check-line"),
              case[1] .. " exposes the native tangent walkthrough with its final check")
        check(record.tangent_slope == "6" and record.tangent_point_value == "9",
              case[1] .. " reports the slope and the point value the line was built from")
        check(record.approximation == case[3] and record.relation == case[4],
              case[1] .. " states whether its answer is an equality or an approximation")
        check(record.mode == (case[3] and "linearize" or "tangent") and record.outcome == "evaluated",
              case[1] .. " names the family it answered")
    end
    for _, case in ipairs({
        {"tangent(1/x,x,0)", "unsupported form"},
        {"tangent(x^2,x)", "unsupported form"},
    }) do
        local record = nps.walkthrough(case[1], "x", "exact")
        check(not record.solved and not record.has_result and record.outcome == case[2] and
              type(record.detail) == "string" and record.detail ~= "",
              case[1] .. " refuses outside the tangent envelope and says why")
    end
    for _, case in ipairs({
        {"limit(1/x,x,0,1)", "+infinity", "infinite limit"},
        {"limit(1/x,x,0,-1)", "-infinity", "infinite limit"},
        {" lim(1/x,x,0,\226\136\1461)", "-infinity", "infinite limit"},
        {"limit(1/x,x,0)", "does not exist", "does not exist"},
    }) do
        giac_calls = 0
        local record = nps.walkthrough(case[1], "x", "exact")
        check(record.solved and record.result == case[2] and not record.answer_only and giac_calls == 0,
              case[1] .. " preserves native infinite and nonexistent limit classifications")
        check(record.outcome == case[3],
              case[1] .. " reports the limit classification it reached as its outcome")
    end
    for _, refusal in ipairs({
        {"limit(x^40/(x^40+1),x,infinity)", "resource exceeded", "resource limit reached", 0},
        {"limit(1/x,x,infinity,1)", "invalid input", "invalid input", 0},
        {"int(sqrt(x)*exp(x),x,0,1)", "unsupported form", "unsupported", 1},
    }) do
        script("undef", "undef", "undef", "undef")
        giac_calls = 0
        local refused = nps.walkthrough(refusal[1], "x", "exact")
        check(refused.outcome == refusal[2] and refused.status == refusal[3] and
              not refused.solved and refused.result == nil and giac_calls == refusal[4],
              refusal[1] .. " reports its refusal in the calculus engine's own vocabulary")
    end
    -- A definite integral inherits the antiderivative attempt's own word rather than the status it
    -- arrived with. The control after the loop is what says both rows reached that route at all.
    for _, primitive in ipairs({
        {"undef", "refused", "partially solved"},
        {"1", "verification failed", "verification failed"},
    }) do
        script(primitive[1], "undef", "undef", "undef")
        local withheld = nps.walkthrough("int(ln(x),x,1,2)", "x", "exact")
        check(withheld.outcome == primitive[2] and withheld.status == primitive[3] and
              not withheld.solved and withheld.result == nil,
              "a definite integral takes its outcome from the antiderivative attempt: " .. primitive[2])
    end
    script("0", "2*ln(2)-1", "0")
    local antiderivative = nps.walkthrough("int(ln(x),x,1,2)", "x", "exact")
    check(antiderivative.outcome == "evaluated" and antiderivative.solved and
          antiderivative.result ~= nil,
          "and the same integral evaluates when the antiderivative check passes")
    script("9", "1")
    local disagreement = nps.walkthrough("int(x^2,x,0,1)", "x", "exact")
    check(disagreement.outcome == "verification failed" and not disagreement.has_result and
          disagreement.result == nil and disagreement.agrees == false,
          "calculus withholds an answer when the backend comparison fails")
    script("1/3", "0")
    local verified = nps.walkthrough("int(x^2,x,0,1)", "x", "exact")
    -- nps_v4.lua:3464 reads solved only for a record carrying neither expression field, so this
    -- says the shell's missing-context guard never sees a calculus record at all.
    check(type(verified.original_expression) == "string" and
          type(verified.normalized_expression) == "string",
          "a calculus record always carries its expression context")
    for _, failure in ipairs({
        { reply = "Not enough memory", tag = "resource failure" },
        { reply = "Time limit exceeded", tag = "timeout" },
    }) do
        script("1/3", failure.reply)
        local unfinished = nps.walkthrough("int(x^2,x,0,1)", "x", "exact")
        check(unfinished.result == verified.result and
              unfinished.status == "solved but unchecked" and unfinished.agrees == nil and
              unfinished.giac_tag == "exact" and unfinished.giac_compare_tag == failure.tag,
              "a calculus comparison that ran out of room keeps the native answer and says so: " ..
              failure.tag)
        -- stepVerdict reads all three beside the tag, at nps_v4.lua:2689-2693, so a field emitted
        -- and never asserted is a field the next regression removes silently.
        check(unfinished.giac_compare_form == "no result" and
              unfinished.giac_compare_raw == failure.reply and
              unfinished.giac_compare_detail == failure.reply,
              "and the comparison's form, raw reply and detail travel with it: " .. failure.tag)
        -- The engine answered and an unfinished comparison does not take that back. The empty
        -- detail beside it is what keeps resultNote at nps_v4.lua:2760 printing the same note.
        check(unfinished.solved and unfinished.outcome == "evaluated" and unfinished.detail == "",
              "and an unchecked calculus answer is still an answer the engine reached: " ..
              failure.tag)
        script(failure.reply)
        local unasked = nps.walkthrough("int(x^2,x,0,1)", "x", "exact")
        check(unasked.result == verified.result and unasked.status == "solved but unchecked" and
              unasked.agrees == nil and unasked.giac_tag == failure.tag and
              unasked.giac_compare_tag == nil,
              "a calculus backend that could not answer at all keeps the native answer: " ..
              failure.tag)
    end
    -- The control the refusing rows below need. One call is only evidence that the second ask was
    -- refused if the tie-break happened at all, and an integrate handed no backend answers one just
    -- as well by never asking.
    script("0", "x*ln(x)-x", "0")
    giac_calls = 0
    local agreed = nps.integrate("ln(x)", "x")
    check(giac_calls == 3 and agreed.result ~= nil,
          "a backend that answers the integral tie-break is asked for the tie-break and the check")
    for _, reply in ipairs({"Time limit exceeded", "Not enough memory"}) do
        script(reply, "x*ln(x)-x")
        giac_calls = 0
        local exhausted = nps.integrate("ln(x)", "x")
        check(giac_calls == 1 and exhausted.answer_only == false and exhausted.result == nil,
              "a backend that gave up on the integral tie-break is not asked again for the " ..
              "answer: " .. reply)
    end
    script("2")
    giac_calls = 0
    local borrowed = nps.walkthrough("int(x^2,x,0,sqrt(2))", "x", "exact")
    check(borrowed.outcome == "unsupported form" and borrowed.status == "unsupported" and
          borrowed.answer_only and borrowed.has_result and not borrowed.solved and
          borrowed.result == "2" and giac_calls == 1,
          "an answer only Giac produced leaves the native refusal as the calculus outcome")
    giac_calls = 0
    nps.test_escape_pressed(true)
    local cancelled = nps.walkthrough("int(x^2,x,0,1)", "x", "exact")
    nps.test_escape_pressed(false)
    check(cancelled.outcome == "cancelled" and not cancelled.has_result and giac_calls == 0,
          "cancelled calculus never enters the backend")
end

local integer_cases = {
    {"iquo(17,5)", "3", "int.division"},
    {"irem(17,5)", "2", "int.division"},
    {"factorial(5)", "120", "int.factorial-product"},
    {"perm(5,2)", "20", "int.permutation-product"},
    {"comb(5,2)", "10", "int.combination-product"},
    {"is_prime(17)", "2", "int.prime-conclusion"},
    {"nextprime(17)", "19", "int.next-prime"},
    {"powmod(2,10,17)", "4", "int.modular-power"},
    {"ifactor(60)", nil, "int.factor-product"},
    {"gcd(-48,18)", "6", "int.gcd-conclusion"},
    {"gcd(0,0)", "0", "int.gcd-conclusion"},
    {"gcd(0,-18)", "18", "int.gcd-conclusion"},
}
giac_calls = 0
for _, case in ipairs(integer_cases) do
    local record = nps.walkthrough(case[1], "unused + variable", "exact")
    check(type(record) == "table" and record.solved and record.has_result and not record.answer_only,
          case[1] .. " returns a native integer walkthrough")
    check(record.request_expression == case[1] and record.original_expression == case[1] and
          record.mode == "integer", case[1] .. " retains the full scalar command")
    if case[2] then check(record.result == case[2], case[1] .. " returns its exact integer result") end
    check(command_has_rule(record, case[3]) and #record.steps > 0,
          case[1] .. " exposes its actual arithmetic rule")
    local verified = true
    for _, step in ipairs(record.steps) do
        if step.claim ~= "no claim" and not step.verified then verified = false end
    end
    check(verified, case[1] .. " keeps typed verification on every mathematical step")
end
check(giac_calls == 0, "integer walkthrough generation does not evaluate a backend answer")
for _, text in ipairs({"iquo(17)", "factorial(-1)", "perm(5,7)", "comb(101,2)",
                       "is_prime(x)", "nextprime(1000000)", "powmod(2,0,17)",
                       "ifactor(1)", "irem(17,0)", "gcd(1000000001,18)",
                       "gcd(x,18)", "gcd(48)", "gcd(48,18,6)"}) do
    local record = nps.walkthrough(text, "x", "exact")
    check(type(record) == "table" and not record.solved and record.result == nil and not record.answer_only,
          text .. " preserves an explicit integer refusal")
end
check(giac_calls == 0, "integer refusals do not fall back to backend evaluation")
nps.test_escape_pressed(true)
for _, case in ipairs(integer_cases) do
    local record = nps.walkthrough(case[1], "x", "exact")
    check(record.outcome == "cancelled" and record.result == nil and not record.has_result,
          case[1] .. " stops before integer work when cancelled")
end
nps.test_escape_pressed(false)
local decimal_integer = nps.walkthrough("factorial(5)", "x", "decimal")
check(decimal_integer.outcome == "unsupported form" and decimal_integer.numeric_mode == "decimal" and
      decimal_integer.result == nil and giac_calls == 0, "integer commands preserve an unsupported numeric mode")

script("1/2", "0")
local decimal_diff = nps.walkthrough("diff(0.5*x,x)", "x", "decimal")
check(type(decimal_diff) == "table" and decimal_diff.solved, "decimal differentiate walkthrough succeeds")
check(decimal_diff.result_form == "elementary closed form" and decimal_diff.result == "(0.5 * 1)",
      "decimal mode classifies an exact decimal rewrite as elementary closed form")
check(decimal_diff.numeric_mode == "decimal",
      "and records decimal numeric mode")

script("2*x", "0")
local wrapped_command = nps.walkthrough(" ((diff(x^2,x))) ", "y")
check(type(wrapped_command) == "table" and wrapped_command.solved,
      "parentheses around a complete native command still dispatch")
script("2*y", "0")
local command_record = nps.walkthrough(" diff( y^2, y ) ", "x")
check(command_record.solved and command_record.request_expression == " diff( y^2, y ) " and
      command_record.original_expression == " y^2",
      "command dispatch preserves whitespace and uses an explicit variable")
script("2*y", "0")
command_record = nps.walkthrough("diff(y^2)", "y")
check(command_record.solved, "one-argument commands use the selected variable")
giac_calls = 0
for _, text in ipairs({"ref([[1,2],[3,4]])", "rref([[1]])"}) do
    local record = nps.walkthrough(text, "unused + variable")
    check(type(record) == "table" and not record.solved and not record.has_result and
          record.result == nil and record.status == "dependency unavailable",
          "a split backend without row events reports its missing matrix capability")
    check(record.request_expression == text and record.answer_only == false,
          "an unavailable matrix trace preserves the request without inventing an answer")
end
check(giac_calls == 0, "matrix tracing never falls back to string evaluation")
giac_calls = 0
for _, text in ipairs({"normal(x/x)", "determinant(A)", "det(A)+1", "sin(x)", "1+diff(x,x)", "solve(x=1,x)+2"}) do
    check(nps.walkthrough(text, "x") == nil, "unhandled CAS input remains unchanged: " .. text)
end
check(giac_calls == 0, "classification of ordinary CAS input never invokes Giac")
do
    local record = nps.walkthrough("det([[1,2],[3,4]])", "unused + variable", "exact")
    check(type(record) == "table" and record.mode == "determinant" and not record.solved and
          not record.has_result and record.result == nil and not record.answer_only and
          record.status == "dependency unavailable" and record.request_expression == "det([[1,2],[3,4]])",
          "a determinant requires native row tracing instead of a scalar string-backend answer")
    for _, text in ipairs({"det(A)", "det(2)", "det([1,2])", "det([[1,2,3],[4,5,6]])",
                           "det([[x]])", "det([[1.0]])", "det([[1],[2,3]])", "det([])",
                           "det([[1]],x)", "det()", "det([[1,0,0,0,0],[0,1,0,0,0],[0,0,1,0,0],[0,0,0,1,0],[0,0,0,0,1]])"}) do
        local refused = nps.walkthrough(text, "x", "exact")
        check(type(refused) == "table" and refused.mode == "determinant" and not refused.solved and
              not refused.has_result and refused.result == nil and not refused.answer_only and
              refused.request_expression == text and #refused.steps == 0,
              "determinant neighbors retain a native refusal before backend work: " .. text)
    end
    local decimal = nps.walkthrough("det([[1,2],[3,4]])", "x", "decimal")
    check(type(decimal) == "table" and decimal.mode == "determinant" and
          decimal.outcome == "unsupported form" and decimal.numeric_mode == "decimal" and decimal.result == nil,
          "determinant decimal mode is an explicit refusal")
    nps.test_escape_pressed(true)
    local cancelled = nps.walkthrough("det([[1,2],[3,4]])", "x")
    nps.test_escape_pressed(false)
    check(type(cancelled) == "table" and cancelled.outcome == "cancelled" and
          not cancelled.solved and not cancelled.has_result and cancelled.result == nil,
          "a cancelled determinant request withholds the scalar answer")
    check(giac_calls == 0, "determinant refusal and cancellation do not evaluate a fallback")
end
for _, text in ipairs({"diff(x,x,2)", "int(x,x,0)", "solve(x=1,x,y)", "simplify(x,x)",
                       "factor(x,2)", "rearrange(x=1)", "diff(x,x+1)", "solve(x=1,2)",
                       "int(x,x=2)", "solve(", "diff(x,,x)", "factor()", "diff(x\0,y)"}) do
    local record = nps.walkthrough(text, "x")
    check(type(record) == "table" and not record.solved and record.result == nil and
          record.request_expression == text and #record.steps == 0 and
          record.original_expression == nil and record.normalized_expression == nil,
          "recognized invalid command is a structured refusal: " .. text)
end
check(giac_calls == 0, "invalid commands never reach Giac or fall back to CAS")
for _, text in ipairs({"diff([1,2],x)", "int([[1,2],[3,4]],x)", "solve(x=[1,2],x)",
                       "simplify(0*[1,2])", "expand([x,1])", "factor([[1]])",
                       "rearrange(x=[1,2],x)", "gcd([1,2],3)"}) do
    local record = nps.walkthrough(text, "x")
    check(type(record) == "table" and record.status == "unsupported" and
          not record.solved and not record.answer_only and record.result == nil and
          record.request_expression == text and #record.steps == 0,
          "scalar walkthroughs retain an explicit collection refusal: " .. text)
end
check(giac_calls == 0, "collection refusals never ask Giac to invent scalar steps")
command_record = nps.walkthrough("diff(" .. string.rep("x+", 10000) .. "x,x)", "x")
check(command_record.outcome == "resource exceeded" and command_record.result == nil and giac_calls == 0,
      "command parser resource exhaustion stays distinct without backend work")
nps.test_escape_pressed(true)
for _, case in ipairs(command_cases) do
    local record = nps.walkthrough(case[1], "x")
    check(record.outcome == "cancelled" and record.result == nil and not record.answer_only,
          case[2] .. " command preserves cancellation")
end
nps.test_escape_pressed(false)
check(giac_calls == 0, "cancelled commands do not invoke Giac")
script("9", "9")
command_record = nps.walkthrough("diff(x^2,x)", "x")
check(command_record.agrees == false, "command dispatch retains disagreement with the backend")
script("1")
command_record = nps.walkthrough("factor(x^2-1)", "x")
check(command_record.outcome == "verification failed" and command_record.result == nil and
      command_has_rule(command_record, "alg.rewrite.check-by-evaluation"),
      "a failed rewrite cross-check withholds the result and keeps the failed check")
giac_calls = 0
command_record = nps.walkthrough("factor(x=1)", "x")
check(type(command_record) == "table" and not command_record.solved and command_record.result == nil,
      "a recognized factor command outside its envelope is a native refusal")
check(giac_calls == 0, "unsupported rewrite input does not ask Giac to invent a walkthrough")
for _, text in ipairs({"simplify(2+3)", "simplify(0.5+0.5)", "rearrange(v=u+a*t,a)"}) do
    command_record = nps.walkthrough(text, "x", "decimal")
    check(command_record.outcome == "unsupported form" and not command_record.solved and
          command_record.numeric_mode == "decimal" and command_record.result == nil and
          command_record.original_expression == nil and command_record.normalized_expression == nil,
          "exact-only operations retain and refuse an explicit decimal request: " .. text)
end

-- A clean run, and the result shape nps_v2.lua relies on.
script("2*x*sin(x)+x^2*cos(x)", "0")
local r = nps.differentiate("x^2*sin(x)", "x")
check(type(r) == "table", "differentiate returns a table")
check(r.solved == true, "x^2*sin(x) differentiates")
check(r.agrees == true, "a zero difference means Giac agrees")
check(r.giac_calls == 2, "the cross-check costs two calls")
check(type(r.steps) == "table" and #r.steps > 0, "steps come back")
check(r.steps_truncated == false, "and are not truncated")
check(type(r.canonical) == "string", "the canonical form is a string")
check(r.has_result == true, "and the derivative record states that it reached an answer")

script("[[4]]", "0")
r = nps.solve("2x + 5 = 13", "x")
check(r.solved == true and r.result == "4", "a linear equation solves")
check(r.agrees == true, "a peeled solution list agrees")

-- Integration is cross-checked by having Giac differentiate the answer, so the scripted replies
-- are a derivative and then the zero that says it matches the integrand.
script("x^2", "0")
r = nps.integrate("x^2", "x")
check(r.solved == true, "x^2 integrates")
check(type(r.result) == "string" and r.result:find("C", 1, true) ~= nil,
      "with a constant of integration in the answer")
check(r.agrees == true, "Giac differentiating the answer agrees with the integrand")
check(r.giac_calls == 2, "which costs two calls")
check(r.giac_method == "differentiated the answer", "and the result says which check it was")
check(r.has_result == true, "and the integral record states that it reached an answer")
check(type(r.steps) == "table" and #r.steps == 4, "plan, rule, constant and check come back")
local has_check = false
local has_detail = false
for _, s in ipairs(r.steps) do
    if s.kind == "check" then has_check = true end
    if type(s.detail) == "string" then has_detail = true end
end
check(has_check, "the derivative check is a step of its own")
check(has_detail, "and at least one step carries its fuller explanation")

script("1/x", "0")
r = nps.integrate("1/x", "x")
check(r.assumptions == "x > 0", "the logarithm's domain comes back as an assumption")

-- The numeric mode, through the entry points rather than around them. Every other check on the mode
-- reaches an engine directly and sets the field on the context itself, so an entry point that reads
-- the argument, validates it and then forgets to pass it on stays green everywhere else. That is not
-- hypothetical: the assignment went missing from integrate for exactly this reason.
script("-2*cos(0.5*x)", "0")
r = nps.integrate("sin(0.5x)", "x", "decimal")
check(r.numeric_mode == "decimal", "integrate runs in the mode its third argument names")
check(r.result:find(".", 1, true) ~= nil, "and decimal mode writes the answer with a decimal in it")

script("-2*cos(0.5*x)", "0")
r = nps.integrate("sin(0.5x)", "x")
check(r.numeric_mode == "exact", "integrate with no third argument runs exact")
check(r.result:find("0.5", 1, true) == nil,
      "and exact mode reads the typed decimal as a fraction instead")

script("0.5", "0")
r = nps.differentiate("x/2", "x", "decimal")
check(r.numeric_mode == "decimal", "differentiate runs in the mode its third argument names")

script("[[1/2]]", "0")
r = nps.solve("2x = 1", "x", "decimal")
check(r.numeric_mode == "decimal", "solve runs in the mode its third argument names")
check(r.result == "0.5", "and reports the half as a decimal")

local bad = pcall(nps.integrate, "x", "x", "approximate")
check(bad == false, "a mode this build does not have is refused rather than read as exact")

do
    local nul = string.char(0)
    for _, solver in ipairs({
        { "solve", "2x=4", "[[2]]" },
        { "solve_local", "2x=4", "[[2]]" },
        { "differentiate", "x^2", "2*x" },
        { "differentiate_local", "x^2", "2*x" },
        { "integrate", "x", "x" },
        { "integrate_local", "x", "x" },
    }) do
        for _, invalid in ipairs({
            { 2, "x" .. nul .. "y" },
            { 3, "exact" .. nul .. "invalid" },
            { 3, "decimal" .. nul .. "invalid" },
        }) do
            local arguments = { solver[2], "x", "exact" }
            arguments[invalid[1]] = invalid[2]
            local calls_before = giac_calls
            local ok, why = pcall(nps[solver[1]], unpack(arguments))
            check(not ok and type(why) == "string" and why:find("NUL", 1, true) ~= nil and
                  giac_calls == calls_before,
                  solver[1] .. " rejects NUL in argument " .. invalid[1] .. " before Giac")
        end
        -- The variable is typed by the student, so a refusal comes back the way a parse failure does,
        -- as nil and a reason, rather than as a raise the shell's editor filter cannot catch.
        for _, variable in ipairs({ "", " ", "\t", " x", "x ", "2", "x+y", "sin(x)", "x,y", "(x)" }) do
            local calls_before = giac_calls
            local ok, refused, why = pcall(nps[solver[1]], solver[2], variable)
            check(ok and refused == nil and type(why) == "string" and
                  why:find("identifier", 1, true) ~= nil and giac_calls == calls_before,
                  solver[1] .. " refuses variable " .. string.format("%q", variable) ..
                  " with a reason and no raise, before Giac")
        end
        do
            local calls_before = giac_calls
            local ok, refused, why = pcall(nps[solver[1]], solver[2], string.rep("x", 4097))
            check(ok and refused == nil and type(why) == "string" and
                  why:find("identifier", 1, true) ~= nil and giac_calls == calls_before,
                  solver[1] .. " refuses a variable beyond the shared input-byte limit before Giac")
        end
        for _, variable in ipairs({ "long_name2", "_velocity2" }) do
            script(solver[3]:gsub("x", variable), "0")
            local named = nps[solver[1]](solver[2]:gsub("x", variable), variable)
            check(named.solved and named.status == "solved and verified",
                  solver[1] .. " accepts identifier " .. variable)
        end
        script(solver[3]:gsub("x", "pi"), "0")
        local pi_reference = nps[solver[1]](solver[2]:gsub("x", "pi"), "pi")
        for _, expression_name in ipairs({ "pi", "\207\128" }) do
            for _, variable_name in ipairs({ "pi", "\207\128" }) do
                script(solver[3]:gsub("x", "pi"), "0")
                local named = nps[solver[1]](solver[2]:gsub("x", expression_name), variable_name)
                check(named.solved and named.status == pi_reference.status and
                      named.result == pi_reference.result and named.canonical == pi_reference.canonical and
                      named.normalized_expression == pi_reference.normalized_expression,
                      solver[1] .. " binds input " .. expression_name .. " to variable " .. variable_name)
            end
        end
        script(solver[3], "0")
        local omitted = nps[solver[1]](solver[2])
        script(solver[3], "0")
        local defaulted = nps[solver[1]](solver[2], nil, nil)
        check(omitted.solved and defaulted.solved and omitted.numeric_mode == "exact" and
              defaulted.numeric_mode == "exact" and omitted.result == defaulted.result,
              solver[1] .. " preserves omitted and nil variable and mode defaults")
    end

    for _, calculation in ipairs({
        { "canonical", { "x+1" } },
        { "giac", { "x+1" } },
        { "kinematics", { "find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s" } },
        { "kinematics_local", { "find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s" } },
        { "unit_conversion", { "3 m", "cm" } },
        { "density", { "mass", "density", "4 kg/m^3", "volume", "3 m^3" } },
        { "vector_addition", { "(1, 2) m", "(3, 4) m" } },
    }) do
        for index, argument in ipairs(calculation[2]) do
            local arguments = { unpack(calculation[2]) }
            arguments[index] = argument .. nul .. "invalid"
            local calls_before = giac_calls
            local ok, why = pcall(nps[calculation[1]], unpack(arguments))
            check(not ok and type(why) == "string" and why:find("NUL", 1, true) ~= nil and
                  giac_calls == calls_before,
                  calculation[1] .. " rejects NUL in argument " .. index .. " before Giac")
        end
    end
end

local before_answer_only = giac_calls
script("x")
r = nps.integrate("x*sin(x)", "x")
check(r.solved == false and r.answer_only == true,
      "a refused integral can return a Giac answer without claiming a derivation")
check(r.result == "x" and r.giac_tag == "exact",
      "the answer-only integral carries the usable answer and Giac tag")
check(type(r.steps) == "table" and #r.steps == 0 and r.step_count == 0,
      "the answer-only integral carries no derivation")
check(giac_calls == before_answer_only + 1, "the answer-only integral asks Giac once")
check(r.has_result == true, "and an answer it did not derive is still an answer it has")

script("cos(x)")
r = nps.differentiate("nosuchfn(x)", "x")
check(r.outcome == "unsupported form" and r.answer_only == true and r.result == "cos(x)",
      "a refused derivative returns a labelled Giac answer")
check(r.giac_tag == "exact" and #r.steps == 0 and r.step_count == 0,
      "the answer-only derivative has a tag and no derivation")
check(r.has_result == true, "and says it has the answer it was handed")

-- STEP-025 asks for the backend answer to stay segregated from the walkthrough, not to disappear
-- when there is a walkthrough to segregate it from. A student who got partway is the last one who
-- should lose it, so the prefix and the labelled answer arrive together.
script("cos(x)")
r = nps.differentiate("sin(x) + x^x", "x")
check(r.outcome == "unsupported form" and r.status == "partially solved",
      "a derivative that gets partway reports the part it did")
check(r.answer_only == true and r.result == "cos(x)" and r.giac_tag == "exact",
      "and still carries the segregated Giac answer rather than losing it to the prefix")
check(#r.steps == 3 and r.step_count == 3,
      "while keeping the steps it checked, unlike the refusals above that got nowhere")

script("[[2]]")
r = nps.solve("x^3 = 8", "x")
check(r.outcome == "not linear in the unknown" and r.answer_only == true and r.result == "2",
      "a refused nonlinear solve returns a labelled scalar Giac answer")
check(r.giac_tag == "exact" and #r.steps == 0 and r.step_count == 0,
      "the answer-only solve has a tag and no derivation")

-- Kinematics asks Giac twice inside the solve, to rearrange the symbolic equation and to check that
-- form against the value the linear solver reached, then twice more for the cross-check on the
-- substituted equation. Solves answer with a list, is_zero with a zero.
script("[v0+a*t]", "0", "[[17]]", "0")
r = nps.kinematics("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s")
check(r.outcome == "solved", "a kinematics problem solves")
check(r.result == "v = 17 m/s", "with the answer carrying its unit")
check(r.value == "17" and r.unit == "m/s", "and the value and the unit separately")
check(r.precision.kind == "exact" and r.precision.significant_digits == 0,
      "and exposes exact result precision at the top level")
check(r.equation == "(v = (v0 + (a * t)))", "the equation it chose comes back")
check(r.rearranged == "v = (v0 + (a * t))", "and Giac's symbolic rearrangement")
check(r.agrees == true, "the cross-check agrees")
check(r.giac_calls == 4, "which costs four calls in all")
check(r.giac_method == "solved the substituted equation", "and the result says which check it was")
check(type(r.steps) == "table" and #r.steps > 0, "steps come back")
local kin_check = false
for _, s in ipairs(r.steps) do
    if s.kind == "check" then kin_check = true end
end
check(kin_check, "including a check of its own")

script("[v0+a*t]", "0", "[[-49/2]]", "0")
r = nps.kinematics("find v; v0 = 0 m/s; a = -9.8 m/s^2; t = 2.5 s")
check(r.solved == true and r.result == "v = -25 m/s" and
      r.precision.kind == "measured" and r.precision.significant_digits == 2,
      "the kinematics bridge exposes measured result precision after final-only rounding")

local function catch_up_body(name, position, velocity, start_time, motion, acceleration)
    local body = {
        name = name,
        frame = "track",
        position = position,
        velocity = velocity,
        start_time = start_time,
        motion = motion or "constant_velocity",
    }
    if acceleration ~= nil then body.acceleration = acceleration end
    return body
end

local catch_up_input = {
    first = catch_up_body("Atlas", "0 m", "2 m/s", "0 s"),
    second = catch_up_body("Boreal", "0 m", "4 m/s", "5 s"),
}
local before_catch_up = giac_calls
r = nps.catch_up(catch_up_input)
check(type(r) == "table" and r.solved == true and r.has_result == true and
      r.outcome == "solved" and
      r.status == "solved and verified" and r.answer_only == false,
      "the catch-up bridge returns a verified typed solution")
check(r.result == "Atlas and Boreal meet at 10 s and position 20 m" and
      r.event_time.value == "10" and r.event_time.exact_value == "10" and
      r.event_time.unit == "s",
      "the catch-up bridge returns structured exact event time")
check(r.event_position.value == "20" and r.event_position.exact_value == "20" and
      r.event_position.unit == "m" and r.shared_active_start.value == "5",
      "the catch-up bridge returns event position and shared active boundary")
check(r.event_time.precision.kind == "exact" and
      r.event_time.precision.significant_digits == 0 and
      r.event_position.precision.kind == "exact" and r.precision.kind == "exact",
      "the catch-up result retains exact precision metadata")
check(type(r.equation) == "string" and type(r.active_domain) == "string" and
      type(r.substituted) == "string",
      "the catch-up result exposes its equality, active domain and substituted check")
check(type(r.nodes) == "number" and r.nodes > 0 and r.step_count == #r.steps and
      r.rewrites > 0 and r.giac_calls == 0 and giac_calls == before_catch_up,
      "the catch-up bridge reports local solver cost without a backend")
local catch_up_rules = {}
for _, s in ipairs(r.steps) do if s.rule then catch_up_rules[s.rule] = true end end
check(catch_up_rules["physics.catch-up.equal-position"] and
      catch_up_rules["physics.catch-up.shared-domain"] and
      catch_up_rules["physics.catch-up.verify-first-position"] and
      catch_up_rules["physics.catch-up.verify-second-position"],
      "the catch-up bridge retains model, domain and two-body verification provenance")

r = nps.catch_up({
    first = catch_up_body("Lead", "0 m", "36 km/h", "0 min"),
    second = catch_up_body("Chaser", "0 cm", "72 km/h", "5 s"),
})
check(r.event_time.exact_value == "10" and r.event_position.exact_value == "100" and
      r.event_time.unit == "s" and r.event_position.unit == "m",
      "the catch-up bridge converts mixed units exactly to SI")

r = nps.catch_up({
    first = catch_up_body("Atlas", "0 m", "2.00 m/s", "0 s"),
    second = catch_up_body("Boreal", "0 m", "4.00 m/s", "5.00 s"),
})
check(r.event_time.value == "10.0" and r.event_time.exact_value == "10" and
      r.event_position.value == "20.0" and r.event_position.exact_value == "20" and
      r.event_time.precision.kind == "measured" and
      r.event_time.precision.significant_digits == 3 and
      r.precision.kind == "measured" and r.precision.significant_digits == 3,
      "the catch-up bridge separates measured reporting from exact values")

r = nps.catch_up({
    first = catch_up_body("First", "0 m", "1 m/s", "0 s"),
    second = catch_up_body("Second", "1 m", "1 m/s", "0 s"),
})
check(r.outcome == "no meeting" and r.solved == false and r.has_result == true and
      r.result == r.detail and r.event_time == nil,
      "the catch-up bridge preserves a no-meeting classification")
r = nps.catch_up({
    first = catch_up_body("First", "0 m", "1 m/s", "0 s"),
    second = catch_up_body("Second", "5 m", "1 m/s", "5 s"),
})
check(r.outcome == "meeting at every active time" and r.solved == false and
      r.has_result == true and r.result == r.detail and r.event_time == nil and
      r.shared_active_start.exact_value == "5",
      "the catch-up bridge does not force coincident laws to one event")
r = nps.catch_up({
    first = catch_up_body("First", "0 m", "1 m/s", "0 s"),
    second = catch_up_body("Second", "10 m", "2 m/s", "5 s"),
})
check(r.outcome == "meeting before shared active time" and r.has_result == false and
      r.result == nil and r.event_time == nil and
      r.detail:find("before the shared active interval", 1, true) ~= nil,
      "the catch-up bridge refuses an algebraic root before both bodies are active")

r = nps.catch_up({
    first = catch_up_body("Atlas", "0 m", "2 m/s", "0 s",
                          "constant_acceleration", "1 m/s^2"),
    second = catch_up_input.second,
})
check(r.outcome == "nonlinear motion unsupported" and r.status == "unsupported" and
      r.event_time == nil,
      "the catch-up bridge refuses a nonlinear constant-acceleration law")
r = nps.catch_up({
    first = catch_up_body("Atlas", "0 m", "2 m/s", "0 s",
                          "constant_acceleration", "0 m/s^2"),
    second = catch_up_input.second,
})
check(r.outcome == "solved" and r.event_time.exact_value == "10",
      "the catch-up bridge permits a declared exact-zero acceleration reduction")

r = nps.catch_up({
    first = catch_up_input.first,
    second = catch_up_body("Boreal", "0 m", "4 s", "5 s"),
})
check(r.outcome == "dimension mismatch" and r.event_time == nil,
      "the catch-up bridge preserves typed dimension refusal")
r = nps.catch_up({})
check(r.outcome == "invalid problem" and r.detail == "first: body must be a table" and
      #r.steps == 0,
      "the catch-up bridge returns a structured missing-body refusal")
r = nps.catch_up({
    first = catch_up_body("Atlas", "0 m", "2 m/s", "0 s", "constant_velocity", "0 m/s^2"),
    second = catch_up_input.second,
})
check(r.outcome == "invalid problem" and
      r.detail:find("acceleration is only valid", 1, true) ~= nil and #r.steps == 0,
      "constant-velocity input cannot carry contradictory acceleration metadata")
r = nps.catch_up({
    first = catch_up_body("Atlas", "0 m", "2 m/s", "0 s", "constant_acceleration"),
    second = catch_up_input.second,
})
check(r.outcome == "invalid problem" and r.detail:find("acceleration", 1, true) ~= nil and
      #r.steps == 0,
      "constant-acceleration input requires an acceleration quantity string")
r = nps.catch_up({
    first = catch_up_body("Atlas", "0 m", "2 m/s", "0 s", "accelerating"),
    second = catch_up_input.second,
})
check(r.outcome == "invalid problem" and r.detail:find("motion", 1, true) ~= nil and
      #r.steps == 0,
      "the catch-up bridge refuses an unknown motion model without an alias")
local malformed_body = catch_up_body("Atlas", "0 m", "2 m/s", "0 s")
malformed_body.position = 0
r = nps.catch_up({ first = malformed_body, second = catch_up_input.second })
check(r.outcome == "invalid problem" and r.detail:find("position must be a string", 1, true) ~= nil,
      "the catch-up bridge requires quantities to cross the boundary as strings")
r = nps.catch_up("Atlas catches Boreal")
check(r.outcome == "invalid problem" and r.detail == "catch_up input must be a table" and
      #r.steps == 0,
      "the catch-up API refuses string grammar at its table-only boundary")

local oversized_name = catch_up_body(string.rep("A", 4097), "0 m", "2 m/s", "0 s")
r = nps.catch_up({ first = oversized_name, second = catch_up_input.second })
check(r.outcome == "invalid problem" and r.status == "invalid input" and
      r.detail == "first: name is too long" and #r.steps == 0,
      "the catch-up bridge refuses an oversized structured string field")

local long_first = catch_up_body(string.rep("A", 2100), "0 m", "2 m/s", "0 s")
local long_second = catch_up_body(string.rep("B", 2100), "0 m", "4 m/s", "5 s")
r = nps.catch_up({ first = long_first, second = long_second })
check(r.outcome == "resource exceeded" and r.status == "resource limit reached" and
      r.event_time == nil and #r.steps == 0,
      "the catch-up bridge exposes the core aggregate identifier resource limit")

r = nps.unit_conversion("3 cm^2", "mm^2")
check(r.solved == true and r.outcome == "converted" and r.status == "solved and verified",
      "the unit conversion bridge returns a verified conversion")
check(r.result == "300 mm^2" and r.value == r.result and r.exact_value == "300" and
      r.unit == "mm^2" and r.precision.kind == "exact",
      "the unit conversion bridge returns structured exact value and unit fields")
check(r.source_to_si_factor == "0.0001" and r.si_to_target_factor == "1000000" and
      r.combined_factor == "100" and r.giac_calls == 0,
      "the unit conversion bridge exposes each exact chain factor without a backend")
local conversion_rules = {}
for _, s in ipairs(r.steps) do conversion_rules[s.rule] = true end
check(conversion_rules["unit.convert.plan"] and conversion_rules["unit.convert.check-dimension"] and
      conversion_rules["unit.convert.source-to-si"] and
      conversion_rules["unit.convert.si-to-target"],
      "the bridge retains unit conversion plan, check and transformation records")

r = nps.unit_conversion("2.50 cm^3", "m^3")
check(r.result == "0.00000250 m^3" and r.exact_value == "0.0000025" and
      r.precision.kind == "measured" and r.precision.significant_digits == 3,
      "the unit bridge rounds only its reported measured value")
local conversion_has_report = false
for _, s in ipairs(r.steps) do
    if s.rule == "unit.convert.report-precision" then conversion_has_report = true end
end
check(conversion_has_report, "the measured conversion carries its final-only reporting record")

r = nps.unit_conversion("3 m", "s")
check(r.outcome == "dimension mismatch" and r.solved == false and r.result == nil,
      "the unit bridge refuses incompatible dimensions")
check(type(r.steps) == "table" and #r.steps == 2 and r.steps[2].failed == true,
      "dimension refusal retains the failed pre-arithmetic check")
r = nps.unit_conversion("3 m", "parsec")
check(r.outcome == "invalid target unit" and r.solved == false and r.result == nil and
      type(r.steps) == "table" and #r.steps == 0,
      "the unit bridge returns a structured target parse refusal")

r = nps.density("mass", "density", "2 g/cm^3", "volume", "3 cm^3")
check(r.solved == true and r.outcome == "solved" and r.status == "solved and verified",
      "the density bridge returns a verified solution")
check(r.result == "mass = 0.006 kg" and r.value == "0.006" and r.exact_value == "0.006" and
      r.unit == "kg" and r.unknown == "mass" and r.precision.kind == "exact",
      "the density bridge returns structured answer fields in SI")
local density_rules = {}
for _, s in ipairs(r.steps) do
    if s.rule then density_rules[s.rule] = true end
end
check(density_rules["physics.density.definition"] and
      density_rules["physics.density.check-dimensions"] and
      density_rules["physics.density.convert-units"] and
      density_rules["physics.density.substitute"] and
      density_rules["physics.density.check-candidate"],
      "the bridge retains density planning, conversion, solve and verification records")
check(r.giac_calls == 0 and type(r.equation) == "string" and type(r.substituted) == "string",
      "density stays local and returns both symbolic and substituted relations")

r = nps.density("mass", "density", "2.00 g/cm^3", "volume", "3.00 cm^3")
check(r.solved == true and r.precision.kind == "measured" and
      r.precision.significant_digits == 3,
      "the density bridge exposes measured result precision")

r = nps.density("mass", "density", "4 kg/m^3", "density", "5 kg/m^3")
check(r.outcome == "duplicate known" and r.solved == false and r.result == nil and #r.steps == 0,
      "the density bridge preserves a typed duplicate-known refusal")
r = nps.density("mass", "density", "4 kg/m^3", "volume", "3 s")
check(r.outcome == "dimension mismatch" and r.solved == false and r.result == nil and #r.steps == 0,
      "the density bridge rejects a known with the wrong dimension before solving")
r = nps.density("mass", "density", "not a quantity", "volume", "3 m^3")
check(r.outcome == "invalid input" and type(r.detail) == "string" and #r.steps == 0,
      "the density bridge returns a structured quantity parse refusal")
r = nps.density("weight", "density", "4 kg/m^3", "volume", "3 m^3")
check(r.outcome == "invalid input" and r.detail == "unknown density variable weight" and
      #r.steps == 0,
      "the density bridge refuses an unknown typed variable without inventing a mapping")

r = nps.vector_addition("(0.00120, 0.0020) km", "5 i + 5 j m")
check(r.solved == true and r.outcome == "solved" and r.status == "solved and verified",
      "the vector addition bridge returns a verified solution")
check(r.result == "(6.2 i + 7.0 j) m" and r.value == r.result,
      "the vector addition bridge returns Cartesian unit-vector form with units")
check(r.precision.kind == "measured" and r.precision.significant_digits == 2,
      "the vector addition bridge exposes measured result precision")
check(r.answer_only == false and r.giac_calls == 0,
      "vector addition is a recorded local solve without a Giac fallback")
local vector_rules = {}
for _, s in ipairs(r.steps) do vector_rules[s.rule] = true end
check(vector_rules["vec.add.plan"] and vector_rules["vec.add.convert-si"] and
      vector_rules["vec.add.component-i"] and vector_rules["vec.add.component-j"] and
      vector_rules["vec.add.report-precision"],
      "the bridge retains the vector plan, conversion, component and reporting steps")

r = nps.vector_addition("(1, 2, 3) m", "(4, 5, 6) m")
check(r.solved == true and r.outcome == "solved" and r.status == "solved and verified",
      "the vector bridge solves a rank-three addition")
check(r.result == "(5 i + 7 j + 9 k) m" and r.value == r.result and
      r.precision.kind == "exact",
      "the rank-three bridge answer carries every axis in unit-vector form")
local rank_three_rules = {}
for _, s in ipairs(r.steps) do rank_three_rules[s.rule] = true end
check(rank_three_rules["vec.add.component-i"] and rank_three_rules["vec.add.component-j"] and
      rank_three_rules["vec.add.component-k"],
      "the bridge emits the k component step beside the i and j steps")

r = nps.vector_addition("(1, 2, 3) m", "(4, 5) m")
check(r.outcome == "rank mismatch" and r.solved == false and r.result == nil,
      "the vector bridge still refuses two vectors whose ranks disagree")
r = nps.vector_addition("(1, 2) m", "(3, 4) s")
check(r.outcome == "dimension mismatch" and r.solved == false and r.result == nil,
      "the vector bridge refuses incompatible dimensions")
r = nps.vector_addition("not a vector", "(3, 4) m")
check(r.outcome == "invalid input" and type(r.detail) == "string" and #r.steps == 0,
      "the vector bridge returns a structured parse refusal")

local exact_precision = { kind = "exact", significant_digits = 0 }
local measured_two = { kind = "measured", significant_digits = 2 }
local measured_three = { kind = "measured", significant_digits = 3 }
local relative_motion_input = {
    subject_name = "drone",
    reference_name = "wind",
    subject_velocity = {
        x = "36", y = "-18", rank = 2, frame = "ground", unit = "km/h",
        precision = exact_precision,
    },
    reference_velocity = {
        x = "3", y = "1", rank = 2, frame = "ground", unit = "m/s",
        precision = exact_precision,
    },
}
local before_relative_motion = giac_calls
script("7", "-6")
r = nps.relative_motion(relative_motion_input)
check(r.solved == true and r.outcome == "solved" and r.status == "solved and verified",
      "the relative-motion bridge returns a verified typed solution")
check(r.result == "(7 i - 6 j) m/s" and r.exact_x == "7" and r.exact_y == "-6" and
      r.unit == "m/s" and r.frame == "ground",
      "the relative-motion bridge returns exact SI components and frame")
check(r.direction == "southeast" and
      r.interpretation:find("southeast relative to wind", 1, true) ~= nil,
      "the relative-motion bridge interprets the negative component direction")
check(r.giac_calls == 2 and giac_calls == before_relative_motion + 2,
      "the relative-motion bridge checks both components with Giac")
check(r.agrees == true and r.giac_tag == "exact" and
      r.giac_method == "Giac Simplify and local canonical comparison",
      "successful relative-motion component checks publish explicit trust metadata")
local relative_rules = {}
for _, s in ipairs(r.steps) do if s.rule then relative_rules[s.rule] = true end end
check(relative_rules["physics.relative-motion.definition"] and
      relative_rules["physics.relative-motion.convert-si"] and
      relative_rules["physics.relative-motion.component-i"] and
      relative_rules["physics.relative-motion.component-j"] and
      relative_rules["physics.relative-motion.interpret-direction"],
      "the bridge retains relative-motion law, conversion, component and direction provenance")

local before_relative_local = giac_calls
r = nps.relative_motion_local(relative_motion_input)
check(r.solved == true and r.result == "(7 i - 6 j) m/s" and r.giac_calls == 0 and
      giac_calls == before_relative_local,
      "relative_motion_local uses the exact vector library without a backend")
check(r.agrees == nil and r.giac_tag == nil and r.giac_method == nil,
      "relative_motion_local does not claim an unrequested cross-check")
r = nps.relative_motion_local({
    subject_name = "drone",
    reference_name = "wind",
    subject_velocity = {
        x = "10.0", y = "-2.0", rank = 2, frame = "ground", unit = "m/s",
        precision = measured_two,
    },
    reference_velocity = {
        x = "4.00", y = "3.00", rank = 2, frame = "ground", unit = "m/s",
        precision = measured_three,
    },
})
check(r.result == "(6.0 i - 5.0 j) m/s" and r.exact_x == "6" and r.exact_y == "-5",
      "the Lua bridge preserves exact components while reporting trailing decimal zeros")
check(r.precision.kind == "measured" and r.precision.significant_digits == 2 and
      r.precision.last_significant_decimal_place == -1,
      "the Lua bridge transports mixed component precision through relative motion")
script("8")
r = nps.relative_motion(relative_motion_input)
check(r.outcome == "verification failed" and r.result == nil and r.giac_calls == 1 and
      r.agrees == false and r.giac_tag == "exact" and
      r.giac_method == "Giac Simplify and local canonical comparison",
      "a disagreeing relative-motion backend withholds the local vector")
-- Both axes, because a backend that is down for one is down for the other, and a single scripted
-- reply would let the second axis fall through to the "0" default and genuinely disagree.
script("Error: Bad Argument Value", "Error: Bad Argument Value")
r = nps.relative_motion(relative_motion_input)
check(r.outcome == "solved" and r.result == "(7 i - 6 j) m/s" and r.giac_calls == 2 and
      r.status == "solved but unchecked" and r.agrees == nil and
      r.giac_tag == "backend failure" and
      r.giac_method == "Giac Simplify and local canonical comparison",
      "a relative-motion backend failure leaves both components computed and nothing compared")
r = nps.relative_motion_local({})
check(r.outcome == "invalid problem" and r.detail:find("subject_name", 1, true) ~= nil and
      #r.steps == 0,
      "the relative-motion bridge returns a structured missing-field refusal")
local oversized_relative_motion = {
    subject_name = string.rep("A", 2100),
    reference_name = string.rep("B", 2100),
    subject_velocity = relative_motion_input.subject_velocity,
    reference_velocity = relative_motion_input.reference_velocity,
}
r = nps.relative_motion_local(oversized_relative_motion)
check(r.outcome == "resource exceeded" and r.result == nil and #r.steps == 0,
      "the relative-motion bridge bounds aggregate structured identity text")

local work_input = {
    force = {
        x = "3", y = "4", rank = 2, frame = "lab", unit = "N",
        precision = exact_precision,
    },
    displacement = {
        x = "2", y = "1", rank = 2, frame = "lab", unit = "m",
        precision = exact_precision,
    },
    force_profile = "constant",
}
script("10")
r = nps.work(work_input)
check(r.solved == true and r.outcome == "solved" and r.status == "solved and verified",
      "the work bridge returns a verified typed solution")
check(r.result == "10 kg m^2/s^2" and r.value == "10" and r.exact_value == "10" and
      r.unit == "kg m^2/s^2" and r.sign == "positive",
      "the work bridge returns exact SI value, unit and sign fields")
check(r.backend_value == "10" and r.giac_calls == 1 and r.precision.kind == "exact" and
      r.precision.significant_digits == 0,
      "work uses one independent backend check and preserves precision metadata")
local work_rules = {}
for _, s in ipairs(r.steps) do if s.rule then work_rules[s.rule] = true end end
check(work_rules["physics.work.evaluate-dot"] and work_rules["physics.work.check-candidate"] and
      work_rules["physics.work.giac-cross-check"],
      "the work bridge retains local production, candidate check and backend provenance")

local before_work_local = giac_calls
r = nps.work_local({
    force = {
        x = "2", y = "-3", rank = 2, frame = "lab", unit = "N",
        precision = exact_precision,
    },
    displacement = {
        x = "-1", y = "4", rank = 2, frame = "lab", unit = "m",
        precision = exact_precision,
    },
    force_profile = "constant",
})
check(r.value == "-14" and r.sign == "negative" and
      r.interpretation:find("opposite", 1, true) ~= nil,
      "the local work bridge preserves negative work and its interpretation")
check(giac_calls == before_work_local and r.giac_calls == 0,
      "work_local never calls the backend")

r = nps.work_local({
    force = {
        x = "2.50", y = "0", rank = 2, frame = "lab", unit = "N",
        precision = measured_three,
    },
    displacement = {
        x = "3.40", y = "0", rank = 2, frame = "lab", unit = "m",
        precision = measured_three,
    },
    force_profile = "constant",
})
check(r.value == "8.50" and r.exact_value == "8.5" and r.precision.kind == "measured" and
      r.precision.significant_digits == 3,
      "the work bridge separates measured reporting from its exact value")

r = nps.work_local({
    force = {
        x = "3", y = "4", rank = 2, frame = "lab", unit = "m",
        precision = exact_precision,
    },
    displacement = work_input.displacement,
    force_profile = "constant",
})
check(r.outcome == "dimension mismatch" and r.result == nil,
      "the work bridge preserves dimension mismatch")
r = nps.work_local({
    force = work_input.force,
    displacement = {
        x = "2", y = "1", rank = 2, frame = "ground", unit = "m",
        precision = exact_precision,
    },
    force_profile = "constant",
})
check(r.outcome == "frame mismatch" and r.result == nil,
      "the work bridge preserves frame mismatch")
r = nps.work_local({
    force = work_input.force,
    displacement = {
        x = "2", y = "1", z = "0", rank = 3, frame = "lab", unit = "m",
        precision = exact_precision,
    },
    force_profile = "constant",
})
check(r.outcome == "rank mismatch" and r.result == nil,
      "the work bridge preserves rank mismatch")
r = nps.work_local({
    force = { x = "3", y = "4", rank = 2, frame = "lab", unit = "N" },
    displacement = work_input.displacement,
    force_profile = "constant",
})
check(r.outcome == "invalid problem" and r.detail:find("precision", 1, true) ~= nil and
      #r.steps == 0,
      "the work bridge refuses missing typed precision metadata")
script("Error: Bad Argument Value")
r = nps.work(work_input)
check(r.outcome == "solved" and r.result ~= nil and r.status == "solved but unchecked" and
      r.giac_calls == 1,
      "a work backend failure costs the cross-check, not the local answer")
script("11")
r = nps.work(work_input)
check(r.outcome == "verification failed" and r.result == nil,
      "a disagreeing work backend withholds the local answer")
r = nps.work_local({
    force = work_input.force,
    displacement = work_input.displacement,
    force_profile = "unspecified",
})
check(r.outcome == "clarification required" and r.result == nil,
      "the work bridge preserves a missing applicability clarification")
r = nps.work_local({
    force = work_input.force,
    displacement = work_input.displacement,
    force_profile = "variable",
})
check(r.outcome == "law not applicable" and r.result == nil,
      "the work bridge preserves a variable-force applicability refusal")

forces_input = {
    body = "block",
    support = "table",
    mass = "2 kg",
    gravity = "10 m/s^2",
    surface = "horizontal",
    applied = "12 N",
    friction = "kinetic",
    friction_coefficient = "0.25",
    motion = "up the axis",
    equilibrium = false,
    unknown = "acceleration",
}
before_forces = giac_calls
r = nps.forces(forces_input)
check(r.solved == true and r.outcome == "solved" and r.status == "solved and verified",
      "the forces bridge returns a verified typed solution")
check(r.value == "3.5" and r.exact_value == "3.5" and r.unit == "m/s^2" and
      r.result == "acceleration = 3.5 m/s^2" and r.unknown == "acceleration",
      "the forces bridge reports the acceleration with its SI unit and exact value")
check(giac_calls == before_forces and r.giac_calls == 0,
      "the forces bridge answers without the backend")
check(type(r.inventory) == "table" and #r.inventory == 4 and
      r.along_equation ~= nil and r.across_equation ~= nil,
      "the forces bridge emits the whole force inventory and both axis equations")
by_kind = {}
for _, entry in ipairs(r.inventory) do by_kind[entry.kind] = entry end
check(by_kind.weight ~= nil and by_kind.weight.magnitude == "20 N" and
      by_kind.normal ~= nil and by_kind.normal.across == "20 N" and
      by_kind.applied ~= nil and by_kind.applied.along == "12 N" and
      by_kind.friction ~= nil and by_kind.friction.along == "-5 N",
      "each inventory entry carries the components #158 draws its labels from")
check(by_kind.weight.known == true and by_kind.normal.known == true and
      by_kind.applied.known == true and by_kind.friction.known == true,
      "every force an acceleration request supplies is published as known")
check(type(r.pairs) == "table" and #r.pairs == 2 and r.pairs[1].on_body == "block" and
      r.pairs[1].reaction_on == "table" and r.pairs[2].on_body == "block" and
      r.pairs[2].reaction_on == "the Earth",
      "the third-law pair travels beside the inventory rather than inside it")
r = nps.forces({
    body = "block",
    support = "table",
    mass = "2 kg",
    gravity = "10 m/s^2",
    surface = "horizontal",
    acceleration = "1 m/s^2",
    equilibrium = false,
    unknown = "applied force",
})
check(r.solved == true and r.result == "applied force = 2 N",
      "the forces bridge solves for the applied force")
solved_by_kind = {}
for _, entry in ipairs(r.inventory) do solved_by_kind[entry.kind] = entry end
check(#r.inventory == 3 and solved_by_kind.applied ~= nil and
      solved_by_kind.applied.along == "2 N" and solved_by_kind.applied.magnitude == "2 N",
      "the applied force being solved for reaches the bridge inventory with its components")
check(solved_by_kind.applied.known == false and solved_by_kind.weight.known == true and
      solved_by_kind.normal.known == true,
      "the bridge marks the solved force unknown and the supplied ones known")

r = nps.forces(forces_input)
forces_rules = {}
for _, s2 in ipairs(r.steps) do if s2.rule then forces_rules[s2.rule] = true end end
check(forces_rules["physics.forces.weight"] and forces_rules["physics.forces.normal-force"] and
      forces_rules["physics.forces.check-residual"],
      "the forces bridge retains the weight, normal force and residual check steps")

r = nps.forces({
    body = "block", support = "table", mass = "2 kg", gravity = "10 m/s^2",
    surface = "horizontal", applied = "12 N", friction = "kinetic",
    friction_coefficient = "0.25", equilibrium = false, unknown = "acceleration",
})
check(r.outcome == "motion sense undeclared" and r.result == nil,
      "the forces bridge preserves an undeclared motion sense refusal")
r = nps.forces({
    body = "block", support = "table", mass = "2 m", gravity = "10 m/s^2",
    surface = "horizontal", unknown = "normal force",
})
check(r.outcome == "dimension mismatch" and r.result == nil,
      "the forces bridge preserves a dimension mismatch")
r = nps.forces({
    body = "block", support = "ramp", mass = "2 kg", gravity = "10 m/s^2",
    surface = "incline", incline_sin = "0.6", incline_cos = "0.7",
    unknown = "normal force",
})
check(r.outcome == "incline angle not exact" and r.result == nil,
      "the forces bridge preserves an inexact incline angle refusal")
r = nps.forces({
    body = "block", support = "table", mass = "2 kg", gravity = "10 m/s^2",
    surface = "horizontal", friction = "sticky", unknown = "normal force",
})
check(r.outcome == "invalid problem" and #r.steps == 0,
      "the forces bridge refuses an unknown friction model before solving")

script("sqrt(3)/2", "0", "5*sqrt(3)", "0", "1/2", "0", "5", "0")
r = nps.magnitude_angle_to_components({
    magnitude = "10", angle = "30", rank = 2, frame = "lab", unit = "m/s",
    precision = exact_precision, angle_unit = "degrees",
})
check(r.outcome == "solved" and r.has_components == true and r.has_polar == false and
      r.components.y == "5" and r.components.x:find("sqrt", 1, true) ~= nil,
      "the component bridge returns exact symbolic Cartesian components")
check(r.components.rank == 2 and r.components.frame == "lab" and r.components.unit == "m/s" and
      r.components.precision.kind == "exact" and r.precision.kind == "exact" and
      r.giac_calls == 8,
      "component conversion retains all typed metadata and backend cost")

script("1/2", "0", "5", "0", "5.0", "sqrt(3)/2", "0", "5*sqrt(3)", "0", "8.66")
r = nps.magnitude_angle_to_components({
    magnitude = "10.0", angle = "60", rank = 2, frame = "lab", unit = "m",
    precision = measured_three, angle_unit = "degrees",
})
check(r.outcome == "solved" and r.components.x == "5.0" and r.components.y == "8.66" and
      r.components.precision.kind == "measured" and
      r.components.precision.significant_digits == 3 and
      r.precision.kind == "measured" and r.precision.significant_digits == 3,
      "the component bridge preserves explicitly approximate component forms")

script("0", "atan2(-4,-3)", "0", "atan2(-4,-3)*180/pi", "0")
r = nps.components_to_magnitude_angle({
    x = "-3", y = "-4", rank = 2, frame = "ground", unit = "m",
    precision = exact_precision, angle_unit = "degrees",
})
check(r.outcome == "solved" and r.has_polar == true and r.polar.magnitude == "5" and
      r.polar.angle:find("atan2", 1, true) ~= nil and
      r.polar.angle:find("-4", 1, true) ~= nil,
      "inverse component conversion preserves a negative-quadrant atan2 direction")
check(r.polar.rank == 2 and r.polar.frame == "ground" and r.polar.unit == "m" and
      r.polar.angle_unit == "degrees" and r.precision.kind == "exact" and r.giac_calls == 5,
      "inverse component conversion retains rank, frame, units and angle units")

r = nps.components_to_magnitude_angle({
    x = "1", y = "2", z = "3", rank = 3, frame = "lab", unit = "m",
    precision = exact_precision, angle_unit = "radians",
})
check(r.outcome == "invalid input" and r.has_polar == false and r.giac_calls == 0,
      "the inverse component bridge preserves the family rank refusal")
r = nps.components_to_magnitude_angle({
    x = "x^^2", y = "1", rank = 2, frame = "lab", unit = "m",
    precision = exact_precision, angle_unit = "radians",
})
check(r.outcome == "invalid input" and r.detail:find("character", 1, true) ~= nil and
      r.giac_calls == 0,
      "the inverse component bridge rejects malformed component expressions before Giac")
r = nps.components_to_magnitude_angle({
    x = "1", y = "2", z = "3", rank = 2, frame = "lab", unit = "m",
    precision = exact_precision, angle_unit = "radians",
})
check(r.outcome == "invalid input" and r.detail:find("must not contain z", 1, true) ~= nil and
      r.giac_calls == 0,
      "a rank 2 vector carrying z is refused by name")
-- Both faults at once, which says which phase ran: the reads all finish before any parse, so the
-- field that should not be there is named rather than the one that will not parse.
r = nps.components_to_magnitude_angle({
    x = "x^^2", y = "2", z = "3", rank = 2, frame = "lab", unit = "m",
    precision = exact_precision, angle_unit = "radians",
})
check(r.outcome == "invalid input" and r.detail:find("must not contain z", 1, true) ~= nil,
      "and it is refused before a malformed component is parsed: " .. tostring(r.detail))
r = nps.magnitude_angle_to_components({
    magnitude = "10", angle = "30", rank = 2, frame = "lab", unit = "m",
    precision = exact_precision,
})
check(r.outcome == "invalid input" and r.detail:find("angle_unit", 1, true) ~= nil,
      "the component bridge requires an explicit angle unit")
local invalid_ranks = {
    { value = 2.5, detail = "rank must be an integer" },
    { value = 0 / 0, detail = "rank is outside the supported range" },
    { value = 1 / 0, detail = "rank is outside the supported range" },
    { value = -1 / 0, detail = "rank is outside the supported range" },
    { value = -1, detail = "rank is outside the supported range" },
    { value = 65536, detail = "rank is outside the supported range" },
    { value = "2", detail = "rank must be an integer" },
    { value = 4, detail = "rank must be 2 or 3" },
}
for _, invalid_rank in ipairs(invalid_ranks) do
    r = nps.magnitude_angle_to_components({
        magnitude = "10", angle = "30", rank = invalid_rank.value, frame = "lab", unit = "m",
        precision = exact_precision, angle_unit = "degrees",
    })
    check(r.outcome == "invalid input" and
          r.detail:find(invalid_rank.detail, 1, true) ~= nil,
          "the component bridge rejects invalid rank metadata")
end
r = nps.magnitude_angle_to_components({
    magnitude = "10", angle = "30", rank = 2, frame = "lab", unit = "m",
    precision = { kind = "measured", significant_digits = 2.5 }, angle_unit = "degrees",
})
check(r.outcome == "invalid input" and
      r.detail:find("significant_digits must be an integer", 1, true) ~= nil,
      "the component bridge rejects fractional precision metadata")
script("Error: Bad Argument Value")
r = nps.magnitude_angle_to_components({
    magnitude = "10", angle = "30", rank = 2, frame = "lab", unit = "m",
    precision = exact_precision, angle_unit = "degrees",
})
check(r.outcome == "backend failure" and r.has_components == false and r.giac_calls == 1,
      "a component backend failure exposes no partial vector")
luagiac = nil
r = nps.work(work_input)
check(r.outcome == "solved" and r.value == "10" and r.giac_calls == 0,
      "work retains its local result when no cross-check backend is available")
r = nps.magnitude_angle_to_components({
    magnitude = "10", angle = "30", rank = 2, frame = "lab", unit = "m",
    precision = exact_precision, angle_unit = "degrees",
})
check(r.outcome == "backend failure" and r.has_components == false,
      "an unavailable component backend is a typed failure")
luagiac = { caseval = function(command)
    giac_calls = giac_calls + 1
    local reply = table.remove(replies, 1)
    if type(reply) == "function" then return reply(command) end
    return reply or "0"
end }

script("[v0+a*t]", "0", "[[17]]", "0")
r = nps.kinematics("find v; v0 = 18 km/h; a = 3 m/s^2; t = 4 s")
check(r.result == "v = 17 m/s", "a quantity given in km/h converts before the equation is used")
check(r.has_result == true, "native kinematics publishes has_result")

r = nps.kinematics("v0 = 5 m/s; a = 3 m/s^2")
check(r.outcome == "invalid input", "a problem with no unknown is refused")
check(type(r.steps) == "table" and #r.steps == 0, "with an empty steps table")
check(r.answer_only == false and r.result == nil, "invalid kinematics is not answer-only success")

local quadratic_problem = "find t; x = 4 m; v0 = 4 m/s; a = -2 m/s^2"
script("[[2]]")
r = nps.kinematics(quadratic_problem)
check(r.outcome == "no applicable equation" and r.status == "unsupported" and
      r.answer_only == true,
      "a locally refused one-root kinematics equation can return a Giac answer")
check(r.result == "t = 2 s" and r.value == "2" and r.unit == "s" and r.giac_tag == "exact",
      "the answer-only kinematics result carries its quantity, unit and Giac tag")
check(r.has_result == true, "backend-only kinematics publishes has_result")
check(type(r.steps) == "table" and #r.steps == 0 and r.step_count == 0,
      "answer-only kinematics carries no derivation")
check(r.detail:find("fully specified and dimensionally valid", 1, true) ~= nil,
      "and records why that exact equation was offered")

for _, sample in ipairs({
    {"3*x^2-12=0", "[-2,2]", true},
    {"x^2=4", "[[-2,2]]", true},
    {"x^2=4", "[-sqrt(4),sqrt(4)]", true},
    {"4*x^2=1", "[-sqrt(1/4),sqrt(1/4)]", true},
    {"4*x^2=1", "[-2/4,2/4]", true},
    {"x^2=4", "[-2,2,2]", true},
    {"x^2=0", "[0,0]", true},
    {"x^2=-4", "[]", true},
    {"x^2=4", "[2]", false},
    {"x^2=4", "[2,2]", false},
    {"x^2=4", "[-2,2,3]", false},
    {"x^2=4", "[-3,3]", false},
    {"x^2=4", "[-sqrt(9),sqrt(9)]", false},
    {"x^2=4", "[]", false},
    {"x^2=-4", "[2]", false},
}) do
    script(sample[2])
    local checked = nps.solve(sample[1], "x")
    check(checked.giac_tag == "exact" and checked.agrees == sample[3] and
          checked.status == (sample[3] and "solved and verified" or "verification failed"),
          "the complete finite root set is compared: " .. sample[1] .. " with " .. sample[2])
end
for _, reply in ipairs({"[-2.0,2.0]", "[-sqrt(2),sqrt(2)]", "[x]", "[i,-i]",
                         "[2,]", "[[2],[-2]]"}) do
    script(reply)
    local checked = nps.solve("x^2=4", "x")
    check(checked.agrees == nil and checked.status == "solved but unchecked",
          "an unqualified root set cannot confirm or refute the derivation: " .. reply)
end
script(function()
    nps.test_escape_pressed(true)
    return "[-2,2]"
end)
local cancelled_set = nps.solve("x^2=4", "x")
nps.test_escape_pressed(false)
check(cancelled_set.status == "cancelled" and cancelled_set.agrees == nil,
      "cancellation between the backend reply and set comparison prevents confirmation")
script("[-2,2]")
check(nps.solve("x^2=4", "x").agrees == true, "the next finite-set comparison recovers after cancellation")
script("[" .. string.rep("2,", 2050) .. "2]")
local limited_set = nps.solve("x^2=4", "x")
check(limited_set.status == "solved but unchecked" and limited_set.agrees == nil and
      limited_set.result ~= nil,
      "an oversized solution reply cannot report a partial or confirmed root set")
script("[-2,2]")
r = nps.kinematics(quadratic_problem)
check(r.answer_only == false and r.result == nil and r.giac_tag == "exact" and
      r.giac_detail:find("exactly one", 1, true) ~= nil,
      "multiple kinematics roots remain a refusal rather than selecting one")
check(r.has_result == false, "multiple-root kinematics explicitly has no result")

script("Error: Bad Argument Value")
r = nps.kinematics(quadratic_problem)
check(r.answer_only == false and r.result == nil and r.giac_tag == "backend error",
      "a kinematics backend error remains a refusal")
check(r.has_result == false, "backend-error kinematics explicitly has no result")

local before_dimension_failure = giac_calls
r = nps.kinematics("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 m")
check(r.outcome == "dimension mismatch" and r.status == "invalid input" and
      r.answer_only == false and r.result == nil,
      "dimensionally invalid kinematics cannot become answer-only success")
check(r.has_result == false, "dimensionally invalid kinematics explicitly has no result")
check(giac_calls == before_dimension_failure,
      "dimensionally invalid kinematics never reaches Giac")

script("[v0-a*t]", "1")
r = nps.kinematics("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s")
check(r.outcome == "verification failed" and r.status == "verification failed" and
      r.answer_only == false and r.result == nil,
      "a failed kinematics verification cannot become answer-only success")
check(r.has_result == false, "failed kinematics verification explicitly has no result")

-- The local entry points never reach Giac.
giac_calls = 0
local raw_derivative = "  x + 2*x  "
r = nps.differentiate_local(raw_derivative, "x")
check(giac_calls == 0 and r.giac_tag == nil, "differentiate_local makes no Giac call")
check(r.original_expression == raw_derivative and
      r.normalized_expression == "(x + (2 * x))",
      "the derivative bridge preserves source bytes and publishes its normalized AST spelling")
local raw_equation = "  3*x = 9  "
r = nps.solve_local(raw_equation, "x")
check(giac_calls == 0 and r.solved == true, "solve_local makes no Giac call")
check(r.original_expression == raw_equation and
      r.normalized_expression == "((3 * x) = 9)",
      "the equation bridge preserves source bytes and publishes its normalized AST spelling")
local raw_integral = "  x*2  "
r = nps.integrate_local(raw_integral, "x")
check(giac_calls == 0 and r.solved == true, "integrate_local makes no Giac call")
check(r.original_expression == raw_integral and r.normalized_expression == "(x * 2)",
      "the integral bridge preserves source bytes and prints the retained normalized AST")
r = nps.kinematics_local("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s")
check(giac_calls == 0 and r.result == "v = 17 m/s", "kinematics_local makes no Giac call")
check(r.has_result == true, "local kinematics publishes has_result")
check(r.rearranged == nil, "and records no rearrangement it had no way to check")
r = nps.kinematics_local("find t; v = 6 m/s; v0 = 5 m/s; a = 0 m/s^2")
check(giac_calls == 0 and r.outcome == "no solution" and r.solved == false and
      r.has_result == true and r.result == nil and r.status == "solved and verified" and
      type(r.steps) == "table" and #r.steps > 0,
      "verified inconsistent kinematics publishes an empty result set")
r = nps.integrate_local("x*sin(x)", "x")
check(giac_calls == 0 and r.answer_only == false and r.result == nil,
      "a local-only refusal cannot become answer-only")
r = nps.kinematics_local(quadratic_problem)
check(giac_calls == 0 and r.answer_only == false and r.result == nil,
      "a local-only quadratic kinematics refusal cannot become answer-only")
check(r.has_result == false, "local kinematics refusal explicitly has no result")

nps.test_escape_pressed(true)
giac_calls = 0
r = nps.solve("x = 4", "x")
check(r.outcome == "cancelled" and r.result == nil, "the solve bridge returns cancellation")
check(type(r.steps) == "table" and #r.steps == 0, "with no solve steps")
check(r.answer_only == false, "a cancelled solve is not answer-only")
r = nps.differentiate("x^2", "x")
check(r.outcome == "cancelled" and r.result == nil, "the derivative bridge returns cancellation")
check(type(r.steps) == "table" and #r.steps == 0, "with no derivative steps")
check(r.answer_only == false, "a cancelled derivative is not answer-only")
r = nps.integrate("sin(2x)", "x")
check(r.outcome == "cancelled" and r.result == nil, "the integral bridge returns cancellation")
check(type(r.steps) == "table" and #r.steps == 0, "with no integral steps")
check(r.answer_only == false, "a cancelled integral is not answer-only")
check(giac_calls == 0, "a cancelled integral outcome does not reach Giac")
r = nps.kinematics("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s")
check(r.outcome == "cancelled" and r.result == nil, "the kinematics bridge returns cancellation")
check(type(r.steps) == "table" and #r.steps == 0, "with no kinematics steps")
check(r.answer_only == false, "cancelled kinematics is not answer-only")
check(r.has_result == false, "cancelled kinematics explicitly has no result")
r = nps.catch_up(catch_up_input)
check(r.outcome == "cancelled" and r.event_time == nil and #r.steps == 0 and
      r.answer_only == false,
      "the catch-up bridge returns cancellation without a partial event or derivation")
r = nps.vector_addition("(1, 2) m", "(3, 4) m")
check(r.outcome == "cancelled" and r.result == nil and #r.steps == 0,
      "the vector bridge returns cancellation without partial steps")
r = nps.relative_motion(relative_motion_input)
check(r.outcome == "cancelled" and r.result == nil and #r.steps == 0,
      "the relative-motion bridge returns cancellation without partial steps")
r = nps.unit_conversion("3 cm^2", "mm^2")
check(r.outcome == "cancelled" and r.result == nil and #r.steps == 0,
      "the unit conversion bridge returns cancellation without partial steps")
r = nps.density("mass", "density", "4 kg/m^3", "volume", "3 m^3")
check(r.outcome == "cancelled" and r.result == nil and #r.steps == 0,
      "the density bridge returns cancellation without partial steps")
r = nps.work(work_input)
check(r.outcome == "cancelled" and r.result == nil and #r.steps == 0,
      "the work bridge returns cancellation without partial steps")
r = nps.magnitude_angle_to_components({
    magnitude = "10", angle = "30", rank = 2, frame = "lab", unit = "m",
    precision = exact_precision, angle_unit = "degrees",
})
check(r.outcome == "cancelled" and r.has_components == false and #r.steps == 0,
      "the component bridge returns cancellation without partial steps")
check(giac_calls == 0, "cancelled bridge calls never reach Giac")
nps.test_escape_pressed(false)

local wide = "x"
for _ = 1, 9 do wide = "(" .. wide .. "+" .. wide .. ")" end
giac_calls = 0
r = nps.differentiate(wide, "x")
check(r.outcome == "resource exceeded" and r.result == nil,
      "the derivative bridge returns a resource halt")
-- A nest of sums halts inside a composite rule, and STEP-025 keeps what was checked under it. The
-- assertion used to be that nothing survived, which was the engine's behaviour of the day rather
-- than the requirement: what the requirement forbids is the answer above, not the working below.
check(type(r.steps) == "table" and #r.steps > 0,
      "and the bridge carries the prefix the halt kept across to Lua")
local unfilled = 0
for _, step in ipairs(r.steps) do
    if step.kind == "transformation" and (step.after == nil or step.after == "") then
        unfilled = unfilled + 1
    end
end
check(unfilled == 0, "with every kept transformation showing what it produced")
check(r.answer_only == false, "a resource-halted derivative is not answer-only")
check(giac_calls == 0, "a resource-halted bridge call never reaches Giac")

giac_calls = 0
r = nps.integrate("0.1234567890123456789*x", "x")
check(r.outcome == "resource exceeded" and r.result == nil and r.answer_only == false,
      "the integral bridge returns a resource halt without an answer")
check(giac_calls == 0, "a resource-halted integral outcome does not reach Giac")

script("[[1]]")
giac_calls = 0
r = nps.solve("4611686018427387904x + 4611686018427387904x = 1", "x")
check(r.status == "resource limit reached" and r.result == nil and r.answer_only == false,
      "an arithmetic resource limit does not return an answer")
check(giac_calls == 0 and r.giac_calls == 0,
      "an arithmetic resource limit never reaches Giac, observed " .. giac_calls .. " calls")

-- Every way Giac can misbehave, each of which used to be a reset when it left our table exposed.
script("Error: Bad Argument Value")
r = nps.differentiate("x^2", "x")
check(r.giac_tag == "backend error" and r.agrees == nil and
      r.status == "dependency unavailable",
      "a Giac error is tagged and cannot leave a complete result")

local first_verifier_failures = {
    { reply = "Time limit exceeded", tag = "timeout", status = "solved but unchecked" },
    { reply = "Not enough memory", tag = "resource failure", status = "solved but unchecked" },
    { reply = "Unable to differentiate", tag = "unevaluated", status = "solved but unchecked" },
    { reply = "this ( is not ) an expression", tag = "malformed result", status = "solved but unchecked" },
}
for _, failure in ipairs(first_verifier_failures) do
    script(failure.reply)
    r = nps.differentiate("x^2", "x")
    check(r.giac_tag == failure.tag and r.status == failure.status and r.agrees == nil,
          "an unusable first verifier reply becomes " .. failure.status .. ": " .. failure.tag)
    check(r.result ~= nil and r.answer_only == false,
          "and the derivative we computed ourselves is still the answer: " .. failure.tag)
end

local second_verifier_failures = {
    { reply = "Time limit exceeded", tag = "timeout", status = "solved but unchecked" },
    { reply = "Not enough memory", tag = "resource failure", status = "solved but unchecked" },
    { reply = "this ( is not ) an expression", tag = "malformed result", status = "solved but unchecked" },
    { reply = "0.0", tag = "approximate", status = "solved but unchecked" },
}
for _, failure in ipairs(second_verifier_failures) do
    script("2*x", failure.reply)
    r = nps.differentiate("x^2", "x")
    check(r.giac_tag == "exact" and r.giac_compare_tag == failure.tag and
          r.status == failure.status and r.agrees == nil,
          "an unusable comparison reply becomes " .. failure.status .. ": " .. failure.tag)
end

-- The control for the four rows above. Without it they pass for a differentiate whose status is
-- never touched by the cross-check at all.
script("2*x", "0")
r = nps.differentiate("x^2", "x")
check(r.status == "solved and verified" and r.agrees == true,
      "while a comparison that does finish and agrees still says so")

script("Error: Bad Argument Value")
r = nps.differentiate("x^2", "x")
check(r.status == "dependency unavailable",
      "and a backend that is broken rather than out of room keeps its own status")

script("2.0*x", "0")
r = nps.differentiate("x^2", "x")
check(r.giac_tag == "approximate" and r.giac_compare_tag == "exact" and
      r.status == "solved but unchecked" and r.agrees == nil,
      "an approximate backend answer cannot pass an exact symbolic cross-check")

-- The learner pressed escape while Giac was working. Giac reports that in the result string, and a
-- stop the learner asked for is not the same fact as a check that merely could not be used.
script("GIAC_ERROR: Stopped by user interruption.")
r = nps.differentiate("x^2", "x")
check(r.giac_tag == "cancelled" and r.status == "cancelled" and r.agrees == nil,
      "a cancelled first verifier reply is recorded as a cancellation")
check(r.result ~= nil and r.answer_only == false,
      "and the derivative computed before the stop is still the answer")

script("2*x", "GIAC_ERROR: Stopped by user interruption.")
r = nps.differentiate("x^2", "x")
check(r.giac_tag == "exact" and r.giac_compare_tag == "cancelled" and
      r.status == "cancelled" and r.agrees == nil,
      "a cancelled comparison reply is recorded as a cancellation")

script("[[4]]", "GIAC_ERROR: Stopped by user interruption.")
r = nps.solve("2x + 5 = 13", "x")
check(r.result == "4" and r.status == "cancelled" and r.giac_compare_tag == "cancelled",
      "a cancelled supplementary equation check keeps the candidate and names the stop")

script("GIAC_ERROR: Stopped by user interruption or stack overflow.")
r = nps.differentiate("x^2", "x")
check(r.giac_tag == "resource failure" and r.status == "solved but unchecked",
      "while the spelling giac cannot separate from a stack overflow keeps its resource reading")

-- The same spelling, with the learner holding escape while giac was working. The bridge lends the
-- backend the keypad poll its own budget uses, so the reading giac's text cannot settle is settled.
script(function()
    nps.test_escape_pressed(true)
    return "GIAC_ERROR: Stopped by user interruption or stack overflow."
end)
r = nps.differentiate("x^2", "x")
nps.test_escape_pressed(false)
check(r.giac_tag == "cancelled" and r.status == "cancelled" and r.agrees == nil,
      "and a stop the keypad confirms reads that spelling as the cancellation it was")
check(r.result ~= nil and r.answer_only == false,
      "and the derivative computed before that stop is still the answer")

script("Error: Bad Argument Value")
r = nps.integrate("x*sin(x)", "x")
check(r.giac_tag == "backend error" and r.result == nil and r.answer_only == false,
      "a backend error cannot turn a core refusal into answer-only success")

script(function() error("caseval blew up") end)
r = nps.differentiate("x^2", "x")
check(r.giac_tag == "backend error", "a raise inside caseval is a backend error")
check(r.giac_detail ~= nil and r.giac_detail:find("blew up", 1, true) ~= nil,
      "and its message is carried")

script(function() return 42 end)
r = nps.differentiate("x^2", "x")
check(r.giac_tag == "backend error", "a non-string reply is a backend error")

script("2*x", "x")
r = nps.differentiate("x^2", "x")
check(r.agrees == false and r.giac_compare_tag == "exact" and
      r.status == "verification failed",
      "a non-zero exact difference is a failed verification")

script("x^2", "Error: Bad Argument Value")
r = nps.integrate("x^2", "x")
check(r.result ~= nil and r.status == "dependency unavailable" and
      r.giac_compare_tag == "backend error",
      "a failed supplementary integral cross-check retains the candidate but not complete status")

script("[[4]]", "Time limit exceeded")
r = nps.solve("2x + 5 = 13", "x")
check(r.result == "4" and r.status == "solved but unchecked" and
      r.giac_compare_tag == "timeout",
      "a timed-out supplementary equation check retains the candidate and says it went unchecked")

script("[v0+a*t]", "0", "[[17]]", "Error: Bad Argument Value")
r = nps.kinematics("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s")
check(r.result == "v = 17 m/s" and r.status == "dependency unavailable" and
      r.giac_compare_tag == "backend error",
      "a failed supplementary kinematics check retains the candidate with dependency status")
check(r.has_result == true, "kinematics retains has_result after a supplementary backend error")

script("[v0+a*t]", "0", "[[17]]", "Time limit exceeded")
r = nps.kinematics("find v; v0 = 5 m/s; a = 3 m/s^2; t = 4 s")
check(r.result == "v = 17 m/s" and r.status == "solved but unchecked" and
      r.giac_compare_tag == "timeout",
      "and a timed-out one says the same as every other engine does")
check(r.has_result == true, "kinematics retains has_result after a supplementary timeout")

-- Complete backend root sets cross-check the locally verified quadratic cases.
script("[-2,2]")
r = nps.solve("x^2 = 4", "x")
check(r.solved == true and r.result == "2 and (-2)" and
      r.giac_tag == "exact" and type(r.giac) == "string" and r.answer_only == false and
      r.status == "solved and verified" and r.agrees == true,
      "both locally verified roots agree with the complete backend solution set")
local cases = {}
for _, step in ipairs(r.steps) do
    if step.kind == "branch" then cases[#cases + 1] = step end
end
check(#cases == 2 and cases[1].case == "(x = 2)" and cases[2].case == "(x = (-2))",
      "both cases cross the bridge carrying the condition each one stands for")
check(cases[1].resolution == "solved" and cases[2].resolution == "solved" and
      cases[1].settled_by == "substitution into the original equation" and
      cases[2].settled_by == "substitution into the original equation",
      "and each says how it was settled rather than only that it was")
local completeness = false
for _, step in ipairs(r.steps) do
    if step.kind == "check" and step.goal == "Check that no case is missing" then
        completeness = true
    end
end
check(completeness, "the split's own completeness check is one of the steps a reader sees")

-- An empty real solution set is an answer this rule reached and checked, not a refusal, so the case
-- it ruled out has to say rejected rather than come back looking like the two above.
script("[]")
r = nps.solve("x^2 = -4", "x")
check(r.giac == "[]" and r.agrees == true,
      "the exact empty backend solution set remains visible across the bridge")
check(r.outcome == "no real solution" and r.solved == false and r.result == nil and
      r.answer_only == false and r.has_result == true,
      "an equation with no real root reports the empty set rather than a refusal")
local rejected = nil
for _, step in ipairs(r.steps) do
    if step.kind == "branch" then rejected = step end
end
check(rejected ~= nil and rejected.resolution == "rejected" and
      rejected.settled_by == "no real number squares to a negative",
      "the single case is marked ruled out with the reason it was ruled out")

script("[]")
r = nps.solve("x^2 = 5", "x")
check(r.outcome == "outside the declared envelope" and r.solved == false and #r.steps == 0 and
      r.detail:find("whose square root is not exact", 1, true) ~= nil,
      "a square with no exact root is refused by name rather than answered in decimals")

script("[-4,1]")
r = nps.solve("x^2 + 3x = 4", "x")
check(r.outcome == "not linear in the unknown" and #r.steps == 0 and r.has_result == true and
      r.answer_only == true and r.status == "unsupported" and r.result == "[(-4), 1]",
      "a refused native quadratic retains every backend root as an explicitly separate answer")

-- The linear rule states two answers that have no value to print, the same way the square-root rule
-- states an empty real set. All three are verified derivations, and a viewer that reads a missing
-- result as no answer would put NO RESULT over every one of them.
script("[]")
r = nps.solve("x + 1 = x + 2", "x")
check(r.outcome == "no solution" and r.solved == false and r.result == nil and
      r.has_result == true and r.status == "solved and verified",
      "an equation no value satisfies is a checked answer rather than an absent one")
script("[]")
r = nps.solve("x + 1 = x + 1", "x")
check(r.outcome == "true for every value" and r.solved == false and r.result == nil and
      r.has_result == true and r.status == "solved and verified",
      "and so is one that every value satisfies")

script("[[-3999999999/4000000000]]", "0")
r = nps.solve("4000000000x + 3999999999 = 0", "x")
local large_exact_result = "(-3999999999 * (4000000000^(-1)))"
check(r.solved == true and r.status == "solved and verified" and
      r.result == large_exact_result and r.answer_only == false and r.agrees == true,
      "large exact arithmetic returns the verified local solution agreed by Giac")
local exact_substitution_passed = false
for _, step in ipairs(r.steps) do
    if step.kind == "check" and
       step.action == "substitute the solution into the equation as it was typed" and
       step.before == "both sides equal" and step.after == "both sides are equal" and
       step.verified == true and step.failed == false and
       type(step.checks) == "string" and step.checks:find("substitution: passed", 1, true) then
        exact_substitution_passed = true
    end
end
check(exact_substitution_passed,
      "the large exact solution carries an independently passed substitution record")

script("[[0]]", "1")
r = nps.solve("4000000000x + 3999999999 = 0", "x")
check(r.solved == true and r.status == "verification failed" and
      r.result == large_exact_result and r.answer_only == false,
      "an incorrect backend answer cannot replace the local candidate or remain complete")
check(r.giac == "0" and r.agrees == false,
      "an actually incorrect backend answer is recorded as a disagreement")

luagiac = nil
r = nps.differentiate("x^2", "x")
check(r.giac_tag == "unavailable" and r.status == "dependency unavailable",
      "no luagiac means the required cross-check is dependency unavailable")
luagiac = { caseval = function() return "0" end }

-- Refusals at the edge, each returning nil and a message rather than raising.
local v, why = nps.differentiate("x^^2", "x")
check(v == nil and type(why) == "string" and why:find("character", 1, true) ~= nil,
      "a syntax error names its position")
v, why = nps.solve("", "x")
check(v == nil and type(why) == "string", "an empty input is refused with a message")
local oversized = string.rep("x", 4097)
r = nps.differentiate_local(oversized, "x")
check(type(r) == "table" and r.outcome == "resource exceeded" and
      r.status == "resource limit reached" and r.result == nil,
      "an input beyond the shared parser bound returns a typed resource refusal")
local expanded_input = "f(" .. string.rep("x,", 2000) .. "x)"
check(#expanded_input <= 4096, "the normalized-expansion fixture fits the input bound")
r = nps.differentiate_local(expanded_input, "x")
check(type(r) == "table" and r.outcome == "resource exceeded" and
      r.status == "resource limit reached" and r.normalized_expression == nil,
      "a normalized AST spelling beyond the same bound is refused instead of truncated")
v, why = nps.canonical("(((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((x")
check(v == nil, "an unbalanced deep input is refused")

local ok, err = pcall(nps.differentiate)
check(not ok and tostring(err):find("string expected", 1, true) ~= nil,
      "a missing argument raises Lua's own error")
ok, err = pcall(nps.solve, {})
check(not ok, "a table where a string was expected raises")

-- The collector must be running again after every return, including the early ones. Churned rather
-- than collected: collectgarbage("collect") restarts a stopped collector in LuaJIT, so a check that
-- calls it passes whatever the module left behind. Measured here, a stopped collector grows the heap
-- by 25 MB over this loop and a running one by 53 kb.
nps.differentiate("x^^2", "x")
local before = collectgarbage("count")
for i = 1, 200000 do local waste = { i, tostring(i), { i } } end
check(collectgarbage("count") - before <= 4096, "the collector runs after a parse refusal")

-- Arguments are read before the collector is stopped, which AGENTS.md asks for because a raise is a
-- longjmp on the device and skips GcPause's destructor, leaving the collector stopped for the rest of
-- the session. Asked at the moment of the read rather than after it: the metatable records the state
-- it can see and then raises, and the host cannot answer afterwards because LuaJIT unwinds through
-- C++ and runs the destructor on the way out.
do
    local table_arguments = {
        "catch_up", "relative_motion", "relative_motion_local", "work", "work_local",
        "magnitude_angle_to_components", "components_to_magnitude_angle",
    }
    for _, name in ipairs(table_arguments) do
        local running = nil
        local probe = setmetatable({}, {
            __index = function()
                running = collectgarbage("isrunning")
                error("the field read that this entry point starts with")
            end,
        })
        pcall(nps[name], probe)
        check(running == true,
              name .. " reads its table argument before it stops the collector, saw " ..
                  tostring(running))
    end
end

-- Ordinary use in a loop, with the kinds of input the fuzz generator favours.
local inputs = {
    "sin(x)*cos(x)", "x^2 + 3x - 5", "1/(x + 1)", "exp(x^2)", "ln(x)/x", "sqrt(x)*x",
    "tan(x)^2", "2x + 5 = 13", "x/3 - 1 = 4", "x = 1 + x", "9223372036854775807*x",
    "-(-(-(x)))", "f(x, y, z)", "nosuchfn(x)", "x^(1/2)", "3.5x + 1.25 = 8",
}
for _, s in ipairs(inputs) do
    script("0", "0")
    local d = nps.differentiate(s, "x")
    script("[[1]]", "0")
    local q = nps.solve(s, "x")
    script("0", "0")
    local i = nps.integrate(s, "x")
    check(d == nil or type(d.steps) == "table", "differentiate returns steps for " .. s)
    check(q == nil or type(q.steps) == "table", "solve returns steps for " .. s)
    check(i == nil or type(i.steps) == "table", "integrate returns steps for " .. s)
end

local stale_global_surface = {
    caseval = function() return "stale global caseval" end,
    solve = function() return "stale global solve" end,
    sentinel = "stale global",
}
local stale_loaded_surface = {
    caseval = function() return "stale loaded caseval" end,
    solve = function() return "stale loaded solve" end,
    sentinel = "stale loaded",
}
_G.nps_nspire = stale_global_surface
package.loaded.nps_nspire = stale_loaded_surface

local failed_surface = nps.test_integrity_failure_surface()
check(failed_surface ~= stale_global_surface and failed_surface ~= stale_loaded_surface and
      _G.nps_nspire == failed_surface and package.loaded.nps_nspire == failed_surface,
      "an integrity failure replaces both previously registered module tables")
check(type(failed_surface) == "table" and
      failed_surface.integrity_status() == "mismatch" and
      type(failed_surface.capability_manifest) == "function",
      "an integrity failure registers its typed diagnostic surface")
for _, name in ipairs({ "caseval", "canonical", "giac", "solve", "solve_local", "differentiate",
                        "differentiate_local", "integrate", "integrate_local", "kinematics",
                        "kinematics_local", "catch_up", "unit_conversion", "density",
                        "vector_addition", "relative_motion", "relative_motion_local", "work",
                        "work_local", "magnitude_angle_to_components",
                        "components_to_magnitude_angle", "typed_check" }) do
    check(failed_surface[name] == nil,
          "the integrity-failed surface withholds " .. name)
end
check(failed_surface.sentinel == nil,
      "the integrity-failed surface drops stale non-API fields")
local failed_manifest = failed_surface.capability_manifest()
check(type(failed_manifest.installed_modules) == "table" and
      #failed_manifest.installed_modules == 0,
      "the integrity-failed manifest exposes no installed solver or content module")
check(failed_manifest.symbolic_backend.name == "Giac" and
      failed_manifest.symbolic_backend.available == false and
      failed_manifest.symbolic_backend.interface_id == "unavailable" and
      failed_manifest.symbolic_backend.deployment == "integrity-rejected",
      "the integrity-failed manifest marks the bundled backend unavailable")
local v4_file = assert(io.open("lua/nps_v4.lua", "rb"))
local v4_source = v4_file:read("*a")
v4_file:close()
check(not v4_source:find("math.evalStr", 1, true),
      "Ki V4 contains no alternate Nspire math-engine fallback")

do
    local function copy(value)
        if type(value) ~= "table" then return value end
        local copied = {}
        for key, field in pairs(value) do copied[key] = copy(field) end
        return copied
    end
    local vector = {
        x = "1", y = "2", rank = 2, frame = "ground", unit = "m/s",
        precision = {kind = "exact", significant_digits = 0},
    }
    local relative = {
        subject_name = string.rep("q", 3000), reference_name = "reference",
        subject_velocity = vector, reference_velocity = vector,
    }
    local body = {
        name = "runner", frame = "ground", position = "0 m", velocity = "1 m/s",
        start_time = "0 s", motion = "constant_velocity",
    }
    local cases = {
        {nps.relative_motion, {subject_name = relative.subject_name}, {"reference_name"}},
        {nps.relative_motion_local, relative, {"reference_name"}},
        {nps.relative_motion_local, relative, {"subject_velocity"}},
        {nps.relative_motion_local, relative, {"reference_velocity"}},
        {nps.catch_up, {first = body, second = body}, {"first"}},
        {nps.catch_up, {first = body, second = body}, {"second"}},
        {nps.catch_up, {first = body, second = body}, {"first", "frame"}},
        {nps.catch_up, {first = body, second = body}, {"first", "acceleration"}},
        {nps.work, {force = {rank = 2, frame = relative.subject_name}}, {"force", "unit"}},
        {nps.work_local, work_input, {"force"}},
        {nps.work_local, work_input, {"displacement"}},
        {nps.work_local, work_input, {"force_profile"}},
        {nps.magnitude_angle_to_components, {rank = 2, frame = relative.subject_name}, {"unit"}},
        {nps.components_to_magnitude_angle, {rank = 2, frame = relative.subject_name}, {"unit"}},
        {nps.components_to_magnitude_angle, vector, {"z"}},
    }
    for _, key in ipairs({"rank", "frame", "unit", "precision", "x", "y"}) do
        cases[#cases + 1] = {nps.relative_motion_local, relative, {"subject_velocity", key}}
    end
    for _, key in ipairs({"kind", "significant_digits"}) do
        cases[#cases + 1] = {
            nps.relative_motion_local, relative, {"subject_velocity", "precision", key},
        }
    end
    for _, case in ipairs(cases) do
        local expected = case[1](copy(case[2]))
        for _, inherited in ipairs({false, true}) do
            local supplied = copy(case[2])
            local parent = supplied
            for index = 1, #case[3] - 1 do parent = parent[case[3][index]] end
            local key = case[3][#case[3]]
            local original = parent[key]
            parent[key] = nil
            local calls = 0
            setmetatable(parent, {__index = inherited and {[key] = original} or function()
                calls = calls + 1
                error("request field executed")
            end})
            local ok, refused = pcall(case[1], supplied)
            if inherited then
                check(ok and type(refused) == "table" and refused.status == expected.status and
                      refused.outcome == expected.outcome and refused.result == expected.result,
                      table.concat(case[3], ".") .. " preserves inherited field behavior")
            else
                check(ok and calls == 1 and type(refused) == "table" and
                      refused.status == "invalid input" and refused.result == nil and
                      refused.detail:find("request field executed", 1, true),
                      table.concat(case[3], ".") .. " contains lookup errors as input refusals")
            end
        end
    end
    local optional_calls = 0
    local supplied = copy(relative)
    for _, velocity in ipairs({supplied.subject_velocity, supplied.reference_velocity}) do
        setmetatable(velocity, {__index = function()
            optional_calls = optional_calls + 1
            error("optional field executed")
        end})
    end
    local ok, solved = pcall(nps.relative_motion_local, supplied)
    check(ok and optional_calls == 1 and solved.status == "invalid input" and solved.result == nil,
          "optional planar z lookup errors are input refusals")
end

-- Appends to the file the unit run wrote, the way ui_smoke_v4.lua does, rather than opening a second
-- one. A second file is a second thing traceability can fail to find, and a file it cannot open
-- reads exactly like a file it opened and found nothing in, which would turn PLAT-009 from honestly
-- unmet into falsely met. One file has one absence and that absence is already loud.
local function writeEvidence()
    local path = os.getenv("NPS_EVIDENCE")
    if not path then return end

    local existing = io.open(path, "rb")
    if not existing then
        check(false, "luax evidence requires the completed unit evidence file")
        return
    end
    local before = existing:read("*a")
    existing:close()
    check(before:find("group\tadapter\n", 1, true) ~= nil,
          "luax evidence appends only after the unit evidence fixture")

    local rows = { "group\tluax" }
    for _, record in ipairs(evidence_records) do
        rows[#rows + 1] = table.concat({
            "evidence", record.requirement, record.passed and "pass" or "fail", "luax", record.what,
        }, "\t")
    end
    local appended = table.concat(rows, "\n") .. "\n"
    local output, openError = io.open(path, "ab")
    if not output then
        check(false, "luax evidence could not append: " .. tostring(openError))
        return
    end
    local wrote, writeError = output:write(appended)
    local closed, closeError = output:close()
    check(wrote ~= nil and closed ~= nil,
          "luax evidence append completes: " .. tostring(writeError or closeError or "ok"))
    if not wrote or not closed then return end

    -- Read back rather than trust the write, and check the prior bytes are still there, because an
    -- append that truncated would leave a file that still parses.
    local combined = io.open(path, "rb")
    if not combined then
        check(false, "luax evidence path cannot be reopened")
        return
    end
    local after = combined:read("*a")
    combined:close()
    local emitted = after:sub(#before + 1)
    check(after:sub(1, #before) == before and
          emitted:find("evidence\tPLAT-009\t", 1, true) ~= nil,
          "luax evidence preserves prior TSV and emits its own requirement links")
end

writeEvidence()

print(string.format("luax host: %d checks, %d failed", checks, failures))
os.exit(failures == 0 and 0 or 1)
