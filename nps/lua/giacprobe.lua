-- Minimal reproduction of the reset, with StepCAS removed from the picture.
--
-- Established: luagiac.caseval("1+1"), as the first Giac call in a fresh document, resets the
-- calculator. Reproduced three times. It is not memory (8192 kb still free), not a duplicate module
-- load, and not our C++ (800 ASan and UBSan rounds clean). Since the first caseval is the one that
-- runs luagiac_init (luabridge.cc:72-75), the fault is in initialisation.
--
-- What this run tests. luabridge.cc:39-40 is the FIRST thing luagiac_init does:
--
--   unsigned green=*(unsigned *) 0x90110b04;
--   unsigned red=*(unsigned *) 0x90110b0c;
--
-- Those are CX 1 LED registers, read with no is_cx2 branch, even though khicas branches on is_cx2
-- for other peripherals (k_csdk.c:1335 picks the contrast register that way) and Ndl reads the
-- CX II keypad at a different address entirely (on_key_pressed.c:27, 0x90140810).
--
-- So P1 and P2 read those two words directly, before luagiac is loaded and with no Giac code
-- anywhere. If the calculator dies between the "about to read" marker and the value, the read itself
-- is the fault and luagiac_init cannot survive its own first line on this hardware. If both read
-- back fine, the address is harmless and this lead is dead.

platform.apilevel = '2.0'

local shown = {}
local ran = false

local core_ok = pcall(nrequire, "nps_split")

local function t(s)
    shown[#shown + 1] = s
    if core_ok and nps_split and nps_split.trace then
        pcall(nps_split.trace, s)
    end
end

local function peek(label, addr)
    t(label .. " about to read " .. string.format("%08x", addr))
    local ok, v = pcall(nps_split.peek, addr)
    t(label .. " ok=" .. tostring(ok) .. " value=" .. tostring(v))
end

local function probe()
    t("giacprobe4 start core=" .. tostring(core_ok))
    if not (nps_split and nps_split.peek) then
        t("nps_split.peek is missing")
        return
    end

    -- A known good CX II peripheral first, so a fault on P1 cannot be blamed on peek itself.
    peek("P0 keypad", 0x90140810)
    peek("P1 exam green", 0x90110b04)
    peek("P2 exam red", 0x90110b0c)

    t("giacprobe4 reads done, loading luagiac")
    local giac_ok = luagiac ~= nil or pcall(nrequire, "luagiac")
    t("luagiac loaded=" .. tostring(giac_ok))

    if not luagiac or not luagiac.caseval then
        t("luagiac.caseval is missing")
        return
    end

    t("G1 asking: 1+1")
    local ok1, out1 = pcall(luagiac.caseval, "1+1")
    t("G1 ok=" .. tostring(ok1) .. " out=" .. string.sub(tostring(out1), 1, 40))

    t("giacprobe4 done")
end

function on.paint(gc)
    gc:setFont("sansserif", "r", 9)
    gc:setColorRGB(0, 0, 0)
    gc:drawString(ran and "giac probe 4: finished" or "giac probe 4: press enter", 3, 0, "top")
    local y = 15
    for i = math.max(1, #shown - 13), #shown do
        gc:drawString(shown[i], 3, y, "top")
        y = y + 15
    end
end

function on.enterKey()
    ran = true
    local ok, err = pcall(probe)
    if not ok then
        t("probe raised: " .. tostring(err))
    end
    platform.window:invalidate()
end

on.returnKey = on.enterKey
