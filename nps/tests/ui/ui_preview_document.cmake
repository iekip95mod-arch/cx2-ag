file(READ "${SOURCE}/lua/nps_v4.lua" application)
file(READ "${SOURCE}/tests/target/ui_preview_device.lua" probe)
file(WRITE "${OUTPUT}" "${application}\n${probe}\n")
