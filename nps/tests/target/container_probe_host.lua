local probe = assert(package.loadlib(arg[1], "luaopen_nps_container_probe"))()
local checks = 0
local function check(ok, message)
    checks = checks + 1
    assert(ok, message)
end
for kind = 1, 6 do
    local checksum, object_bytes, heap_bytes = probe.run(kind, 200, 199)
    check(checksum == 4422400 and object_bytes > 0, "native batch computes the complete command checksum")
    check(kind % 3 == 0 and heap_bytes >= 1280 or kind % 3 ~= 0 and heap_bytes == 0,
        "only the standard vector has separately allocated capacity")
end
for index, bounds in ipairs({{1, 6}, {1, 20000}, {0, 199}}) do
    for _, invalid in ipairs({false, {}, "invalid", -math.huge, math.huge, 0 / 0, bounds[1] - 1, bounds[2] + 1, bounds[1] + 0.5}) do
        local arguments = {1, 1, 0}
        arguments[index] = invalid
        check(not pcall(probe.run, unpack(arguments)), "raising arguments are refused before native container construction")
        check(probe.run(1, 1, 0) == 15744, "refused arguments leave the next workload usable")
    end
    local arguments = {1, 1, 0}
    arguments[index] = nil
    check(not pcall(probe.run, arguments[1], arguments[2], arguments[3]), "missing numeric arguments are refused")
end
check(probe.run(6, 20000, 199) == 442240000, "maximum supported batch preserves its exact checksum")
print("container bridge: " .. checks .. " checks passed")
